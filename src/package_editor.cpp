#include "package_editor.hpp"

#include <fastxlsx/detail/formula_reference_audit.hpp>
#include <fastxlsx/detail/worksheet_event_reader.hpp>
#include <fastxlsx/detail/worksheet_transformer.hpp>
#include <fastxlsx/detail/xml.hpp>
#include <fastxlsx/workbook.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <exception>
#include <fstream>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace fastxlsx::detail {
namespace {

constexpr std::string_view content_type_worksheet =
    "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml";
constexpr std::string_view content_type_workbook =
    "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml";
constexpr std::string_view content_type_shared_strings =
    "application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml";
constexpr std::string_view content_type_core_properties =
    "application/vnd.openxmlformats-package.core-properties+xml";
constexpr std::string_view content_type_extended_properties =
    "application/vnd.openxmlformats-officedocument.extended-properties+xml";
constexpr std::string_view relationship_type_calc_chain =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain";
constexpr std::string_view relationship_type_core_properties =
    "http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties";
constexpr std::string_view relationship_type_extended_properties =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties";
constexpr std::string_view relationship_type_control =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/control";
constexpr std::string_view relationship_type_drawing =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing";
constexpr std::string_view relationship_type_hyperlink =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink";
constexpr std::string_view relationship_type_image =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image";
constexpr std::string_view relationship_type_ole_object =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/oleObject";
constexpr std::string_view relationship_type_printer_settings =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/printerSettings";
constexpr std::string_view relationship_type_table =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/table";
constexpr std::string_view relationship_type_vml_drawing =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing";
constexpr std::string_view office_document_relationships_namespace =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
constexpr std::string_view content_types_entry_name = "[Content_Types].xml";
constexpr std::string_view package_relationships_entry_name = "_rels/.rels";
constexpr std::string_view relationship_part_extension = ".rels";
constexpr std::string_view relationship_part_segment = "/_rels/";
constexpr std::string_view root_relationships_prefix = "_rels/";
constexpr std::string_view worksheet_cell_replacement_output_input_context =
    "current worksheet input for worksheet cell replacement output";
constexpr std::string_view worksheet_sheet_data_replacement_output_input_context =
    "current worksheet input for worksheet sheetData replacement output";
constexpr std::uint16_t stored_compression_method = 0;
constexpr std::uint16_t deflate_compression_method = 8;
constexpr std::size_t package_entry_file_chunk_size = 1024U * 1024U;
constexpr std::size_t package_entry_reader_chunk_size = 1024U * 1024U;

std::uint64_t steady_clock_elapsed_milliseconds(
    std::chrono::steady_clock::time_point start)
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start)
            .count());
}

struct XmlTagRange {
    std::size_t open = 0;
    std::size_t close = 0;
};

struct XmlNamespaceBinding {
    std::string prefix;
    std::string uri;
};

using XmlNamespaceScope = std::vector<XmlNamespaceBinding>;

struct WorkbookCalcPrScan {
    XmlTagRange workbook_tag;
    XmlTagRange calc_pr_tag;
    std::size_t closing_workbook = std::string_view::npos;
    bool has_workbook = false;
    bool has_direct_calc_pr = false;
};

struct WorksheetLocalMetadataAudit {
    std::string_view element;
    std::string_view description;
};

struct WorksheetRelationshipReference {
    std::string element;
    std::string relationship_id;
};

struct WorksheetRelationshipReferenceAuditResult {
    std::vector<std::string> notes;
    std::vector<WorksheetRelationshipReferenceAudit> audits;
};

struct WorksheetPayloadDependencyAuditResult {
    std::vector<std::string> notes;
    std::vector<WorksheetPayloadDependencyAudit> audits;
};

PackageManifest build_manifest_from_reader(const PackageReader& reader)
{
    PackageManifest manifest;
    for (const ContentTypeDefault& item : reader.content_types().defaults()) {
        manifest.content_types().add_default(item.extension, item.content_type);
    }
    for (const ContentTypeOverride& item : reader.content_types().overrides()) {
        manifest.content_types().add_override(item.part_name, item.content_type);
    }

    for (const PackagePart& part : reader.part_index().parts()) {
        PackagePart& imported_part = manifest.ensure_part(part.name);
        imported_part.content_type = part.content_type;
    }

    for (const Relationship& relationship : reader.package_relationships().relationships()) {
        manifest.add_package_relationship(relationship);
    }

    for (const PackagePart& part : reader.part_index().parts()) {
        const RelationshipSet* relationships = reader.relationships_for(part.name);
        if (relationships == nullptr) {
            continue;
        }
        for (const Relationship& relationship : relationships->relationships()) {
            manifest.add_relationship(part.name, relationship);
        }
    }

    return manifest;
}

bool is_xml_space(char ch) noexcept
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
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
    const std::size_t colon = name.rfind(':');
    return colon == std::string_view::npos ? std::string_view() : name.substr(0, colon);
}

std::size_t start_tag_attribute_offset(std::string_view xml, const XmlTagRange& tag)
{
    std::size_t offset = tag.open + 1;
    while (offset < tag.close && !is_xml_name_stop(xml[offset])) {
        ++offset;
    }
    return offset;
}

bool read_next_attribute(std::string_view xml, const XmlTagRange& tag,
    std::size_t& offset, std::string_view& attribute_name,
    std::size_t& value_begin, std::size_t& value_end)
{
    while (offset < tag.close && is_xml_space(xml[offset])) {
        ++offset;
    }
    if (offset >= tag.close || xml[offset] == '/') {
        return false;
    }

    const std::size_t name_begin = offset;
    while (offset < tag.close && !is_xml_name_stop(xml[offset])) {
        ++offset;
    }
    if (name_begin == offset) {
        throw FastXlsxError("small XML part attribute name is missing");
    }
    attribute_name = xml.substr(name_begin, offset - name_begin);

    while (offset < tag.close && is_xml_space(xml[offset])) {
        ++offset;
    }
    if (offset >= tag.close || xml[offset] != '=') {
        throw FastXlsxError("small XML part attribute is missing '='");
    }
    ++offset;
    while (offset < tag.close && is_xml_space(xml[offset])) {
        ++offset;
    }
    if (offset >= tag.close || (xml[offset] != '"' && xml[offset] != '\'')) {
        throw FastXlsxError("small XML part attribute value must be quoted");
    }

    const char quote = xml[offset++];
    value_begin = offset;
    while (offset < tag.close && xml[offset] != quote) {
        ++offset;
    }
    if (offset >= tag.close) {
        throw FastXlsxError("small XML part attribute value is not closed");
    }
    value_end = offset;
    ++offset;
    return true;
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
    XmlNamespaceScope& namespaces, std::string_view xml, const XmlTagRange& tag)
{
    std::size_t offset = start_tag_attribute_offset(xml, tag);
    std::string_view attribute_name;
    std::size_t value_begin = 0;
    std::size_t value_end = 0;
    while (read_next_attribute(xml, tag, offset, attribute_name, value_begin, value_end)) {
        constexpr std::string_view namespace_prefix = "xmlns:";
        if (attribute_name.rfind(namespace_prefix, 0) != 0) {
            continue;
        }

        upsert_namespace_binding(namespaces,
            std::string(attribute_name.substr(namespace_prefix.size())),
            std::string(xml.substr(value_begin, value_end - value_begin)));
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

    throw FastXlsxError("small XML part tag is not closed");
}

std::size_t find_xml_tag_end_or_npos(std::string_view xml, std::size_t open_offset) noexcept
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

    return std::string_view::npos;
}

static constexpr std::string_view xml_cdata_open = "<![CDATA[";
static constexpr std::string_view xml_cdata_close = "]]>";
static constexpr std::string_view xml_processing_instruction_open = "<?";
static constexpr std::string_view xml_processing_instruction_close = "?>";

bool is_xml_cdata_start(std::string_view xml, std::size_t open_offset) noexcept
{
    return open_offset <= xml.size()
        && xml.substr(open_offset, xml_cdata_open.size()) == xml_cdata_open;
}

std::size_t find_xml_cdata_end_or_npos(std::string_view xml, std::size_t open_offset) noexcept
{
    const std::size_t close =
        xml.find(xml_cdata_close, open_offset + xml_cdata_open.size());
    if (close == std::string_view::npos) {
        return std::string_view::npos;
    }
    return close + xml_cdata_close.size();
}

bool is_xml_processing_instruction_start(std::string_view xml, std::size_t open_offset) noexcept
{
    return open_offset <= xml.size()
        && xml.substr(open_offset, xml_processing_instruction_open.size())
            == xml_processing_instruction_open;
}

std::size_t find_xml_processing_instruction_end_or_npos(
    std::string_view xml,
    std::size_t open_offset) noexcept
{
    const std::size_t close = xml.find(
        xml_processing_instruction_close,
        open_offset + xml_processing_instruction_open.size());
    if (close == std::string_view::npos) {
        return std::string_view::npos;
    }
    return close + xml_processing_instruction_close.size();
}

std::string_view start_tag_name(std::string_view xml, const XmlTagRange& tag)
{
    std::size_t offset = tag.open + 1;
    while (offset < tag.close && is_xml_space(xml[offset])) {
        ++offset;
    }
    const std::size_t begin = offset;
    while (offset < tag.close && !is_xml_name_stop(xml[offset])) {
        ++offset;
    }
    if (begin == offset) {
        throw FastXlsxError("small XML part tag name is missing");
    }
    return xml.substr(begin, offset - begin);
}

std::string_view closing_tag_name(std::string_view xml, const XmlTagRange& tag)
{
    std::size_t offset = tag.open + 2;
    while (offset < tag.close && is_xml_space(xml[offset])) {
        ++offset;
    }
    const std::size_t begin = offset;
    while (offset < tag.close && !is_xml_name_stop(xml[offset])) {
        ++offset;
    }
    if (begin == offset) {
        throw FastXlsxError("small XML part closing tag name is missing");
    }
    return xml.substr(begin, offset - begin);
}

bool find_start_tag_after(std::string_view xml, std::string_view local_name,
    std::size_t& offset, XmlTagRange& output)
{
    for (;;) {
        const std::size_t open = xml.find('<', offset);
        if (open == std::string_view::npos) {
            return false;
        }
        if (open + 1 >= xml.size()) {
            throw FastXlsxError("small XML part tag is truncated");
        }
        if (xml.substr(open, 4) == "<!--") {
            const std::size_t close = xml.find("-->", open + 4);
            if (close == std::string_view::npos) {
                throw FastXlsxError("small XML part comment is not closed");
            }
            offset = close + 3;
            continue;
        }

        const char marker = xml[open + 1];
        const std::size_t close = find_xml_tag_end(xml, open);
        if (marker != '/' && marker != '?' && marker != '!') {
            XmlTagRange candidate {open, close};
            if (local_xml_name(start_tag_name(xml, candidate)) == local_name) {
                output = candidate;
                offset = close + 1;
                return true;
            }
        }
        offset = close + 1;
    }
}

bool find_start_tag(std::string_view xml, std::string_view local_name, XmlTagRange& output)
{
    std::size_t offset = 0;
    return find_start_tag_after(xml, local_name, offset, output);
}

bool prefix_is_xml_prolog_only(std::string_view xml, std::size_t root_open)
{
    std::size_t offset = 0;
    while (offset < root_open) {
        if (is_xml_space(xml[offset])) {
            ++offset;
            continue;
        }
        if (xml.substr(offset, 4) == "<!--") {
            const std::size_t close = xml.find("-->", offset + 4);
            if (close == std::string_view::npos || close + 3 > root_open) {
                return false;
            }
            offset = close + 3;
            continue;
        }
        if (xml.substr(offset, 2) == "<?") {
            const std::size_t close = xml.find("?>", offset + 2);
            if (close == std::string_view::npos || close + 2 > root_open) {
                return false;
            }
            offset = close + 2;
            continue;
        }
        return false;
    }

    return offset == root_open;
}

bool is_self_closing_tag(std::string_view xml, const XmlTagRange& tag) noexcept
{
    std::size_t offset = tag.close;
    while (offset > tag.open + 1) {
        --offset;
        if (!is_xml_space(xml[offset])) {
            return xml[offset] == '/';
        }
    }
    return false;
}

bool find_closing_tag_end_after(std::string_view xml, std::string_view local_name,
    std::size_t search_offset, std::size_t& output)
{
    for (std::size_t offset = search_offset;;) {
        const std::size_t open = xml.find("</", offset);
        if (open == std::string_view::npos) {
            return false;
        }
        const std::size_t close = find_xml_tag_end(xml, open);

        std::size_t name_begin = open + 2;
        while (name_begin < close && is_xml_space(xml[name_begin])) {
            ++name_begin;
        }
        std::size_t name_end = name_begin;
        while (name_end < close && !is_xml_name_stop(xml[name_end])) {
            ++name_end;
        }
        if (name_begin == name_end) {
            throw FastXlsxError("small XML part closing tag name is missing");
        }
        if (local_xml_name(xml.substr(name_begin, name_end - name_begin)) == local_name) {
            output = close + 1;
            return true;
        }

        offset = close + 1;
    }
}

bool find_attribute_value(std::string_view xml, const XmlTagRange& tag,
    std::string_view local_name, std::size_t& value_begin, std::size_t& value_end)
{
    std::size_t offset = start_tag_attribute_offset(xml, tag);
    std::string_view attribute_name;
    std::size_t current_value_begin = 0;
    std::size_t current_value_end = 0;
    while (read_next_attribute(xml, tag, offset, attribute_name,
        current_value_begin, current_value_end)) {
        if (local_xml_name(attribute_name) == local_name) {
            value_begin = current_value_begin;
            value_end = current_value_end;
            return true;
        }
    }

    return false;
}

bool find_relationship_id_value(
    std::string_view xml, const XmlTagRange& tag,
    const std::vector<XmlNamespaceScope>& namespace_stack,
    std::size_t& value_begin, std::size_t& value_end)
{
    std::size_t offset = start_tag_attribute_offset(xml, tag);
    std::string_view attribute_name;
    std::size_t current_value_begin = 0;
    std::size_t current_value_end = 0;
    while (read_next_attribute(xml, tag, offset, attribute_name,
        current_value_begin, current_value_end)) {
        if (local_xml_name(attribute_name) != "id") {
            continue;
        }

        const std::string_view prefix = xml_name_prefix(attribute_name);
        if (prefix.empty()) {
            continue;
        }

        const std::string* uri = find_namespace_uri(namespace_stack, prefix);
        if (uri != nullptr && *uri == office_document_relationships_namespace) {
            value_begin = current_value_begin;
            value_end = current_value_end;
            return true;
        }
    }

    return false;
}

std::size_t start_tag_attribute_insert_offset(std::string_view xml, const XmlTagRange& tag)
{
    std::size_t offset = tag.close;
    while (offset > tag.open + 1 && is_xml_space(xml[offset - 1])) {
        --offset;
    }
    if (offset > tag.open + 1 && xml[offset - 1] == '/') {
        --offset;
        while (offset > tag.open + 1 && is_xml_space(xml[offset - 1])) {
            --offset;
        }
    }
    return offset;
}

WorkbookCalcPrScan scan_workbook_direct_calc_pr(std::string_view workbook_xml)
{
    WorkbookCalcPrScan result;
    bool inside_workbook = false;
    std::size_t element_depth = 0;

    for (std::size_t offset = 0;;) {
        const std::size_t open = workbook_xml.find('<', offset);
        if (open == std::string_view::npos) {
            break;
        }
        if (open + 1 >= workbook_xml.size()) {
            throw FastXlsxError("small XML part tag is truncated");
        }

        if (workbook_xml.substr(open, 4) == "<!--") {
            const std::size_t close = workbook_xml.find("-->", open + 4);
            if (close == std::string_view::npos) {
                throw FastXlsxError("small XML part comment is not closed");
            }
            offset = close + 3;
            continue;
        }

        const std::size_t close = find_xml_tag_end(workbook_xml, open);
        const char marker = workbook_xml[open + 1];
        const XmlTagRange tag {open, close};

        if (marker == '/') {
            if (inside_workbook) {
                if (element_depth == 1
                    && local_xml_name(closing_tag_name(workbook_xml, tag)) == "workbook") {
                    result.closing_workbook = open;
                    return result;
                }
                if (element_depth > 0) {
                    --element_depth;
                }
                if (element_depth == 0) {
                    inside_workbook = false;
                }
            }
            offset = close + 1;
            continue;
        }

        if (marker == '?' || marker == '!') {
            offset = close + 1;
            continue;
        }

        const std::string_view local_name = local_xml_name(start_tag_name(workbook_xml, tag));
        const bool self_closing = is_self_closing_tag(workbook_xml, tag);
        if (!inside_workbook) {
            if (local_name == "workbook") {
                result.workbook_tag = tag;
                result.has_workbook = true;
                if (!self_closing) {
                    inside_workbook = true;
                    element_depth = 1;
                }
            }
            offset = close + 1;
            continue;
        }

        if (element_depth == 1 && local_name == "calcPr") {
            result.calc_pr_tag = tag;
            result.has_direct_calc_pr = true;
            return result;
        }

        if (!self_closing) {
            ++element_depth;
        }
        offset = close + 1;
    }

    if (!result.has_workbook) {
        throw FastXlsxError("workbook XML root is missing");
    }
    return result;
}

std::string request_full_calculation_in_workbook_xml(std::string workbook_xml)
{
    const WorkbookCalcPrScan workbook_scan =
        scan_workbook_direct_calc_pr(workbook_xml);
    if (workbook_scan.has_direct_calc_pr) {
        std::size_t value_begin = 0;
        std::size_t value_end = 0;
        if (find_attribute_value(
                workbook_xml, workbook_scan.calc_pr_tag, "fullCalcOnLoad",
                value_begin, value_end)) {
            workbook_xml.replace(value_begin, value_end - value_begin, "1");
            return workbook_xml;
        }

        workbook_xml.insert(
            start_tag_attribute_insert_offset(workbook_xml, workbook_scan.calc_pr_tag),
            R"( fullCalcOnLoad="1")");
        return workbook_xml;
    }

    if (workbook_scan.closing_workbook == std::string_view::npos) {
        throw FastXlsxError("workbook XML closing tag is missing");
    }

    const std::string_view prefix =
        xml_name_prefix(start_tag_name(workbook_xml, workbook_scan.workbook_tag));
    std::string calc_pr_name(prefix);
    if (!calc_pr_name.empty()) {
        calc_pr_name.push_back(':');
    }
    calc_pr_name += "calcPr";

    std::string calc_pr_xml = "<";
    calc_pr_xml += calc_pr_name;
    calc_pr_xml += R"( calcId="124519" fullCalcOnLoad="1"/>)";
    workbook_xml.insert(workbook_scan.closing_workbook, calc_pr_xml);
    return workbook_xml;
}

bool find_unqualified_attribute_value(std::string_view xml, const XmlTagRange& tag,
    std::string_view name, std::size_t& value_begin, std::size_t& value_end)
{
    std::size_t offset = tag.open + 1;
    while (offset < tag.close && !is_xml_name_stop(xml[offset])) {
        ++offset;
    }

    while (offset < tag.close) {
        while (offset < tag.close && is_xml_space(xml[offset])) {
            ++offset;
        }
        if (offset >= tag.close || xml[offset] == '/') {
            return false;
        }

        const std::size_t name_begin = offset;
        while (offset < tag.close && !is_xml_name_stop(xml[offset])) {
            ++offset;
        }
        if (name_begin == offset) {
            throw FastXlsxError("small XML part attribute name is missing");
        }
        const std::string_view attribute_name = xml.substr(name_begin, offset - name_begin);

        while (offset < tag.close && is_xml_space(xml[offset])) {
            ++offset;
        }
        if (offset >= tag.close || xml[offset] != '=') {
            throw FastXlsxError("small XML part attribute is missing '='");
        }
        ++offset;
        while (offset < tag.close && is_xml_space(xml[offset])) {
            ++offset;
        }
        if (offset >= tag.close || (xml[offset] != '"' && xml[offset] != '\'')) {
            throw FastXlsxError("small XML part attribute value must be quoted");
        }

        const char quote = xml[offset++];
        const std::size_t current_value_begin = offset;
        while (offset < tag.close && xml[offset] != quote) {
            ++offset;
        }
        if (offset >= tag.close) {
            throw FastXlsxError("small XML part attribute value is not closed");
        }
        const std::size_t current_value_end = offset;
        ++offset;

        if (attribute_name == name) {
            value_begin = current_value_begin;
            value_end = current_value_end;
            return true;
        }
    }

    return false;
}

bool has_direct_workbook_child_tag(std::string_view workbook_xml, std::string_view child_name)
{
    bool inside_workbook = false;
    bool saw_workbook = false;
    std::size_t element_depth = 0;

    for (std::size_t offset = 0;;) {
        const std::size_t open = workbook_xml.find('<', offset);
        if (open == std::string_view::npos) {
            break;
        }
        if (open + 1 >= workbook_xml.size()) {
            throw FastXlsxError("small XML part tag is truncated");
        }
        if (workbook_xml.substr(open, 4) == "<!--") {
            const std::size_t close = workbook_xml.find("-->", open + 4);
            if (close == std::string_view::npos) {
                throw FastXlsxError("small XML part comment is not closed");
            }
            offset = close + 3;
            continue;
        }

        const std::size_t close = find_xml_tag_end(workbook_xml, open);
        const char marker = workbook_xml[open + 1];
        const XmlTagRange tag {open, close};

        if (marker == '/') {
            if (inside_workbook) {
                const std::string_view local_name =
                    local_xml_name(closing_tag_name(workbook_xml, tag));
                if (element_depth == 1 && local_name == "workbook") {
                    return false;
                }
                if (element_depth > 0) {
                    --element_depth;
                }
                if (element_depth == 0) {
                    inside_workbook = false;
                }
            }
            offset = close + 1;
            continue;
        }

        if (marker == '?' || marker == '!') {
            offset = close + 1;
            continue;
        }

        const std::string_view local_name = local_xml_name(start_tag_name(workbook_xml, tag));
        const bool self_closing = is_self_closing_tag(workbook_xml, tag);
        if (!inside_workbook) {
            if (local_name == "workbook") {
                saw_workbook = true;
                if (!self_closing) {
                    inside_workbook = true;
                    element_depth = 1;
                }
            }
            offset = close + 1;
            continue;
        }

        if (element_depth == 1 && local_name == child_name) {
            return true;
        }
        if (!self_closing) {
            ++element_depth;
        }
        offset = close + 1;
    }

    if (!saw_workbook) {
        throw FastXlsxError("workbook XML root is missing");
    }
    return false;
}

char ascii_lower(char ch) noexcept
{
    if (ch >= 'A' && ch <= 'Z') {
        return static_cast<char>(ch - 'A' + 'a');
    }
    return ch;
}

bool ascii_equals_ignoring_case(std::string_view lhs, std::string_view rhs) noexcept
{
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (ascii_lower(lhs[index]) != ascii_lower(rhs[index])) {
            return false;
        }
    }
    return true;
}

void validate_sheet_catalog_rename_target(std::string_view new_name)
{
    if (new_name.empty()) {
        throw FastXlsxError("workbook sheet rename target name is empty");
    }
    if (new_name.size() > 31U) {
        throw FastXlsxError("workbook sheet rename target name exceeds 31 characters");
    }
    if (new_name.find_first_of(":\\/?*[]") != std::string_view::npos) {
        throw FastXlsxError("workbook sheet rename target name contains invalid characters");
    }
    if (new_name.front() == '\'' || new_name.back() == '\'') {
        throw FastXlsxError(
            "workbook sheet rename target name cannot start or end with an apostrophe");
    }
}

WorkbookSheetReference select_sheet_catalog_rename_target(
    const std::vector<WorkbookSheetReference>& sheets, std::string_view old_name,
    std::string_view new_name)
{
    const WorkbookSheetReference* old_match = nullptr;
    for (const WorkbookSheetReference& sheet : sheets) {
        if (sheet.name == new_name) {
            throw FastXlsxError("workbook sheet rename target name already exists");
        }
        if (sheet.name != old_name && ascii_equals_ignoring_case(sheet.name, new_name)) {
            throw FastXlsxError(
                "workbook sheet rename target name already exists case-insensitively");
        }
        if (sheet.name != old_name) {
            continue;
        }
        if (old_match != nullptr) {
            throw FastXlsxError("workbook sheet name is ambiguous");
        }
        old_match = &sheet;
    }
    if (old_match == nullptr) {
        throw FastXlsxError("workbook sheet name is not present");
    }
    return *old_match;
}

bool find_workbook_sheet_name_attribute_value(std::string_view workbook_xml,
    const WorkbookSheetReference& target, std::size_t& value_begin,
    std::size_t& value_end)
{
    bool inside_workbook = false;
    bool inside_sheets = false;
    std::size_t element_depth = 0;
    std::size_t sheets_child_depth = 0;
    std::vector<XmlNamespaceScope> namespace_stack;

    for (std::size_t offset = 0;;) {
        const std::size_t open = workbook_xml.find('<', offset);
        if (open == std::string_view::npos) {
            break;
        }
        if (open + 1 >= workbook_xml.size()) {
            throw FastXlsxError("small XML part tag is truncated");
        }
        if (workbook_xml.substr(open, 4) == "<!--") {
            const std::size_t close = workbook_xml.find("-->", open + 4);
            if (close == std::string_view::npos) {
                throw FastXlsxError("small XML part comment is not closed");
            }
            offset = close + 3;
            continue;
        }

        const std::size_t close = find_xml_tag_end(workbook_xml, open);
        const char marker = workbook_xml[open + 1];
        const XmlTagRange tag {open, close};

        if (marker == '/') {
            const std::string_view local_name =
                local_xml_name(closing_tag_name(workbook_xml, tag));
            if (inside_sheets) {
                if (sheets_child_depth == 0 && local_name == "sheets") {
                    inside_sheets = false;
                } else if (sheets_child_depth > 0) {
                    --sheets_child_depth;
                }
            }
            if (inside_workbook) {
                if (element_depth > 0) {
                    --element_depth;
                }
                if (element_depth == 0) {
                    inside_workbook = false;
                }
            }
            if (namespace_stack.empty()) {
                throw FastXlsxError("small XML part closing tag has no matching start tag");
            }
            namespace_stack.pop_back();
            offset = close + 1;
            continue;
        }

        if (marker == '?' || marker == '!') {
            offset = close + 1;
            continue;
        }

        XmlNamespaceScope current_scope;
        ingest_namespace_declarations(current_scope, workbook_xml, tag);
        std::vector<XmlNamespaceScope> current_namespaces = namespace_stack;
        current_namespaces.push_back(current_scope);

        const std::string_view local_name = local_xml_name(start_tag_name(workbook_xml, tag));
        const bool self_closing = is_self_closing_tag(workbook_xml, tag);
        if (!inside_sheets) {
            if (inside_workbook && element_depth == 1 && local_name == "sheets") {
                if (!self_closing) {
                    inside_sheets = true;
                    sheets_child_depth = 0;
                }
            }
        } else {
            if (sheets_child_depth == 0 && local_name == "sheet") {
                std::size_t sheet_id_begin = 0;
                std::size_t sheet_id_end = 0;
                std::size_t relationship_id_begin = 0;
                std::size_t relationship_id_end = 0;
                if (find_unqualified_attribute_value(workbook_xml, tag, "sheetId",
                        sheet_id_begin, sheet_id_end)
                    && find_relationship_id_value(workbook_xml, tag,
                        current_namespaces, relationship_id_begin, relationship_id_end)
                    && workbook_xml.substr(sheet_id_begin, sheet_id_end - sheet_id_begin)
                        == target.sheet_id
                    && workbook_xml.substr(relationship_id_begin,
                           relationship_id_end - relationship_id_begin)
                        == target.relationship_id) {
                    if (!find_unqualified_attribute_value(
                            workbook_xml, tag, "name", value_begin, value_end)) {
                        throw FastXlsxError("workbook sheet is missing name");
                    }
                    return true;
                }
            }
            if (!self_closing) {
                ++sheets_child_depth;
            }
        }

        if (!self_closing) {
            if (!inside_workbook && local_name == "workbook") {
                inside_workbook = true;
                element_depth = 1;
            } else if (inside_workbook) {
                ++element_depth;
            }
            namespace_stack.push_back(std::move(current_scope));
        }
        offset = close + 1;
    }

    return false;
}

std::size_t find_streaming_xml_tag_end(std::string_view xml, std::size_t open) noexcept
{
    if (xml.substr(open, 4) == "<!--") {
        const std::size_t comment_close = xml.find("-->", open + 4);
        if (comment_close == std::string_view::npos) {
            return std::string_view::npos;
        }
        return comment_close + 2;
    }
    if (is_xml_processing_instruction_start(xml, open)) {
        const std::size_t close = find_xml_processing_instruction_end_or_npos(xml, open);
        if (close == std::string_view::npos) {
            return std::string_view::npos;
        }
        return close - 1;
    }

    char quote = '\0';
    for (std::size_t offset = open + 1; offset < xml.size(); ++offset) {
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
    return std::string_view::npos;
}

void require_streaming_sheet_data_outside_text(std::string_view text, bool saw_sheet_data_root)
{
    for (const char ch : text) {
        if (!is_xml_space(ch)) {
            if (!saw_sheet_data_root) {
                throw FastXlsxError(
                    "sheetData replacement XML must start with a sheetData element");
            }
            throw FastXlsxError(
                "sheetData replacement XML must contain only one sheetData element");
        }
    }
}

void scan_sheet_data_start_tags_from_chunk_source(
    const WorksheetInputChunkCallback& read_next_chunk,
    const std::function<void(std::string_view, const XmlTagRange&)>& on_start_tag,
    WorksheetEventReaderOptions options)
{
    if (options.max_window_bytes == 0) {
        throw FastXlsxError("sheetData replacement XML streaming scanner requires a bounded window");
    }

    std::string buffer;
    std::string chunk;
    std::vector<std::string> element_stack;
    bool inside_comment = false;
    bool inside_cdata = false;
    bool inside_processing_instruction = false;
    bool saw_sheet_data_root = false;
    bool root_closed = false;

    auto process_buffer = [&](bool final_chunk) {
        for (;;) {
            if (inside_comment) {
                const std::size_t comment_close = buffer.find("-->");
                if (comment_close == std::string::npos) {
                    if (final_chunk) {
                        throw FastXlsxError("sheetData replacement XML comment is not closed");
                    }
                    if (buffer.size() > 2) {
                        buffer.erase(0, buffer.size() - 2);
                    }
                    return;
                }
                buffer.erase(0, comment_close + 3);
                inside_comment = false;
                continue;
            }
            if (inside_cdata) {
                const std::size_t cdata_close = buffer.find(xml_cdata_close);
                if (cdata_close == std::string::npos) {
                    if (final_chunk) {
                        throw FastXlsxError("sheetData replacement XML CDATA is not closed");
                    }
                    if (buffer.size() > xml_cdata_close.size() - 1U) {
                        buffer.erase(0, buffer.size() - (xml_cdata_close.size() - 1U));
                    }
                    return;
                }
                buffer.erase(0, cdata_close + xml_cdata_close.size());
                inside_cdata = false;
                continue;
            }
            if (inside_processing_instruction) {
                const std::size_t processing_instruction_close =
                    buffer.find(xml_processing_instruction_close);
                if (processing_instruction_close == std::string::npos) {
                    if (final_chunk) {
                        throw FastXlsxError(
                            "sheetData replacement XML processing instruction is not closed");
                    }
                    if (buffer.size() > xml_processing_instruction_close.size() - 1U) {
                        buffer.erase(0,
                            buffer.size() - (xml_processing_instruction_close.size() - 1U));
                    }
                    return;
                }
                buffer.erase(
                    0, processing_instruction_close + xml_processing_instruction_close.size());
                inside_processing_instruction = false;
                continue;
            }

            const std::size_t open = buffer.find('<');
            if (open == std::string::npos) {
                if (element_stack.empty()) {
                    require_streaming_sheet_data_outside_text(buffer, saw_sheet_data_root);
                }
                buffer.clear();
                return;
            }

            if (element_stack.empty()) {
                require_streaming_sheet_data_outside_text(
                    std::string_view(buffer).substr(0, open), saw_sheet_data_root);
                if (root_closed) {
                    throw FastXlsxError(
                        "sheetData replacement XML must contain only one sheetData element");
                }
            }

            if (buffer.substr(open, 4) == "<!--") {
                if (element_stack.empty() && !saw_sheet_data_root) {
                    throw FastXlsxError(
                        "sheetData replacement XML must start with a sheetData element");
                }
                const std::size_t comment_close = buffer.find("-->", open + 4);
                if (comment_close == std::string::npos) {
                    if (final_chunk) {
                        throw FastXlsxError("sheetData replacement XML comment is not closed");
                    }
                    buffer.erase(0, open + 4);
                    if (buffer.size() > 2) {
                        buffer.erase(0, buffer.size() - 2);
                    }
                    inside_comment = true;
                    return;
                }
                buffer.erase(0, comment_close + 3);
                continue;
            }
            if (is_xml_cdata_start(buffer, open)) {
                if (element_stack.empty()) {
                    if (!saw_sheet_data_root) {
                        throw FastXlsxError(
                            "sheetData replacement XML must start with a sheetData element");
                    }
                    throw FastXlsxError(
                        "sheetData replacement XML must contain only one sheetData element");
                }
                const std::size_t cdata_close =
                    find_xml_cdata_end_or_npos(buffer, open);
                if (cdata_close == std::string::npos) {
                    if (final_chunk) {
                        throw FastXlsxError("sheetData replacement XML CDATA is not closed");
                    }
                    buffer.erase(0, open + xml_cdata_open.size());
                    if (buffer.size() > xml_cdata_close.size() - 1U) {
                        buffer.erase(0, buffer.size() - (xml_cdata_close.size() - 1U));
                    }
                    inside_cdata = true;
                    return;
                }
                buffer.erase(0, cdata_close);
                continue;
            }
            if (is_xml_processing_instruction_start(buffer, open)) {
                if (element_stack.empty() && !saw_sheet_data_root) {
                    throw FastXlsxError(
                        "sheetData replacement XML must start with a sheetData element");
                }
                const std::size_t processing_instruction_close =
                    find_xml_processing_instruction_end_or_npos(buffer, open);
                if (processing_instruction_close == std::string::npos) {
                    if (final_chunk) {
                        throw FastXlsxError(
                            "sheetData replacement XML processing instruction is not closed");
                    }
                    buffer.erase(0, open + xml_processing_instruction_open.size());
                    if (buffer.size() > xml_processing_instruction_close.size() - 1U) {
                        buffer.erase(0,
                            buffer.size() - (xml_processing_instruction_close.size() - 1U));
                    }
                    inside_processing_instruction = true;
                    return;
                }
                buffer.erase(0, processing_instruction_close);
                continue;
            }

            const std::size_t close = find_streaming_xml_tag_end(buffer, open);
            if (close == std::string_view::npos) {
                if (open > 0) {
                    buffer.erase(0, open);
                }
                if (final_chunk) {
                    throw FastXlsxError("sheetData replacement XML tag is truncated");
                }
                return;
            }

            const std::string_view tag_xml(buffer.data() + open, close - open + 1);
            const char marker = tag_xml.size() > 1 ? tag_xml[1] : '\0';
            const XmlTagRange tag {0, tag_xml.size() - 1};
            if (marker == '/') {
                const std::string local_name(closing_tag_name(tag_xml, tag));
                const std::string closing_name(local_xml_name(local_name));
                if (element_stack.empty() || element_stack.back() != closing_name) {
                    throw FastXlsxError(
                        "sheetData replacement XML contains mismatched closing tag");
                }
                element_stack.pop_back();
                if (element_stack.empty()) {
                    root_closed = true;
                }
                buffer.erase(0, close + 1);
                continue;
            }
            if (marker == '?' || marker == '!') {
                if (element_stack.empty() && !saw_sheet_data_root) {
                    throw FastXlsxError(
                        "sheetData replacement XML must start with a sheetData element");
                }
                buffer.erase(0, close + 1);
                continue;
            }

            const std::string local_name(start_tag_name(tag_xml, tag));
            const std::string element_name(local_xml_name(local_name));
            if (!saw_sheet_data_root) {
                if (element_name != "sheetData") {
                    throw FastXlsxError(
                        "sheetData replacement XML must start with a sheetData element");
                }
                saw_sheet_data_root = true;
            } else if (root_closed && element_stack.empty()) {
                throw FastXlsxError(
                    "sheetData replacement XML must contain only one sheetData element");
            }

            on_start_tag(tag_xml, tag);
            if (is_self_closing_tag(tag_xml, tag)) {
                if (element_stack.empty()) {
                    root_closed = true;
                }
            } else {
                element_stack.push_back(element_name);
            }
            buffer.erase(0, close + 1);
        }
    };

    while (read_next_chunk(chunk)) {
        std::string_view remaining_chunk = chunk;
        while (!remaining_chunk.empty()) {
            if (buffer.size() >= options.max_window_bytes) {
                throw FastXlsxError(
                    "sheetData replacement XML streaming scanner exceeded bounded input window"
                    " while reading chunk of "
                    + std::to_string(remaining_chunk.size()) + " bytes with "
                    + std::to_string(buffer.size()) + " buffered bytes");
            }
            const std::size_t append_size =
                std::min(remaining_chunk.size(), options.max_window_bytes - buffer.size());
            buffer.append(remaining_chunk.substr(0, append_size));
            remaining_chunk.remove_prefix(append_size);
            process_buffer(false);
        }
    }
    process_buffer(true);

    if (!saw_sheet_data_root) {
        throw FastXlsxError("sheetData replacement XML is empty");
    }
    if (!element_stack.empty()) {
        throw FastXlsxError("sheetData replacement XML closing tag is missing");
    }
}

bool is_worksheet_relationship_reference_element(std::string_view local_name) noexcept
{
    static constexpr std::string_view relationship_elements[] = {
        "hyperlink",
        "drawing",
        "legacyDrawing",
        "picture",
        "legacyDrawingHF",
        "pageSetup",
        "oleObject",
        "control",
        "tablePart",
    };

    for (const std::string_view element : relationship_elements) {
        if (element == local_name) {
            return true;
        }
    }
    return false;
}

class WorksheetRelationshipReferenceScanner {
public:
    void process(std::string_view xml)
    {
        if (xml.empty()) {
            return;
        }
        if (inside_ignored_markup()) {
            process_ignored_markup_chunk(xml, false);
            return;
        }
        if (!retained_xml_.empty()) {
            process_retained_xml_with_following_chunk(xml);
            return;
        }
        process_chunk(xml);
    }

    void finish()
    {
        if (inside_ignored_markup()) {
            process_ignored_markup_chunk({}, true);
        }
        process_retained_xml(true);
    }

    [[nodiscard]] const std::vector<WorksheetRelationshipReference>& references() const noexcept
    {
        return references_;
    }

private:
    enum class IgnoredMarkupState {
        None,
        Comment,
        CData,
        ProcessingInstruction,
    };

    void process_chunk(std::string_view xml)
    {
        for (std::size_t offset = 0;;) {
            const std::size_t open = xml.find('<', offset);
            if (open == std::string_view::npos) {
                return;
            }
            if (open + 1 >= xml.size()) {
                retain_xml_tail(xml.substr(open));
                return;
            }
            if (xml.substr(open, 4) == "<!--") {
                const std::size_t close = xml.find("-->", open + 4);
                if (close == std::string_view::npos) {
                    enter_ignored_markup(
                        IgnoredMarkupState::Comment, xml.substr(open + 4), false);
                    return;
                }
                offset = close + 3;
                continue;
            }
            if (is_xml_cdata_start(xml, open)) {
                const std::size_t close = find_xml_cdata_end_or_npos(xml, open);
                if (close == std::string_view::npos) {
                    enter_ignored_markup(
                        IgnoredMarkupState::CData,
                        xml.substr(open + xml_cdata_open.size()), false);
                    return;
                }
                offset = close;
                continue;
            }
            if (is_xml_processing_instruction_start(xml, open)) {
                const std::size_t close =
                    find_xml_processing_instruction_end_or_npos(xml, open);
                if (close == std::string_view::npos) {
                    enter_ignored_markup(IgnoredMarkupState::ProcessingInstruction,
                        xml.substr(open + xml_processing_instruction_open.size()), false);
                    return;
                }
                offset = close;
                continue;
            }

            const std::size_t close = find_xml_tag_end_or_npos(xml, open);
            if (close == std::string_view::npos) {
                retain_xml_tail(xml.substr(open));
                return;
            }

            process_tag(xml, XmlTagRange {open, close});
            offset = close + 1;
        }
    }

    void process_retained_xml(bool final_chunk)
    {
        for (;;) {
            const std::size_t open = retained_xml_.find('<');
            if (open == std::string::npos) {
                retained_xml_.clear();
                return;
            }
            if (open > 0) {
                retained_xml_.erase(0, open);
            }
            if (retained_xml_.size() == 1) {
                if (final_chunk) {
                    throw FastXlsxError("small XML part tag is truncated");
                }
                return;
            }
            if (std::string_view(retained_xml_).substr(0, 4) == "<!--") {
                const std::size_t close = retained_xml_.find("-->", 4);
                if (close == std::string::npos) {
                    enter_ignored_markup_from_retained(
                        IgnoredMarkupState::Comment, std::string_view(retained_xml_).substr(4),
                        final_chunk);
                    return;
                }
                retained_xml_.erase(0, close + 3);
                continue;
            }
            if (is_xml_cdata_start(retained_xml_, 0)) {
                const std::size_t close = find_xml_cdata_end_or_npos(retained_xml_, 0);
                if (close == std::string::npos) {
                    enter_ignored_markup_from_retained(IgnoredMarkupState::CData,
                        std::string_view(retained_xml_).substr(xml_cdata_open.size()),
                        final_chunk);
                    return;
                }
                retained_xml_.erase(0, close);
                continue;
            }
            if (is_xml_processing_instruction_start(retained_xml_, 0)) {
                const std::size_t close =
                    find_xml_processing_instruction_end_or_npos(retained_xml_, 0);
                if (close == std::string::npos) {
                    enter_ignored_markup_from_retained(IgnoredMarkupState::ProcessingInstruction,
                        std::string_view(retained_xml_)
                            .substr(xml_processing_instruction_open.size()),
                        final_chunk);
                    return;
                }
                retained_xml_.erase(0, close);
                continue;
            }

            const std::size_t close = find_xml_tag_end_or_npos(retained_xml_, 0);
            if (close == std::string::npos) {
                if (final_chunk) {
                    throw FastXlsxError("small XML part tag is not closed");
                }
                ensure_retained_xml_within_limit();
                return;
            }

            process_tag(retained_xml_, XmlTagRange {0, close});
            retained_xml_.erase(0, close + 1);
        }
    }

    void process_retained_xml_with_following_chunk(std::string_view xml)
    {
        std::string_view remaining = xml;
        while (!remaining.empty()) {
            if (retained_xml_.size()
                >= package_editor_cell_replacement_event_window_byte_limit) {
                throw FastXlsxError(
                    "worksheet relationship-id scanner exceeded bounded input window");
            }

            const std::size_t append_size = std::min(remaining.size(),
                package_editor_cell_replacement_event_window_byte_limit
                    - retained_xml_.size());
            append_retained_xml(remaining.substr(0, append_size));
            remaining.remove_prefix(append_size);
            process_retained_xml(false);

            if (inside_ignored_markup()) {
                process_ignored_markup_chunk(remaining, false);
                return;
            }
            if (retained_xml_.empty()) {
                process_chunk(remaining);
                return;
            }
        }
    }

    [[nodiscard]] bool inside_ignored_markup() const noexcept
    {
        return ignored_markup_state_ != IgnoredMarkupState::None;
    }

    [[nodiscard]] std::string_view ignored_markup_close() const noexcept
    {
        switch (ignored_markup_state_) {
        case IgnoredMarkupState::Comment:
            return "-->";
        case IgnoredMarkupState::CData:
            return xml_cdata_close;
        case IgnoredMarkupState::ProcessingInstruction:
            return xml_processing_instruction_close;
        case IgnoredMarkupState::None:
            return {};
        }
        return {};
    }

    [[nodiscard]] std::string_view ignored_markup_error() const noexcept
    {
        switch (ignored_markup_state_) {
        case IgnoredMarkupState::Comment:
            return "small XML part comment is not closed";
        case IgnoredMarkupState::CData:
            return "small XML part CDATA is not closed";
        case IgnoredMarkupState::ProcessingInstruction:
            return "small XML part processing instruction is not closed";
        case IgnoredMarkupState::None:
            return "small XML part ignored markup is not closed";
        }
        return "small XML part ignored markup is not closed";
    }

    void enter_ignored_markup(
        IgnoredMarkupState state, std::string_view body_after_open, bool final_chunk)
    {
        retained_xml_.clear();
        ignored_markup_state_ = state;
        process_ignored_markup_chunk(body_after_open, final_chunk);
    }

    void enter_ignored_markup_from_retained(
        IgnoredMarkupState state, std::string_view body_after_open, bool final_chunk)
    {
        const std::string retained_body(body_after_open);
        enter_ignored_markup(state, retained_body, final_chunk);
    }

    void process_ignored_markup_chunk(std::string_view xml, bool final_chunk)
    {
        const std::string_view close_marker = ignored_markup_close();
        const std::size_t tail_size = close_marker.empty() ? 0U : close_marker.size() - 1U;
        const std::size_t boundary_size = std::min(xml.size(), tail_size);

        std::string boundary_window = retained_xml_;
        boundary_window.append(xml.substr(0, boundary_size));
        const std::size_t boundary_close = boundary_window.find(close_marker);
        if (boundary_close != std::string::npos) {
            const std::size_t consumed =
                boundary_close + close_marker.size() - retained_xml_.size();
            retained_xml_.clear();
            ignored_markup_state_ = IgnoredMarkupState::None;
            process_chunk(xml.substr(consumed));
            return;
        }

        const std::size_t direct_close = xml.find(close_marker);
        if (direct_close != std::string_view::npos) {
            const std::size_t consumed = direct_close + close_marker.size();
            retained_xml_.clear();
            ignored_markup_state_ = IgnoredMarkupState::None;
            process_chunk(xml.substr(consumed));
            return;
        }

        if (final_chunk) {
            throw FastXlsxError(std::string(ignored_markup_error()));
        }
        retain_ignored_markup_tail(xml, tail_size);
    }

    void retain_ignored_markup_tail(std::string_view xml, std::size_t tail_size)
    {
        if (tail_size == 0) {
            retained_xml_.clear();
            return;
        }
        if (xml.size() >= tail_size) {
            retained_xml_.assign(xml.substr(xml.size() - tail_size));
            return;
        }

        std::string retained_tail = retained_xml_;
        retained_tail.append(xml);
        if (retained_tail.size() > tail_size) {
            retained_tail.erase(0, retained_tail.size() - tail_size);
        }
        retained_xml_ = std::move(retained_tail);
    }

    void process_tag(std::string_view xml, const XmlTagRange& tag)
    {
        const char marker = xml[tag.open + 1];
        if (marker == '/') {
            if (namespace_stack_.empty()) {
                throw FastXlsxError("small XML part closing tag has no matching start tag");
            }
            namespace_stack_.pop_back();
            return;
        }
        if (marker == '?' || marker == '!') {
            return;
        }

        XmlNamespaceScope current_scope;
        ingest_namespace_declarations(current_scope, xml, tag);
        std::vector<XmlNamespaceScope> current_namespaces = namespace_stack_;
        current_namespaces.push_back(current_scope);

        const std::string_view local_name = local_xml_name(start_tag_name(xml, tag));
        if (is_worksheet_relationship_reference_element(local_name)) {
            std::size_t value_begin = 0;
            std::size_t value_end = 0;
            if (find_relationship_id_value(
                    xml, tag, current_namespaces, value_begin, value_end)) {
                add_reference(
                    local_name, xml.substr(value_begin, value_end - value_begin));
            }
        }

        if (!is_self_closing_tag(xml, tag)) {
            namespace_stack_.push_back(std::move(current_scope));
        }
    }

    void ensure_retained_xml_within_limit() const
    {
        if (retained_xml_.size() > package_editor_cell_replacement_event_window_byte_limit) {
            throw FastXlsxError(
                "worksheet relationship-id scanner exceeded bounded input window");
        }
    }

    void retain_xml_tail(std::string_view xml)
    {
        retained_xml_.assign(xml);
        ensure_retained_xml_within_limit();
    }

    void append_retained_xml(std::string_view xml)
    {
        if (retained_xml_.size() > package_editor_cell_replacement_event_window_byte_limit
            || xml.size() > package_editor_cell_replacement_event_window_byte_limit
            || xml.size()
                > package_editor_cell_replacement_event_window_byte_limit - retained_xml_.size()) {
            throw FastXlsxError(
                "worksheet relationship-id scanner exceeded bounded input window");
        }
        retained_xml_ += xml;
    }

    void add_reference(std::string_view element, std::string_view relationship_id)
    {
        const auto duplicate = std::find_if(references_.begin(), references_.end(),
            [element, relationship_id](const WorksheetRelationshipReference& item) {
                return item.element == element && item.relationship_id == relationship_id;
            });
        if (duplicate != references_.end()) {
            return;
        }

        references_.push_back(WorksheetRelationshipReference {
            std::string(element),
            std::string(relationship_id),
        });
    }

    std::string retained_xml_;
    IgnoredMarkupState ignored_markup_state_ = IgnoredMarkupState::None;
    std::vector<XmlNamespaceScope> namespace_stack_;
    std::vector<WorksheetRelationshipReference> references_;
};

void scan_xml_relationship_references(
    WorksheetRelationshipReferenceScanner& scanner, std::string_view xml)
{
    scanner.process(xml);
}

void finish_xml_relationship_references(
    WorksheetRelationshipReferenceScanner& scanner, bool& relationship_scan_failed)
{
    if (relationship_scan_failed) {
        return;
    }
    try {
        scanner.finish();
    } catch (const std::exception&) {
        relationship_scan_failed = true;
    }
}

WorksheetEventReaderOptions package_editor_cell_replacement_reader_options();

void scan_worksheet_relationship_references_from_chunk_source(
    WorksheetRelationshipReferenceScanner& scanner,
    const WorksheetInputChunkCallback& read_next_chunk)
{
    scan_worksheet_events_from_chunk_source(
        read_next_chunk,
        [&](const WorksheetEvent& event) {
            scan_xml_relationship_references(scanner, event.raw_xml);
        },
        package_editor_cell_replacement_reader_options());
    scanner.finish();
}

bool contains_relationship_reference(
    const std::vector<WorksheetRelationshipReference>& references, std::string_view relationship_id)
{
    return std::any_of(references.begin(), references.end(),
        [relationship_id](const WorksheetRelationshipReference& reference) {
            return reference.relationship_id == relationship_id;
        });
}

std::string_view expected_worksheet_relationship_type(std::string_view element) noexcept
{
    if (element == "hyperlink") {
        return relationship_type_hyperlink;
    }
    if (element == "drawing") {
        return relationship_type_drawing;
    }
    if (element == "legacyDrawing" || element == "legacyDrawingHF") {
        return relationship_type_vml_drawing;
    }
    if (element == "picture") {
        return relationship_type_image;
    }
    if (element == "pageSetup") {
        return relationship_type_printer_settings;
    }
    if (element == "oleObject") {
        return relationship_type_ole_object;
    }
    if (element == "control") {
        return relationship_type_control;
    }
    if (element == "tablePart") {
        return relationship_type_table;
    }
    return {};
}

void append_worksheet_relationship_reference_audit(
    WorksheetRelationshipReferenceAuditResult& result, const PartName& worksheet_part,
    WorksheetRelationshipReferenceAuditKind kind, std::string_view element,
    std::string_view relationship_id, std::string_view expected_relationship_type,
    std::string_view actual_relationship_type, std::string note)
{
    result.audits.push_back(WorksheetRelationshipReferenceAudit {
        worksheet_part,
        kind,
        std::string(element),
        std::string(relationship_id),
        std::string(expected_relationship_type),
        std::string(actual_relationship_type),
        note,
    });
    result.notes.push_back(std::move(note));
}

WorksheetRelationshipReferenceAuditResult worksheet_relationship_reference_parse_failure_audit()
{
    WorksheetRelationshipReferenceAuditResult result;
    result.notes.push_back(
        "worksheet replacement relationship-id audit could not parse relationship-bearing worksheet XML; caller must review worksheet .rels");
    return result;
}

WorksheetRelationshipReferenceAuditResult worksheet_relationship_reference_audit_from_references(
    const PartName& worksheet_part,
    const std::vector<WorksheetRelationshipReference>& references,
    const RelationshipSet* worksheet_relationships)
{
    if (references.empty() && worksheet_relationships == nullptr) {
        return {};
    }

    WorksheetRelationshipReferenceAuditResult result;
    for (const WorksheetRelationshipReference& reference : references) {
        const std::string_view expected_type =
            expected_worksheet_relationship_type(reference.element);
        if (worksheet_relationships == nullptr) {
            std::string note = "worksheet replacement references relationship id ";
            note += reference.relationship_id;
            note += " from <";
            note += reference.element;
            note += "> but source worksheet relationships are missing; caller must repair worksheet .rels";
            append_worksheet_relationship_reference_audit(result, worksheet_part,
                WorksheetRelationshipReferenceAuditKind::MissingRelationships,
                reference.element, reference.relationship_id, expected_type, {}, std::move(note));
            continue;
        }
        const Relationship* relationship =
            worksheet_relationships->find_by_id(reference.relationship_id);
        if (relationship == nullptr) {
            std::string note = "worksheet replacement references relationship id ";
            note += reference.relationship_id;
            note += " from <";
            note += reference.element;
            note += "> but preserved worksheet relationships do not contain that id; caller must repair worksheet .rels";
            append_worksheet_relationship_reference_audit(result, worksheet_part,
                WorksheetRelationshipReferenceAuditKind::MissingRelationshipId,
                reference.element, reference.relationship_id, expected_type, {}, std::move(note));
            continue;
        }

        if (!expected_type.empty() && std::string_view(relationship->type) != expected_type) {
            std::string note = "worksheet replacement references relationship id ";
            note += reference.relationship_id;
            note += " from <";
            note += reference.element;
            note += "> but preserved worksheet relationships use type ";
            note += relationship->type;
            note += "; expected ";
            note += expected_type;
            note += "; caller must review worksheet .rels";
            append_worksheet_relationship_reference_audit(result, worksheet_part,
                WorksheetRelationshipReferenceAuditKind::TypeMismatch, reference.element,
                reference.relationship_id, expected_type, relationship->type, std::move(note));
        }
    }

    if (worksheet_relationships == nullptr) {
        return result;
    }

    for (const Relationship& relationship : worksheet_relationships->relationships()) {
        if (contains_relationship_reference(references, relationship.id)) {
            continue;
        }

        std::string note = "worksheet replacement leaves preserved worksheet relationship id ";
        note += relationship.id;
        note += " unreferenced by replacement worksheet XML; caller should review stale linked-object relationships";
        append_worksheet_relationship_reference_audit(result, worksheet_part,
            WorksheetRelationshipReferenceAuditKind::UnreferencedRelationshipId, {},
            relationship.id, {}, relationship.type, std::move(note));
    }

    return result;
}

WorksheetRelationshipReferenceAuditResult worksheet_relationship_reference_audit_from_chunk_source(
    const PartName& worksheet_part,
    const WorksheetInputChunkCallback& read_next_chunk,
    const RelationshipSet* worksheet_relationships)
{
    try {
        WorksheetRelationshipReferenceScanner scanner;
        scan_worksheet_relationship_references_from_chunk_source(scanner, read_next_chunk);
        return worksheet_relationship_reference_audit_from_references(
            worksheet_part, scanner.references(), worksheet_relationships);
    } catch (const std::exception&) {
        return worksheet_relationship_reference_parse_failure_audit();
    }
}

void reject_relationship_reference_audit_by_policy(
    const WorksheetRelationshipReferenceAuditResult& audit, const ReferencePolicy& policy)
{
    if (policy.unsupported_linked_part_action != ReferencePolicyAction::Fail) {
        return;
    }
    if (audit.audits.empty() && audit.notes.empty()) {
        return;
    }

    throw FastXlsxError(
        "worksheet replacement relationship references blocked by reference policy");
}

void reject_payload_dependencies_by_policy(
    const WorksheetPayloadDependencyAuditResult& audit, const ReferencePolicy& policy,
    std::string_view operation)
{
    if (policy.unsupported_linked_part_action != ReferencePolicyAction::Fail) {
        return;
    }
    if (audit.audits.empty() && audit.notes.empty()) {
        return;
    }

    throw FastXlsxError(
        std::string(operation) + " payload dependencies blocked by reference policy");
}

void append_worksheet_payload_dependency_audit(
    WorksheetPayloadDependencyAuditResult& result, const PartName& worksheet_part,
    WorksheetPayloadDependencyAuditKind kind, WorksheetPayloadDependencyAuditScope scope,
    std::string_view element, std::string note)
{
    const auto existing = std::find_if(result.audits.begin(), result.audits.end(),
        [&worksheet_part, kind, scope, element](const WorksheetPayloadDependencyAudit& audit) {
            return audit.worksheet_part == worksheet_part && audit.kind == kind
                && audit.scope == scope && audit.element == element;
        });
    if (existing != result.audits.end()) {
        existing->note = std::move(note);
        return;
    }

    result.audits.push_back(WorksheetPayloadDependencyAudit {
        worksheet_part,
        kind,
        scope,
        std::string(element),
        note,
    });
    result.notes.push_back(std::move(note));
}

void reject_part_removal_inbound_relationships_by_policy(
    const EditPlan& removal_plan, const PartName& part_name, const ReferencePolicy& policy)
{
    if (policy.unsupported_linked_part_action != ReferencePolicyAction::Fail) {
        return;
    }

    const EditPlanRemovedPart* removed_part = removal_plan.find_removed_part(part_name);
    if (removed_part == nullptr || removed_part->inbound_relationships.empty()) {
        return;
    }

    throw FastXlsxError("part removal inbound relationships blocked by reference policy");
}

bool is_relationship_metadata_element(std::string_view element) noexcept
{
    return element == "hyperlinks"
        || element == "drawing"
        || element == "legacyDrawing"
        || element == "picture"
        || element == "legacyDrawingHF"
        || element == "oleObjects"
        || element == "controls"
        || element == "tableParts";
}

bool cell_start_attribute_equals_for_audit(std::string_view cell_xml,
    std::string_view attribute_name, std::string_view expected_value) noexcept
{
    try {
        if (cell_xml.size() < 2 || cell_xml.front() != '<' || cell_xml.back() != '>') {
            return false;
        }
        const XmlTagRange cell {0, cell_xml.size() - 1};
        std::size_t value_begin = 0;
        std::size_t value_end = 0;
        return find_attribute_value(cell_xml, cell, attribute_name, value_begin, value_end)
            && cell_xml.substr(value_begin, value_end - value_begin) == expected_value;
    } catch (const std::exception&) {
        return false;
    }
}

bool cell_start_has_attribute_for_audit(
    std::string_view cell_xml, std::string_view attribute_name) noexcept
{
    try {
        if (cell_xml.size() < 2 || cell_xml.front() != '<' || cell_xml.back() != '>') {
            return false;
        }
        const XmlTagRange cell {0, cell_xml.size() - 1};
        std::size_t value_begin = 0;
        std::size_t value_end = 0;
        return find_attribute_value(cell_xml, cell, attribute_name, value_begin, value_end);
    } catch (const std::exception&) {
        return false;
    }
}

class WorksheetSheetDataPreservationAuditCollector {
public:
    explicit WorksheetSheetDataPreservationAuditCollector(const PartName& worksheet_part)
        : worksheet_part_(worksheet_part)
    {
    }

    void process_event(const WorksheetEvent& event)
    {
        if (event.kind != WorksheetEventKind::Metadata) {
            return;
        }

        for (std::size_t index = 0; index < audit_count; ++index) {
            const WorksheetLocalMetadataAudit& audit = audits[index];
            if (seen_[index] || event.element_name != audit.element) {
                continue;
            }
            seen_[index] = true;

            std::string note = "sheetData replacement preserved ";
            note += audit.description;
            note += "; ranges or references may require caller review";
            append_worksheet_payload_dependency_audit(result_, worksheet_part_,
                is_relationship_metadata_element(audit.element)
                    ? WorksheetPayloadDependencyAuditKind::RelationshipMetadata
                    : WorksheetPayloadDependencyAuditKind::RangeMetadata,
                WorksheetPayloadDependencyAuditScope::PreservedWorksheetMetadata,
                audit.element, std::move(note));
        }
    }

    WorksheetPayloadDependencyAuditResult finish() &&
    {
        return std::move(result_);
    }

private:
    static constexpr WorksheetLocalMetadataAudit audits[] = {
        {"sheetPr", "worksheet sheet property metadata"},
        {"sheetCalcPr", "worksheet sheet calculation metadata"},
        {"dimension", "worksheet dimension metadata"},
        {"sheetViews", "worksheet view metadata"},
        {"customSheetViews", "worksheet custom view metadata"},
        {"sheetFormatPr", "worksheet default format metadata"},
        {"cols", "worksheet column metadata"},
        {"sheetProtection", "worksheet protection metadata"},
        {"protectedRanges", "worksheet protected-range metadata"},
        {"sortState", "worksheet sort-state metadata"},
        {"autoFilter", "worksheet autoFilter metadata"},
        {"mergeCells", "worksheet merged-cell metadata"},
        {"scenarios", "worksheet scenario metadata"},
        {"dataConsolidate", "worksheet data consolidation metadata"},
        {"customProperties", "worksheet custom property metadata"},
        {"cellWatches", "worksheet cell watch metadata"},
        {"smartTags", "worksheet smart tag metadata"},
        {"webPublishItems", "worksheet web publishing metadata"},
        {"hyperlinks", "worksheet hyperlink metadata"},
        {"dataValidations", "worksheet data validation metadata"},
        {"conditionalFormatting", "worksheet conditional formatting metadata"},
        {"ignoredErrors", "worksheet ignored-error metadata"},
        {"printOptions", "worksheet print options metadata"},
        {"pageMargins", "worksheet page margins metadata"},
        {"pageSetup", "worksheet page setup metadata"},
        {"headerFooter", "worksheet header/footer metadata"},
        {"rowBreaks", "worksheet row break metadata"},
        {"colBreaks", "worksheet column break metadata"},
        {"phoneticPr", "worksheet phonetic metadata"},
        {"drawing", "worksheet drawing reference metadata"},
        {"legacyDrawing", "worksheet legacy drawing reference metadata"},
        {"picture", "worksheet background picture reference metadata"},
        {"legacyDrawingHF", "worksheet header/footer drawing reference metadata"},
        {"oleObjects", "worksheet OLE object reference metadata"},
        {"controls", "worksheet control reference metadata"},
        {"tableParts", "worksheet table reference metadata"},
        {"extLst", "worksheet extension metadata"},
    };
    static constexpr std::size_t audit_count = sizeof(audits) / sizeof(audits[0]);

    const PartName& worksheet_part_;
    WorksheetPayloadDependencyAuditResult result_;
    std::array<bool, audit_count> seen_ {};
};

WorksheetPayloadDependencyAuditResult worksheet_sheet_data_replacement_audit_from_chunk_source(
    const PartName& worksheet_part,
    const WorksheetInputChunkCallback& read_next_chunk)
{
    WorksheetPayloadDependencyAuditResult result;
    bool saw_shared_string_index = false;
    bool saw_style_reference = false;
    bool saw_formula = false;

    scan_sheet_data_start_tags_from_chunk_source(
        read_next_chunk,
        [&](std::string_view tag_xml, const XmlTagRange&) {
            const XmlTagRange tag {0, tag_xml.size() - 1};
            const std::string element_name(
                local_xml_name(std::string(start_tag_name(tag_xml, tag))));
            if (element_name == "c") {
                if (!saw_shared_string_index
                    && cell_start_attribute_equals_for_audit(tag_xml, "t", "s")) {
                    saw_shared_string_index = true;
                }
                if (!saw_style_reference && cell_start_has_attribute_for_audit(tag_xml, "s")) {
                    saw_style_reference = true;
                }
            } else if (element_name == "f") {
                saw_formula = true;
            }
        },
        package_editor_cell_replacement_reader_options());

    if (saw_shared_string_index) {
        append_worksheet_payload_dependency_audit(result, worksheet_part,
            WorksheetPayloadDependencyAuditKind::SharedStrings,
            WorksheetPayloadDependencyAuditScope::SheetDataReplacement, "c",
            "sheetData replacement uses shared string indexes; caller must ensure xl/sharedStrings.xml remains valid");
    }
    if (saw_style_reference) {
        append_worksheet_payload_dependency_audit(result, worksheet_part,
            WorksheetPayloadDependencyAuditKind::Styles,
            WorksheetPayloadDependencyAuditScope::SheetDataReplacement, "c",
            "sheetData replacement uses style id references; caller must ensure xl/styles.xml remains valid");
    }
    if (saw_formula) {
        append_worksheet_payload_dependency_audit(result, worksheet_part,
            WorksheetPayloadDependencyAuditKind::Formula,
            WorksheetPayloadDependencyAuditScope::SheetDataReplacement, "f",
            "sheetData replacement contains formulas; workbook calc metadata and calcChain policy require caller review");
    }
    return result;
}

void append_worksheet_replacement_shared_strings_audit(
    WorksheetPayloadDependencyAuditResult& result, const PartName& worksheet_part)
{
    append_worksheet_payload_dependency_audit(result, worksheet_part,
        WorksheetPayloadDependencyAuditKind::SharedStrings,
        WorksheetPayloadDependencyAuditScope::WorksheetReplacement, "c",
        "worksheet replacement uses shared string indexes; caller must ensure xl/sharedStrings.xml remains valid");
}

void append_worksheet_replacement_styles_audit(
    WorksheetPayloadDependencyAuditResult& result, const PartName& worksheet_part)
{
    append_worksheet_payload_dependency_audit(result, worksheet_part,
        WorksheetPayloadDependencyAuditKind::Styles,
        WorksheetPayloadDependencyAuditScope::WorksheetReplacement, "c",
        "worksheet replacement uses style id references; caller must ensure xl/styles.xml remains valid");
}

void append_worksheet_replacement_formula_audit(
    WorksheetPayloadDependencyAuditResult& result, const PartName& worksheet_part)
{
    append_worksheet_payload_dependency_audit(result, worksheet_part,
        WorksheetPayloadDependencyAuditKind::Formula,
        WorksheetPayloadDependencyAuditScope::WorksheetReplacement, "f",
        "worksheet replacement contains formulas; workbook calc metadata and calcChain policy require caller review");
}

void append_worksheet_replacement_range_metadata_audit(WorksheetPayloadDependencyAuditResult& result,
    const PartName& worksheet_part, std::string_view element, std::string_view description)
{
    std::string note = "worksheet replacement contains ";
    note += description;
    note += "; ranges or references require caller review";
    append_worksheet_payload_dependency_audit(result, worksheet_part,
        WorksheetPayloadDependencyAuditKind::RangeMetadata,
        WorksheetPayloadDependencyAuditScope::WorksheetReplacement, element, std::move(note));
}

void append_worksheet_replacement_relationship_metadata_audit(
    WorksheetPayloadDependencyAuditResult& result, const PartName& worksheet_part,
    std::string_view element, std::string_view description)
{
    std::string note = "worksheet replacement contains ";
    note += description;
    note += "; worksheet relationships and linked parts require caller review";
    append_worksheet_payload_dependency_audit(result, worksheet_part,
        WorksheetPayloadDependencyAuditKind::RelationshipMetadata,
        WorksheetPayloadDependencyAuditScope::WorksheetReplacement, element, std::move(note));
}

void append_payload_audit_result(
    WorksheetPayloadDependencyAuditResult& destination,
    WorksheetPayloadDependencyAuditResult source)
{
    for (WorksheetPayloadDependencyAudit& audit : source.audits) {
        append_worksheet_payload_dependency_audit(destination, audit.worksheet_part,
            audit.kind, audit.scope, audit.element, std::move(audit.note));
    }
}

static constexpr WorksheetLocalMetadataAudit worksheet_replacement_range_metadata_audits[] = {
    {"sheetPr", "worksheet sheet property metadata"},
    {"sheetCalcPr", "worksheet sheet calculation metadata"},
    {"dimension", "worksheet dimension metadata"},
    {"sheetViews", "worksheet view metadata"},
    {"customSheetViews", "worksheet custom view metadata"},
    {"sheetFormatPr", "worksheet default format metadata"},
    {"cols", "worksheet column metadata"},
    {"sheetProtection", "worksheet protection metadata"},
    {"protectedRanges", "worksheet protected-range metadata"},
    {"sortState", "worksheet sort-state metadata"},
    {"autoFilter", "worksheet autoFilter metadata"},
    {"mergeCells", "worksheet merged-cell metadata"},
    {"scenarios", "worksheet scenario metadata"},
    {"dataConsolidate", "worksheet data consolidation metadata"},
    {"customProperties", "worksheet custom property metadata"},
    {"cellWatches", "worksheet cell watch metadata"},
    {"smartTags", "worksheet smart tag metadata"},
    {"webPublishItems", "worksheet web publishing metadata"},
    {"dataValidations", "worksheet data validation metadata"},
    {"conditionalFormatting", "worksheet conditional formatting metadata"},
    {"ignoredErrors", "worksheet ignored-error metadata"},
    {"printOptions", "worksheet print options metadata"},
    {"pageMargins", "worksheet page margins metadata"},
    {"pageSetup", "worksheet page setup metadata"},
    {"headerFooter", "worksheet header/footer metadata"},
    {"rowBreaks", "worksheet row break metadata"},
    {"colBreaks", "worksheet column break metadata"},
    {"phoneticPr", "worksheet phonetic metadata"},
    {"extLst", "worksheet extension metadata"},
};

static constexpr WorksheetLocalMetadataAudit worksheet_replacement_relationship_metadata_audits[] = {
    {"hyperlinks", "worksheet hyperlink metadata"},
    {"drawing", "worksheet drawing relationship metadata"},
    {"legacyDrawing", "worksheet legacy drawing relationship metadata"},
    {"picture", "worksheet background picture relationship metadata"},
    {"legacyDrawingHF", "worksheet header/footer drawing relationship metadata"},
    {"oleObjects", "worksheet OLE object relationship metadata"},
    {"controls", "worksheet control relationship metadata"},
    {"tableParts", "worksheet table relationship metadata"},
};

void scan_replacement_cell_payload_start_tags(
    const WorksheetCellReplacementPayload& payload,
    const std::function<void(std::string_view, const XmlTagRange&)>& on_start_tag);

WorksheetPayloadDependencyAuditResult worksheet_replacement_cell_payload_audit(
    const PartName& worksheet_part,
    const WorksheetCellReplacementPayload& replacement_payload)
{
    WorksheetPayloadDependencyAuditResult result;
    try {
        scan_replacement_cell_payload_start_tags(
            replacement_payload, [&](std::string_view tag_xml, const XmlTagRange& tag) {
                const std::string_view element =
                    local_xml_name(start_tag_name(tag_xml, tag));
                if (element == "c") {
                    if (cell_start_attribute_equals_for_audit(tag_xml, "t", "s")) {
                        append_worksheet_replacement_shared_strings_audit(
                            result, worksheet_part);
                    }
                    if (cell_start_has_attribute_for_audit(tag_xml, "s")) {
                        append_worksheet_replacement_styles_audit(result, worksheet_part);
                    }
                } else if (element == "f") {
                    append_worksheet_replacement_formula_audit(result, worksheet_part);
                }
            }
        );
    } catch (const FastXlsxError& error) {
        throw FastXlsxError(
            std::string("worksheet replacement cell XML payload scanner failed: ")
            + error.what());
    }

    return result;
}

WorksheetPayloadDependencyAuditResult worksheet_replacement_cell_payloads_audit(
    const PartName& worksheet_part,
    const WorksheetCellReplacementPlan& replacement_plan)
{
    WorksheetPayloadDependencyAuditResult result;
    for (const auto& replacement : replacement_plan.replacement_payloads_by_reference) {
        append_payload_audit_result(result,
            worksheet_replacement_cell_payload_audit(
                worksheet_part, replacement.second));
    }
    append_worksheet_replacement_range_metadata_audit(result, worksheet_part,
        "dimension", "worksheet dimension metadata");
    return result;
}

WorksheetRelationshipReferenceAuditResult
worksheet_replacement_cell_payloads_relationship_reference_audit(
    const PartName& worksheet_part,
    const WorksheetCellReplacementPlan& replacement_plan,
    const RelationshipSet* worksheet_relationships)
{
    WorksheetRelationshipReferenceScanner scanner;
    bool relationship_scan_failed = false;
    for (const auto& replacement : replacement_plan.replacement_payloads_by_reference) {
        replacement.second.for_each_chunk([&](std::string_view chunk) {
            if (relationship_scan_failed) {
                return;
            }
            try {
                scan_xml_relationship_references(scanner, chunk);
            } catch (const std::exception&) {
                relationship_scan_failed = true;
            }
        });
    }
    finish_xml_relationship_references(scanner, relationship_scan_failed);
    return relationship_scan_failed
        ? worksheet_relationship_reference_parse_failure_audit()
        : worksheet_relationship_reference_audit_from_references(
            worksheet_part, scanner.references(), worksheet_relationships);
}

class WorksheetReplacementPayloadAuditCollector {
public:
    explicit WorksheetReplacementPayloadAuditCollector(const PartName& worksheet_part)
        : worksheet_part_(worksheet_part)
    {
    }

    void process_start_tag(std::string_view tag_xml, const XmlTagRange& tag)
    {
        const std::string_view element = local_xml_name(start_tag_name(tag_xml, tag));
        if (element == "c") {
            std::size_t value_begin = 0;
            std::size_t value_end = 0;
            if (!saw_shared_string_index_
                && find_attribute_value(tag_xml, tag, "t", value_begin, value_end)
                && tag_xml.substr(value_begin, value_end - value_begin) == "s") {
                append_worksheet_replacement_shared_strings_audit(result_, worksheet_part_);
                saw_shared_string_index_ = true;
            }
            if (!saw_style_reference_
                && find_attribute_value(tag_xml, tag, "s", value_begin, value_end)) {
                append_worksheet_replacement_styles_audit(result_, worksheet_part_);
                saw_style_reference_ = true;
            }
        }
        if (!saw_formula_ && element == "f") {
            append_worksheet_replacement_formula_audit(result_, worksheet_part_);
            saw_formula_ = true;
        }

        for (std::size_t index = 0; index < range_audit_count; ++index) {
            const WorksheetLocalMetadataAudit& audit =
                worksheet_replacement_range_metadata_audits[index];
            if (saw_range_metadata_[index] || element != audit.element) {
                continue;
            }
            append_worksheet_replacement_range_metadata_audit(
                result_, worksheet_part_, audit.element, audit.description);
            saw_range_metadata_[index] = true;
        }

        for (std::size_t index = 0; index < relationship_audit_count; ++index) {
            const WorksheetLocalMetadataAudit& audit =
                worksheet_replacement_relationship_metadata_audits[index];
            if (saw_relationship_metadata_[index] || element != audit.element) {
                continue;
            }
            append_worksheet_replacement_relationship_metadata_audit(
                result_, worksheet_part_, audit.element, audit.description);
            saw_relationship_metadata_[index] = true;
        }
    }

    WorksheetPayloadDependencyAuditResult finish() &&
    {
        return std::move(result_);
    }

private:
    static constexpr std::size_t range_audit_count =
        sizeof(worksheet_replacement_range_metadata_audits)
        / sizeof(worksheet_replacement_range_metadata_audits[0]);
    static constexpr std::size_t relationship_audit_count =
        sizeof(worksheet_replacement_relationship_metadata_audits)
        / sizeof(worksheet_replacement_relationship_metadata_audits[0]);

    const PartName& worksheet_part_;
    WorksheetPayloadDependencyAuditResult result_;
    bool saw_shared_string_index_ = false;
    bool saw_style_reference_ = false;
    bool saw_formula_ = false;
    std::array<bool, range_audit_count> saw_range_metadata_ {};
    std::array<bool, relationship_audit_count> saw_relationship_metadata_ {};
};

bool raw_xml_is_start_tag_for_audit(std::string_view raw_xml) noexcept
{
    return raw_xml.size() >= 2 && raw_xml.front() == '<' && raw_xml.back() == '>'
        && raw_xml[1] != '/' && raw_xml[1] != '?' && raw_xml[1] != '!';
}

void require_replacement_cell_payload_scanner_capacity(
    std::size_t buffered_size,
    std::size_t incoming_size)
{
    if (buffered_size > package_editor_cell_replacement_event_window_byte_limit
        || incoming_size > package_editor_cell_replacement_event_window_byte_limit
        || incoming_size
            > package_editor_cell_replacement_event_window_byte_limit - buffered_size) {
        throw FastXlsxError(
            "worksheet replacement cell payload scanner exceeded bounded input window");
    }
}

void scan_replacement_cell_payload_start_tags(
    const WorksheetCellReplacementPayload& payload,
    const std::function<void(std::string_view, const XmlTagRange&)>& on_start_tag)
{
    std::string buffer;
    bool inside_comment = false;
    bool inside_cdata = false;
    bool inside_processing_instruction = false;

    auto process_buffer = [&](bool final_chunk) {
        for (;;) {
            if (inside_comment) {
                const std::size_t comment_close = buffer.find("-->");
                if (comment_close == std::string::npos) {
                    if (final_chunk) {
                        throw FastXlsxError(
                            "worksheet replacement cell payload comment is not closed");
                    }
                    if (buffer.size() > 2) {
                        buffer.erase(0, buffer.size() - 2);
                    }
                    return;
                }
                buffer.erase(0, comment_close + 3);
                inside_comment = false;
                continue;
            }
            if (inside_cdata) {
                const std::size_t cdata_close = buffer.find(xml_cdata_close);
                if (cdata_close == std::string::npos) {
                    if (final_chunk) {
                        throw FastXlsxError(
                            "worksheet replacement cell payload CDATA is not closed");
                    }
                    if (buffer.size() > xml_cdata_close.size() - 1U) {
                        buffer.erase(0, buffer.size() - (xml_cdata_close.size() - 1U));
                    }
                    return;
                }
                buffer.erase(0, cdata_close + xml_cdata_close.size());
                inside_cdata = false;
                continue;
            }
            if (inside_processing_instruction) {
                const std::size_t processing_instruction_close =
                    buffer.find(xml_processing_instruction_close);
                if (processing_instruction_close == std::string::npos) {
                    if (final_chunk) {
                        throw FastXlsxError(
                            "worksheet replacement cell payload processing instruction is not closed");
                    }
                    if (buffer.size() > xml_processing_instruction_close.size() - 1U) {
                        buffer.erase(0,
                            buffer.size() - (xml_processing_instruction_close.size() - 1U));
                    }
                    return;
                }
                buffer.erase(
                    0, processing_instruction_close + xml_processing_instruction_close.size());
                inside_processing_instruction = false;
                continue;
            }

            const std::size_t open = buffer.find('<');
            if (open == std::string::npos) {
                buffer.clear();
                return;
            }
            if (open > 0) {
                buffer.erase(0, open);
            }

            if (buffer.substr(0, 4) == "<!--") {
                const std::size_t comment_close = buffer.find("-->", 4);
                if (comment_close == std::string::npos) {
                    if (final_chunk) {
                        throw FastXlsxError(
                            "worksheet replacement cell payload comment is not closed");
                    }
                    buffer.erase(0, 4);
                    if (buffer.size() > 2) {
                        buffer.erase(0, buffer.size() - 2);
                    }
                    inside_comment = true;
                    return;
                }
                buffer.erase(0, comment_close + 3);
                continue;
            }
            if (buffer.substr(0, xml_cdata_open.size()) == xml_cdata_open) {
                const std::size_t cdata_close =
                    buffer.find(xml_cdata_close, xml_cdata_open.size());
                if (cdata_close == std::string::npos) {
                    if (final_chunk) {
                        throw FastXlsxError(
                            "worksheet replacement cell payload CDATA is not closed");
                    }
                    buffer.erase(0, xml_cdata_open.size());
                    if (buffer.size() > xml_cdata_close.size() - 1U) {
                        buffer.erase(0, buffer.size() - (xml_cdata_close.size() - 1U));
                    }
                    inside_cdata = true;
                    return;
                }
                buffer.erase(0, cdata_close + xml_cdata_close.size());
                continue;
            }
            if (buffer.substr(0, xml_processing_instruction_open.size())
                == xml_processing_instruction_open) {
                const std::size_t processing_instruction_close =
                    buffer.find(
                        xml_processing_instruction_close,
                        xml_processing_instruction_open.size());
                if (processing_instruction_close == std::string::npos) {
                    if (final_chunk) {
                        throw FastXlsxError(
                            "worksheet replacement cell payload processing instruction is not closed");
                    }
                    buffer.erase(0, xml_processing_instruction_open.size());
                    if (buffer.size() > xml_processing_instruction_close.size() - 1U) {
                        buffer.erase(0,
                            buffer.size() - (xml_processing_instruction_close.size() - 1U));
                    }
                    inside_processing_instruction = true;
                    return;
                }
                buffer.erase(
                    0, processing_instruction_close + xml_processing_instruction_close.size());
                continue;
            }

            const std::size_t close = find_streaming_xml_tag_end(buffer, 0);
            if (close == std::string_view::npos) {
                if (final_chunk) {
                    throw FastXlsxError(
                        "worksheet replacement cell payload tag is truncated");
                }
                return;
            }

            const std::string_view tag_xml(buffer.data(), close + 1);
            const char marker = tag_xml.size() > 1 ? tag_xml[1] : '\0';
            if (marker != '/' && marker != '?' && marker != '!') {
                const XmlTagRange tag {0, tag_xml.size() - 1};
                on_start_tag(tag_xml, tag);
            }
            buffer.erase(0, close + 1);
        }
    };

    payload.for_each_chunk([&](std::string_view chunk) {
        while (!chunk.empty()) {
            if (buffer.size() >= package_editor_cell_replacement_event_window_byte_limit) {
                require_replacement_cell_payload_scanner_capacity(buffer.size(), 1);
            }
            const std::size_t append_size = std::min(chunk.size(),
                package_editor_cell_replacement_event_window_byte_limit - buffer.size());
            buffer.append(chunk.substr(0, append_size));
            chunk.remove_prefix(append_size);
            require_replacement_cell_payload_scanner_capacity(buffer.size(), 0);
            process_buffer(false);
        }
    });
    process_buffer(true);
}

struct WorksheetReplacementChunkAuditResult {
    WorksheetPayloadDependencyAuditResult payload_audit;
    WorksheetRelationshipReferenceAuditResult relationship_reference_audit;
};

WorksheetReplacementChunkAuditResult worksheet_replacement_audits_from_chunk_source(
    const PartName& worksheet_part,
    const WorksheetInputChunkCallback& read_next_chunk,
    const RelationshipSet* worksheet_relationships)
{
    WorksheetReplacementPayloadAuditCollector payload_collector(worksheet_part);
    WorksheetRelationshipReferenceScanner relationship_scanner;
    bool relationship_scan_failed = false;

    scan_worksheet_events_from_chunk_source(
        read_next_chunk,
        [&](const WorksheetEvent& event) {
            if (raw_xml_is_start_tag_for_audit(event.raw_xml)) {
                const XmlTagRange tag {0, event.raw_xml.size() - 1};
                payload_collector.process_start_tag(event.raw_xml, tag);
            }
            if (!relationship_scan_failed) {
                try {
                    scan_xml_relationship_references(relationship_scanner, event.raw_xml);
                } catch (const std::exception&) {
                    relationship_scan_failed = true;
                }
            }
        },
        package_editor_cell_replacement_reader_options());
    finish_xml_relationship_references(relationship_scanner, relationship_scan_failed);

    WorksheetReplacementChunkAuditResult result;
    result.payload_audit = std::move(payload_collector).finish();
    result.relationship_reference_audit =
        relationship_scan_failed
        ? worksheet_relationship_reference_parse_failure_audit()
        : worksheet_relationship_reference_audit_from_references(
            worksheet_part, relationship_scanner.references(), worksheet_relationships);
    return result;
}

std::string relationship_entry_name_for_source_part(const PartName& source_part)
{
    const std::string source_path = source_part.zip_path();
    const std::size_t slash = source_path.find_last_of('/');
    if (slash == std::string::npos) {
        return "_rels/" + source_path + ".rels";
    }

    return source_path.substr(0, slash) + "/_rels/"
        + source_path.substr(slash + 1) + ".rels";
}

// Reverse of relationship_entry_name_for_source_part(): given a `.rels` entry
// name, return the owning source part value (such as `/xl/drawings/drawing1.xml`)
// when the entry is a source-owned owner `.rels` whose part is present in the
// package. Returns an empty string for non-`.rels` entries, the package
// `_rels/.rels`, or owner parts that are not registered source parts. This is a
// read-only audit-visibility lookup; it does not repair, prune, or invent
// relationships.
std::string source_part_for_relationship_entry(
    const PackageReader& reader, std::string_view entry_name)
{
    if (entry_name == package_relationships_entry_name) {
        return {};
    }
    if (entry_name.size() <= relationship_part_extension.size()
        || entry_name.substr(entry_name.size() - relationship_part_extension.size())
            != relationship_part_extension) {
        return {};
    }

    std::string owner_path;
    if (const std::size_t segment = entry_name.find(relationship_part_segment);
        segment != std::string_view::npos) {
        const std::string_view directory = entry_name.substr(0, segment);
        const std::string_view file = entry_name.substr(segment + relationship_part_segment.size());
        owner_path.assign(directory);
        owner_path.push_back('/');
        owner_path.append(file.substr(0, file.size() - relationship_part_extension.size()));
    } else if (entry_name.rfind(root_relationships_prefix, 0) == 0) {
        const std::string_view file = entry_name.substr(root_relationships_prefix.size());
        owner_path.assign(file.substr(0, file.size() - relationship_part_extension.size()));
    } else {
        return {};
    }

    if (owner_path.empty()) {
        return {};
    }

    try {
        const PartName owner_part("/" + owner_path);
        if (reader.part_index().find_part(owner_part) == nullptr) {
            return {};
        }
        return owner_part.value();
    } catch (const FastXlsxError&) {
        return {};
    }
}

std::string default_replacement_reason(PartWriteMode write_mode)
{
    switch (write_mode) {
    case PartWriteMode::GenerateSmallXml:
        return "replacement small XML part data supplied";
    case PartWriteMode::StreamRewrite:
        return "replacement stream-rewritten part data supplied";
    case PartWriteMode::LocalDomRewrite:
        return "replacement local-DOM-rewritten part data supplied";
    case PartWriteMode::CopyOriginal:
        break;
    }
    return {};
}

bool is_metadata_part_replacement_target(const PartName& part_name) noexcept
{
    const std::string zip_path = part_name.zip_path();
    if (zip_path == content_types_entry_name || zip_path == package_relationships_entry_name) {
        return true;
    }
    if (zip_path.size() <= relationship_part_extension.size()
        || zip_path.substr(zip_path.size() - relationship_part_extension.size())
            != relationship_part_extension) {
        return false;
    }
    return zip_path.find(relationship_part_segment) != std::string::npos
        || zip_path.rfind(root_relationships_prefix, 0) == 0;
}

bool is_core_or_extended_properties_entry_name(std::string_view entry_name) noexcept
{
    return entry_name == "docProps/core.xml" || entry_name == "docProps/app.xml";
}

bool is_core_or_extended_properties_part_name(const PartName& part_name) noexcept
{
    return is_core_or_extended_properties_entry_name(part_name.zip_path());
}

bool is_metadata_package_entry_name(std::string_view entry_name) noexcept
{
    if (entry_name == content_types_entry_name || entry_name == package_relationships_entry_name) {
        return true;
    }
    if (entry_name.size() <= relationship_part_extension.size()
        || entry_name.substr(entry_name.size() - relationship_part_extension.size())
            != relationship_part_extension) {
        return false;
    }
    return entry_name.find(relationship_part_segment) != std::string_view::npos
        || entry_name.rfind(root_relationships_prefix, 0) == 0;
}

std::string next_relationship_id(const RelationshipSet& relationships)
{
    for (std::size_t index = 1;; ++index) {
        std::string id = "rId" + std::to_string(index);
        if (relationships.find_by_id(id) == nullptr) {
            return id;
        }
    }
}

bool ensure_package_relationship(
    PackageManifest& manifest, std::string_view type, std::string_view target)
{
    for (const Relationship& relationship : manifest.package_relationships().relationships()) {
        if (relationship.type != type) {
            continue;
        }
        if (relationship.target == target
            && relationship.target_mode == Relationship::TargetMode::Internal) {
            return false;
        }
        throw FastXlsxError("document properties package relationship target conflicts");
    }

    manifest.add_package_relationship(Relationship {
        next_relationship_id(manifest.package_relationships()),
        std::string(type),
        std::string(target),
    });
    return true;
}

bool ensure_generated_part(
    PackageManifest& manifest, const PartName& part_name, std::string_view content_type)
{
    const bool missing_part = manifest.find_part(part_name) == nullptr;
    const bool missing_override = manifest.content_types().override_for(part_name) == nullptr;

    manifest.ensure_part(part_name, std::string(content_type)).mark_generated();
    return missing_part || missing_override;
}

PackagePartReplacement* find_replacement(
    std::vector<PackagePartReplacement>& replacements, const PartName& part_name) noexcept
{
    const auto item = std::find_if(replacements.begin(), replacements.end(),
        [&part_name](const PackagePartReplacement& replacement) {
            return replacement.part_name == part_name;
        });
    return item == replacements.end() ? nullptr : &*item;
}

const PackagePartReplacement* find_replacement(
    const std::vector<PackagePartReplacement>& replacements, std::string_view entry_name)
{
    const auto item = std::find_if(replacements.begin(), replacements.end(),
        [entry_name](const PackagePartReplacement& replacement) {
            return replacement.part_name.zip_path() == entry_name;
        });
    return item == replacements.end() ? nullptr : &*item;
}

const PackagePart* source_part_for_entry_name(
    const PackageReader& reader, std::string_view entry_name);

void require_materialized_package_entry_replacement_allowed(
    const PackageReader& reader, std::string_view entry_name);

void require_materialized_workbook_xml_size(std::uint64_t byte_size,
    std::string_view purpose)
{
    if (byte_size
        <= static_cast<std::uint64_t>(
            package_editor_workbook_xml_materialization_byte_limit)) {
        return;
    }

    throw FastXlsxError(
        "materialized workbook XML exceeds small XML limit for "
        + std::string(purpose)
        + "; workbook catalog/calc helpers are bounded small-XML paths, "
          "not arbitrary package-part materialization");
}

void require_materialized_metadata_xml_size(std::uint64_t byte_size,
    std::string_view purpose)
{
    if (byte_size
        <= static_cast<std::uint64_t>(
            package_editor_metadata_xml_materialization_byte_limit)) {
        return;
    }

    throw FastXlsxError(
        "materialized metadata XML exceeds small XML limit for "
        + std::string(purpose)
        + "; metadata helpers are bounded small-XML paths, not arbitrary "
          "package-entry materialization");
}

void require_materialized_part_replacement_payload_size(
    const PartName& part_name, std::uint64_t byte_size, PartWriteMode write_mode,
    std::string_view purpose, const PartName* workbook_part = nullptr)
{
    if ((workbook_part != nullptr && part_name == *workbook_part)
        || (workbook_part == nullptr && part_name.value() == "/xl/workbook.xml")) {
        require_materialized_workbook_xml_size(byte_size, purpose);
        return;
    }
    if (is_core_or_extended_properties_part_name(part_name)
        || write_mode == PartWriteMode::GenerateSmallXml) {
        require_materialized_metadata_xml_size(byte_size, purpose);
    }
}

void require_materialized_package_entry_replacement_payload_size(
    const PackageReader& reader, std::string_view entry_name,
    std::uint64_t byte_size, std::string_view purpose)
{
    (void)reader;
    if (is_metadata_package_entry_name(entry_name)) {
        require_materialized_metadata_xml_size(byte_size, purpose);
        return;
    }

    throw FastXlsxError(
        "materialized package-entry replacement is only allowed for OPC metadata entries; "
        "package parts must use package-part replacement or staged chunks");
}

void remove_part_replacement(
    std::vector<PackagePartReplacement>& replacements, const PartName& part_name)
{
    replacements.erase(
        std::remove_if(replacements.begin(), replacements.end(),
            [&part_name](const PackagePartReplacement& replacement) {
                return replacement.part_name == part_name;
            }),
        replacements.end());
}

void clear_indexed_source_entry_direct_range_stats(PackagePartReplacement& replacement) noexcept
{
    replacement.indexed_source_entry_direct_range = false;
    replacement.indexed_source_entry_scanned_source_cell_count = 0;
    replacement.indexed_source_entry_matched_replacement_count = 0;
    replacement.indexed_source_entry_staged_output_bytes = 0;
    replacement.indexed_source_entry_source_range_chunk_ms = 0;
    replacement.indexed_source_entry_target_plan_ms = 0;
    replacement.indexed_source_entry_payload_audit_ms = 0;
    replacement.indexed_source_entry_relationship_audit_ms = 0;
    replacement.indexed_source_entry_descriptor_ms = 0;
    replacement.indexed_source_entry_commit_ms = 0;
    replacement.single_pass_worksheet_transform = false;
    replacement.single_pass_scanned_source_cell_count = 0;
    replacement.single_pass_matched_replacement_count = 0;
    replacement.single_pass_inserted_cell_count = 0;
    replacement.single_pass_staged_output_bytes = 0;
    replacement.single_pass_transform_ms = 0;
    replacement.single_pass_commit_ms = 0;
}

void upsert_part_replacement(std::vector<PackagePartReplacement>& replacements,
    PartName part_name, std::string materialized_small_xml, PartWriteMode write_mode,
    std::string reason, const PartName* workbook_part = nullptr)
{
    require_materialized_part_replacement_payload_size(part_name,
        materialized_small_xml.size(), write_mode, "package-part replacement", workbook_part);

    if (auto* replacement = find_replacement(replacements, part_name)) {
        replacement->materialized_small_xml = std::move(materialized_small_xml);
        replacement->chunks.clear();
        replacement->write_mode = write_mode;
        replacement->reason = std::move(reason);
        clear_indexed_source_entry_direct_range_stats(*replacement);
        return;
    }

    replacements.push_back(PackagePartReplacement {
        std::move(part_name),
        std::move(materialized_small_xml),
        {},
        write_mode,
        std::move(reason),
    });
}

void upsert_part_replacement_chunks(std::vector<PackagePartReplacement>& replacements,
    PartName part_name, std::vector<PackageEntryChunk> chunks,
    PartWriteMode write_mode, std::string reason)
{
    if (auto* replacement = find_replacement(replacements, part_name)) {
        replacement->materialized_small_xml.clear();
        replacement->chunks = std::move(chunks);
        replacement->write_mode = write_mode;
        replacement->reason = std::move(reason);
        clear_indexed_source_entry_direct_range_stats(*replacement);
        return;
    }

    replacements.push_back(PackagePartReplacement {
        std::move(part_name),
        {},
        std::move(chunks),
        write_mode,
        std::move(reason),
    });
}

PackageEntryReplacement* find_entry_replacement(
    std::vector<PackageEntryReplacement>& replacements, std::string_view entry_name) noexcept
{
    const auto item = std::find_if(replacements.begin(), replacements.end(),
        [entry_name](const PackageEntryReplacement& replacement) {
            return replacement.entry_name == entry_name;
        });
    return item == replacements.end() ? nullptr : &*item;
}

const PackageEntryReplacement* find_entry_replacement(
    const std::vector<PackageEntryReplacement>& replacements, std::string_view entry_name) noexcept
{
    const auto item = std::find_if(replacements.begin(), replacements.end(),
        [entry_name](const PackageEntryReplacement& replacement) {
            return replacement.entry_name == entry_name;
        });
    return item == replacements.end() ? nullptr : &*item;
}

void upsert_entry_replacement(const PackageReader& reader,
    std::vector<PackageEntryReplacement>& replacements, std::string entry_name,
    std::string materialized_data)
{
    require_materialized_package_entry_replacement_allowed(reader, entry_name);
    require_materialized_package_entry_replacement_payload_size(reader, entry_name,
        materialized_data.size(), "package-entry replacement");

    if (auto* replacement = find_entry_replacement(replacements, entry_name)) {
        replacement->materialized_data = std::move(materialized_data);
        return;
    }
    replacements.push_back(
        PackageEntryReplacement {std::move(entry_name), std::move(materialized_data)});
}

void remove_entry_replacement(
    std::vector<PackageEntryReplacement>& replacements, std::string_view entry_name)
{
    replacements.erase(
        std::remove_if(replacements.begin(), replacements.end(),
            [entry_name](const PackageEntryReplacement& replacement) {
                return replacement.entry_name == entry_name;
            }),
        replacements.end());
}

void remove_omitted_entry(std::vector<std::string>& entries, std::string_view entry_name)
{
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                      [entry_name](const std::string& value) { return value == entry_name; }),
        entries.end());
}

bool contains_entry_name(const std::vector<std::string>& entries, std::string_view entry_name)
{
    return std::any_of(entries.begin(), entries.end(),
        [entry_name](const std::string& value) { return value == entry_name; });
}

void add_omitted_entry(std::vector<std::string>& entries, std::string entry_name)
{
    if (!contains_entry_name(entries, entry_name)) {
        entries.push_back(std::move(entry_name));
    }
}

void add_omitted_part_entries(std::vector<std::string>& entries, const PartName& part_name)
{
    add_omitted_entry(entries, part_name.zip_path());
    add_omitted_entry(entries, relationship_entry_name_for_source_part(part_name));
}

void merge_copy_original_dependency_reasons(EditPlan& destination, const EditPlan& source)
{
    for (const EditPlanEntry& entry : source.entries()) {
        if (entry.write_mode != PartWriteMode::CopyOriginal || entry.reason.empty()) {
            continue;
        }

        EditPlanEntry* existing = destination.find_part(entry.part_name);
        if (existing != nullptr && existing->write_mode == PartWriteMode::CopyOriginal) {
            existing->reason = entry.reason;
            existing->relationship_owner_part = entry.relationship_owner_part;
            existing->relationship_id = entry.relationship_id;
            existing->relationship_type = entry.relationship_type;
            existing->relationship_target = entry.relationship_target;
        }
    }
}

void audit_preserved_relationship_entry(
    EditPlan& edit_plan, const PackageReader& reader, const PartName& owner_part)
{
    const std::string relationship_entry = relationship_entry_name_for_source_part(owner_part);
    if (reader.find_entry(relationship_entry) == nullptr) {
        return;
    }

    edit_plan.set_package_entry(relationship_entry, PartWriteMode::CopyOriginal,
        "source-owned relationships preserved for " + owner_part.value(),
        PackageEntryAuditKind::SourceRelationships, owner_part.value());
}

bool content_types_semantically_equal(
    const ContentTypesManifest& left, const ContentTypesManifest& right)
{
    if (left.defaults().size() != right.defaults().size()
        || left.overrides().size() != right.overrides().size()) {
        return false;
    }

    for (const ContentTypeDefault& item : left.defaults()) {
        const ContentTypeDefault* other = right.default_for(item.extension);
        if (other == nullptr || other->content_type != item.content_type) {
            return false;
        }
    }
    for (const ContentTypeOverride& item : left.overrides()) {
        const ContentTypeOverride* other = right.override_for(item.part_name);
        if (other == nullptr || other->content_type != item.content_type) {
            return false;
        }
    }
    return true;
}

void restore_source_part_manifest_state(
    PackageManifest& manifest, const PackageReader& reader, const PackagePart& source_part)
{
    if (reader.content_types().override_for(source_part.name) != nullptr) {
        manifest.ensure_part(source_part.name, source_part.content_type);
    } else {
        PackagePart& restored_part = manifest.ensure_part(source_part.name);
        restored_part.content_type = source_part.content_type;
    }

    if (const RelationshipSet* relationships = reader.relationships_for(source_part.name)) {
        for (const Relationship& relationship : relationships->relationships()) {
            manifest.add_relationship(source_part.name, relationship);
        }
    }
}

void sync_content_types_entry_after_part_restore(EditPlan& edit_plan,
    std::vector<PackageEntryReplacement>& entry_replacements, const PackageReader& reader,
    const PackageManifest& manifest)
{
    if (find_entry_replacement(entry_replacements, content_types_entry_name) == nullptr) {
        return;
    }

    if (content_types_semantically_equal(manifest.content_types(), reader.content_types())) {
        remove_entry_replacement(entry_replacements, content_types_entry_name);
        edit_plan.set_package_entry(std::string(content_types_entry_name),
            PartWriteMode::CopyOriginal,
            "content types restored to source after ordinary part replacement",
            PackageEntryAuditKind::ContentTypes);
        return;
    }

    upsert_entry_replacement(reader, entry_replacements, std::string(content_types_entry_name),
        serialize_content_types(manifest.content_types()));
    edit_plan.set_package_entry(std::string(content_types_entry_name),
        PartWriteMode::LocalDomRewrite,
        "content types updated after ordinary part replacement restored a source part",
        PackageEntryAuditKind::ContentTypes);
}

void restore_active_part_entry_state_after_replacement(EditPlan& edit_plan,
    std::vector<PackageEntryReplacement>& entry_replacements,
    std::vector<std::string>& omitted_entries, const PackageReader& reader,
    const PackageManifest& manifest, const PartName& part_name)
{
    remove_entry_replacement(entry_replacements, part_name.zip_path());
    remove_omitted_entry(omitted_entries, part_name.zip_path());

    const std::string relationship_entry = relationship_entry_name_for_source_part(part_name);
    remove_omitted_entry(omitted_entries, relationship_entry);
    if (find_entry_replacement(entry_replacements, relationship_entry) == nullptr) {
        audit_preserved_relationship_entry(edit_plan, reader, part_name);
    }

    sync_content_types_entry_after_part_restore(
        edit_plan, entry_replacements, reader, manifest);
}

void stage_part_removal_entries(EditPlan& edit_plan,
    std::vector<PackagePartReplacement>& replacements,
    std::vector<PackageEntryReplacement>& entry_replacements,
    std::vector<std::string>& omitted_entries, const PackageReader& reader,
    const PartName& part_name)
{
    remove_part_replacement(replacements, part_name);
    remove_entry_replacement(entry_replacements, part_name.zip_path());
    add_omitted_entry(omitted_entries, part_name.zip_path());

    const std::string relationship_entry = relationship_entry_name_for_source_part(part_name);
    remove_entry_replacement(entry_replacements, relationship_entry);
    if (reader.find_entry(relationship_entry) != nullptr) {
        add_omitted_entry(omitted_entries, relationship_entry);
        edit_plan.remove_package_entry(relationship_entry,
            "source-owned relationships omitted with removed package part",
            PackageEntryAuditKind::SourceRelationships, part_name.value());
    }
}

void audit_preserved_relationship_entries(EditPlan& edit_plan, const PackageReader& reader,
    const EditPlan& worksheet_plan, const PartName& worksheet_part)
{
    audit_preserved_relationship_entry(edit_plan, reader, worksheet_part);
    for (const EditPlanEntry& entry : worksheet_plan.entries()) {
        if (entry.write_mode != PartWriteMode::CopyOriginal || entry.reason.empty()) {
            continue;
        }
        audit_preserved_relationship_entry(edit_plan, reader, entry.part_name);
    }
}

void merge_removed_part_audit(
    EditPlan& destination, const EditPlan& removal_plan, const PartName& part_name)
{
    const EditPlanRemovedPart* removed_part = removal_plan.find_removed_part(part_name);
    if (removed_part == nullptr) {
        throw FastXlsxError("part removal plan did not record the target part");
    }

    destination.remove_part(removed_part->part_name, removed_part->reason,
        removed_part->inbound_relationships);
}

std::string read_materialized_source_workbook_xml(
    const PackageReader& reader, std::string_view purpose)
{
    const PartName workbook_part = reader.workbook_part();
    if (const PackageReaderEntry* entry = reader.find_entry(workbook_part.zip_path())) {
        require_materialized_workbook_xml_size(entry->uncompressed_size, purpose);
    }
    try {
        return reader.read_entry(workbook_part.zip_path());
    } catch (const std::exception& error) {
        throw FastXlsxError("failed to read materialized source workbook XML for "
            + std::string(purpose) + ": " + error.what());
    }
}

std::string current_worksheet_input_failure_message(
    std::string_view action, std::string_view purpose, std::string_view details)
{
    return std::string("failed to ") + std::string(action) + " "
        + std::string(purpose) + ": "
        + std::string(details);
}

std::string failed_to_consume_current_worksheet_input_message(
    std::string_view purpose, std::string_view details)
{
    return current_worksheet_input_failure_message("consume", purpose, details);
}

std::string current_planned_materialized_workbook_xml(const PackageReader& reader,
    const std::vector<PackagePartReplacement>& replacements,
    std::string_view purpose)
{
    const PartName workbook_part = reader.workbook_part();
    if (const auto* replacement = find_replacement(replacements, workbook_part.zip_path())) {
        if (!replacement->chunks.empty()) {
            throw FastXlsxError(
                "planned workbook XML uses staged chunks and cannot be materialized for "
                + std::string(purpose));
        }
        require_materialized_workbook_xml_size(
            replacement->materialized_small_xml.size(), purpose);
        return replacement->materialized_small_xml;
    }
    return read_materialized_source_workbook_xml(reader, purpose);
}

std::vector<PackageEntryChunk> source_stored_entry_range_chunks(
    const PackageReader& reader, std::string_view entry_name, std::string_view purpose)
{
    const PackageReaderEntry* entry = reader.find_entry(entry_name);
    if (entry == nullptr) {
        throw FastXlsxError("source package entry is missing for " + std::string(purpose)
            + ": " + std::string(entry_name));
    }
    if (entry->compression_method != stored_compression_method) {
        throw FastXlsxError("source package entry must be stored for " + std::string(purpose)
            + ": " + std::string(entry_name));
    }
    if (entry->compressed_size != entry->uncompressed_size) {
        throw FastXlsxError(
            "stored source package entry has mismatched compressed/uncompressed sizes for "
            + std::string(purpose) + ": " + std::string(entry_name));
    }

    PackageEntryChunk chunk = PackageEntryChunk::file_range(
        reader.path(), entry->data_offset, entry->uncompressed_size);
    chunk.has_expected_size = true;
    chunk.expected_size = entry->uncompressed_size;
    chunk.has_expected_crc32 = true;
    chunk.expected_crc32 = entry->crc32;
    return {std::move(chunk)};
}

std::string expected_actual_size_message(
    std::string_view prefix, std::uint64_t expected_size, std::uint64_t actual_size)
{
    return std::string(prefix) + ": expected " + std::to_string(expected_size)
        + " bytes, actual " + std::to_string(actual_size) + " bytes";
}

std::string expected_at_least_size_message(
    std::string_view prefix, std::uint64_t expected_size)
{
    const std::string actual_size =
        expected_size == std::numeric_limits<std::uint64_t>::max()
        ? "more than " + std::to_string(expected_size)
        : "at least " + std::to_string(expected_size + 1U);
    return std::string(prefix) + ": expected " + std::to_string(expected_size)
        + " bytes, read " + actual_size + " bytes";
}

std::string expected_actual_crc32_message(
    std::string_view prefix, std::uint32_t expected_crc32, std::uint32_t actual_crc32)
{
    return std::string(prefix) + ": expected " + std::to_string(expected_crc32)
        + ", actual " + std::to_string(actual_crc32);
}

const std::array<std::uint32_t, 256>& package_editor_crc32_table()
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

class PackageEditorCrc32Accumulator {
public:
    void update(std::string_view data)
    {
        const auto& table = package_editor_crc32_table();
        for (const unsigned char byte : data) {
            value_ = (value_ >> 8u) ^ table[(value_ ^ byte) & 0xffu];
        }
    }

    void reset() noexcept
    {
        value_ = 0xffffffffu;
    }

    [[nodiscard]] std::uint32_t value() const noexcept
    {
        return value_ ^ 0xffffffffu;
    }

private:
    std::uint32_t value_ = 0xffffffffu;
};

std::uint32_t package_editor_crc32(std::string_view data)
{
    PackageEditorCrc32Accumulator crc;
    crc.update(data);
    return crc.value();
}

std::string staged_chunk_file_range_suffix(const PackageEntryChunk& chunk)
{
    if (!chunk.has_file_range) {
        return {};
    }
    return "; range offset " + std::to_string(chunk.file_offset)
        + ", length " + std::to_string(chunk.file_size) + " bytes";
}

std::uint64_t package_entry_file_chunk_payload_size(const PackageEntryChunk& chunk)
{
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(chunk.path, error);
    if (error) {
        throw FastXlsxError(
            "failed to measure staged package-entry chunk file"
            + staged_chunk_file_range_suffix(chunk));
    }
    if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::uint64_t>::max())) {
        throw FastXlsxError("staged package-entry chunk file is too large");
    }
    const std::uint64_t measured_size = static_cast<std::uint64_t>(size);
    if (!chunk.has_file_range) {
        return measured_size;
    }
    if (chunk.file_offset > measured_size
        || chunk.file_size > measured_size - chunk.file_offset) {
        throw FastXlsxError(
            "staged package-entry chunk file range exceeds file size"
            + staged_chunk_file_range_suffix(chunk));
    }
    return chunk.file_size;
}

std::uint64_t package_entry_file_chunk_payload_offset(const PackageEntryChunk& chunk)
{
    return chunk.has_file_range ? chunk.file_offset : 0;
}

void seek_package_entry_file_chunk_start(
    std::ifstream& stream, const PackageEntryChunk& chunk)
{
    if (!chunk.has_file_range) {
        return;
    }
    if (chunk.file_offset
        > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        throw FastXlsxError(
            "staged package-entry chunk file range offset is too large"
            + staged_chunk_file_range_suffix(chunk));
    }
    stream.seekg(static_cast<std::streamoff>(chunk.file_offset), std::ios::beg);
    if (!stream) {
        throw FastXlsxError(
            "failed to seek staged package-entry chunk file range"
            + staged_chunk_file_range_suffix(chunk));
    }
}

void require_expected_staged_chunk_crc32(
    const PackageEntryChunk& chunk, std::uint32_t actual_crc32)
{
    if (chunk.has_expected_crc32 && chunk.expected_crc32 != actual_crc32) {
        throw FastXlsxError(expected_actual_crc32_message(
            "staged package-entry chunk CRC32 changed after validation",
            chunk.expected_crc32, actual_crc32));
    }
}

void require_staged_chunk_crc32_unchanged(
    std::uint32_t expected_crc32, std::uint32_t actual_crc32)
{
    if (expected_crc32 != actual_crc32) {
        throw FastXlsxError(expected_actual_crc32_message(
            "staged package-entry chunk CRC32 changed after validation",
            expected_crc32, actual_crc32));
    }
}

void require_staged_chunk_size_unchanged(
    std::uint64_t expected_size, std::uint64_t actual_size)
{
    if (expected_size != actual_size) {
        throw FastXlsxError(expected_actual_size_message(
            "staged package-entry chunk size changed after validation",
            expected_size, actual_size));
    }
}

std::uint64_t package_entry_chunk_size(const PackageEntryChunk& chunk)
{
    auto require_expected_size = [](const PackageEntryChunk& current_chunk,
                                     std::uint64_t actual_size) {
        if (current_chunk.has_expected_size
            && current_chunk.expected_size != actual_size) {
            throw FastXlsxError(expected_actual_size_message(
                "staged package-entry chunk size changed after validation",
                current_chunk.expected_size, actual_size));
        }
    };

    switch (chunk.kind) {
    case PackageEntryChunk::Kind::Memory:
        if (!chunk.path.empty()) {
            throw FastXlsxError("staged package-entry chunk cannot mix memory and file sources");
        }
        if (chunk.has_file_range) {
            throw FastXlsxError("staged package-entry memory chunk cannot carry a file range");
        }
        {
            const std::uint64_t size = static_cast<std::uint64_t>(chunk.data.size());
            require_expected_size(chunk, size);
            return size;
        }
    case PackageEntryChunk::Kind::File: {
        if (!chunk.data.empty()) {
            throw FastXlsxError("staged package-entry chunk cannot mix memory and file sources");
        }
        const std::uint64_t measured_size = package_entry_file_chunk_payload_size(chunk);
        require_expected_size(chunk, measured_size);
        return measured_size;
    }
    }

    throw FastXlsxError("unsupported staged package-entry chunk kind");
}

std::uint32_t package_entry_chunk_crc32(const PackageEntryChunk& chunk)
{
    switch (chunk.kind) {
    case PackageEntryChunk::Kind::Memory:
        if (!chunk.path.empty()) {
            throw FastXlsxError("staged package-entry chunk cannot mix memory and file sources");
        }
        {
            const std::uint32_t crc = package_editor_crc32(chunk.data);
            require_expected_staged_chunk_crc32(chunk, crc);
            return crc;
        }
    case PackageEntryChunk::Kind::File: {
        if (!chunk.data.empty()) {
            throw FastXlsxError("staged package-entry chunk cannot mix memory and file sources");
        }
        const std::uint64_t payload_size = package_entry_file_chunk_payload_size(chunk);
        std::ifstream stream(chunk.path, std::ios::binary);
        if (!stream) {
            throw FastXlsxError("failed to open staged package-entry chunk file");
        }
        seek_package_entry_file_chunk_start(stream, chunk);
        PackageEditorCrc32Accumulator crc;
        std::vector<char> buffer(package_entry_file_chunk_size);
        std::uint64_t remaining = payload_size;
        while (remaining > 0) {
            const std::size_t requested = static_cast<std::size_t>(
                std::min<std::uint64_t>(
                    remaining, static_cast<std::uint64_t>(buffer.size())));
            stream.read(buffer.data(), static_cast<std::streamsize>(requested));
            const std::streamsize read_size = stream.gcount();
            if (stream.bad()) {
                throw FastXlsxError("failed to read staged package-entry chunk file");
            }
            if (read_size <= 0
                || static_cast<std::uint64_t>(read_size)
                    != static_cast<std::uint64_t>(requested)) {
                throw FastXlsxError(expected_actual_size_message(
                    "staged package-entry chunk file ended before expected bytes",
                    payload_size, payload_size - remaining
                        + static_cast<std::uint64_t>(
                            std::max<std::streamsize>(read_size, 0))));
            }
            crc.update(std::string_view(buffer.data(), static_cast<std::size_t>(read_size)));
            remaining -= static_cast<std::uint64_t>(read_size);
        }
        const std::uint32_t actual_crc32 = crc.value();
        require_expected_staged_chunk_crc32(chunk, actual_crc32);
        return actual_crc32;
    }
    }

    throw FastXlsxError("unsupported staged package-entry chunk kind");
}

std::string staged_package_entry_chunk_context(
    const PackageEntryChunk& chunk, std::size_t chunk_index)
{
    std::string context = "staged package-entry chunk ";
    context += std::to_string(chunk_index);
    switch (chunk.kind) {
    case PackageEntryChunk::Kind::Memory:
        context += " (memory)";
        break;
    case PackageEntryChunk::Kind::File:
        context += " (file '";
        context += chunk.path.generic_string();
        if (chunk.has_file_range) {
            context += "' range ";
            context += std::to_string(chunk.file_offset);
            context += "+";
            context += std::to_string(chunk.file_size);
        } else {
            context += "'";
        }
        context += ")";
        break;
    default:
        context += " (unknown)";
        break;
    }
    return context;
}

std::uint64_t package_entry_chunk_size_with_context(
    const PackageEntryChunk& chunk, std::size_t chunk_index)
{
    try {
        return package_entry_chunk_size(chunk);
    } catch (const std::exception& error) {
        throw FastXlsxError(staged_package_entry_chunk_context(chunk, chunk_index)
            + " is invalid: " + error.what());
    }
}

std::uint32_t package_entry_chunk_crc32_with_context(
    const PackageEntryChunk& chunk, std::size_t chunk_index)
{
    try {
        return package_entry_chunk_crc32(chunk);
    } catch (const std::exception& error) {
        throw FastXlsxError(staged_package_entry_chunk_context(chunk, chunk_index)
            + " is invalid: " + error.what());
    }
}

std::uint64_t package_entry_chunks_size(std::vector<PackageEntryChunk>& chunks)
{
    std::uint64_t total_size = 0;
    for (std::size_t chunk_index = 0; chunk_index < chunks.size(); ++chunk_index) {
        PackageEntryChunk& chunk = chunks[chunk_index];
        const std::uint64_t chunk_size =
            package_entry_chunk_size_with_context(chunk, chunk_index);
        if (chunk_size > std::numeric_limits<std::uint64_t>::max() - total_size) {
            throw FastXlsxError("staged package-entry chunks are too large");
        }
        total_size += chunk_size;
        chunk.expected_crc32 = package_entry_chunk_crc32_with_context(chunk, chunk_index);
        chunk.has_expected_crc32 = true;
        chunk.expected_size = chunk_size;
        chunk.has_expected_size = true;
    }
    return total_size;
}

std::uint64_t package_entry_chunks_size_without_crc32(std::vector<PackageEntryChunk>& chunks)
{
    std::uint64_t total_size = 0;
    for (std::size_t chunk_index = 0; chunk_index < chunks.size(); ++chunk_index) {
        PackageEntryChunk& chunk = chunks[chunk_index];
        const std::uint64_t chunk_size =
            package_entry_chunk_size_with_context(chunk, chunk_index);
        if (chunk_size > std::numeric_limits<std::uint64_t>::max() - total_size) {
            throw FastXlsxError("staged package-entry chunks are too large");
        }
        total_size += chunk_size;
        chunk.expected_size = chunk_size;
        chunk.has_expected_size = true;
    }
    return total_size;
}

void require_staged_package_entry_chunks_valid(
    std::vector<PackageEntryChunk>& chunks, std::string_view purpose,
    bool validate_crc32 = true)
{
    try {
        if (validate_crc32) {
            (void)package_entry_chunks_size(chunks);
        } else {
            (void)package_entry_chunks_size_without_crc32(chunks);
        }
    } catch (const std::exception& error) {
        throw FastXlsxError(std::string(purpose)
            + " has invalid staged chunks: " + error.what());
    }
}

enum class CurrentWorksheetInputSourceKind {
    SourceEntry,
    PlannedChunks,
};

struct CurrentWorksheetInputSource {
    CurrentWorksheetInputSourceKind kind = CurrentWorksheetInputSourceKind::SourceEntry;
    const std::vector<PackageEntryChunk>* planned_chunks = nullptr;
};

std::string source_worksheet_entry_description(const PartName& worksheet_part)
{
    return "source worksheet entry '" + worksheet_part.zip_path()
        + "' for worksheet part '" + worksheet_part.value() + "' (ZIP entry '"
        + worksheet_part.zip_path() + "')";
}

std::string worksheet_part_diagnostic_context(const PartName& worksheet_part)
{
    return "worksheet part '" + worksheet_part.value() + "' (ZIP entry '"
        + worksheet_part.zip_path() + "')";
}

std::string by_name_worksheet_operation_context(
    std::string_view operation_name,
    std::string_view sheet_name,
    const PartName& worksheet_part)
{
    return std::string(operation_name) + " for sheet '" + std::string(sheet_name)
        + "' resolved to " + worksheet_part_diagnostic_context(worksheet_part);
}

CurrentWorksheetInputSource require_current_worksheet_input_source(
    const PackageReader& reader,
    const std::vector<PackagePartReplacement>& replacements,
    const std::vector<PackageEntryReplacement>& entry_replacements,
    const PartName& worksheet_part,
    std::string_view operation_name)
{
    if (find_entry_replacement(entry_replacements, worksheet_part.zip_path()) != nullptr) {
        throw FastXlsxError(
            std::string(operation_name)
            + " input source cannot use a package-entry string replacement for "
            + source_worksheet_entry_description(worksheet_part) + "; "
              "worksheet input must be a source entry or staged chunks");
    }

    if (const PackagePartReplacement* replacement =
            find_replacement(replacements, worksheet_part.zip_path())) {
        if (replacement->chunks.empty()) {
            throw FastXlsxError(
                std::string(operation_name)
                + " input source cannot use a materialized string replacement for "
                + source_worksheet_entry_description(worksheet_part) + "; "
                  "worksheet input must be a source entry or staged chunks");
        }
        return CurrentWorksheetInputSource {
            CurrentWorksheetInputSourceKind::PlannedChunks, &replacement->chunks};
    }

    if (reader.find_entry(worksheet_part.zip_path()) == nullptr) {
        throw FastXlsxError(std::string(operation_name) + " "
            + source_worksheet_entry_description(worksheet_part) + " is missing");
    }
    return CurrentWorksheetInputSource {
        CurrentWorksheetInputSourceKind::SourceEntry, nullptr};
}

PartName resolve_worksheet_part_by_name_for_patch(const PackageReader& reader,
    const PackageManifest& manifest, const std::vector<PackagePartReplacement>& replacements,
    std::string_view sheet_name)
{
    const PartName workbook_part = reader.workbook_part();
    if (manifest.find_part(workbook_part) == nullptr) {
        throw FastXlsxError(
            "worksheet by-name patch requires a planned workbook sheet catalog; "
            "the source officeDocument workbook part has been removed");
    }
    if (find_replacement(replacements, workbook_part.zip_path()) != nullptr) {
        return reader.worksheet_part_by_sheet_name_from_xml(
            sheet_name,
            current_planned_materialized_workbook_xml(reader, replacements,
                "planned workbook sheet catalog resolution"));
    }

    return reader.worksheet_part_by_sheet_name(sheet_name);
}

void require_sheet_data_replacement_payload_size(
    std::string_view payload_name, std::uint64_t byte_size)
{
    if (byte_size
        <= static_cast<std::uint64_t>(
            package_editor_sheet_data_replacement_payload_byte_limit)) {
        return;
    }

    throw FastXlsxError(
        "worksheet sheetData replacement payload exceeds bounded payload limit for "
        + std::string(payload_name)
        + "; current helper is bounded and chunk-source based, not a large-file "
          "streaming worksheet transformer");
}

std::string missing_cell_replacement_error(
    const std::vector<std::string>& missing_cell_references)
{
    std::string message = "worksheet cell replacement target was not found";
    if (missing_cell_references.empty()) {
        return message;
    }

    message += ": ";
    for (std::size_t index = 0; index < missing_cell_references.size(); ++index) {
        if (index != 0) {
            message += ", ";
        }
        message += missing_cell_references[index];
    }
    return message;
}

std::string cell_reference_list(const std::vector<std::string>& cell_references)
{
    std::string message;
    for (std::size_t index = 0; index < cell_references.size(); ++index) {
        if (index != 0) {
            message += ", ";
        }
        message += cell_references[index];
    }
    return message;
}

struct CellReferenceCoordinate {
    std::uint32_t row = 0;
    std::uint32_t column = 0;
};

struct WorksheetDimensionScan {
    bool has_cells = false;
    std::uint32_t first_row = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t first_column = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t last_row = 0;
    std::uint32_t last_column = 0;
};

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
    const char upper =
        static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return static_cast<std::uint32_t>(upper - 'A' + 1);
}

CellReferenceCoordinate parse_cell_reference_coordinate(std::string_view reference)
{
    if (reference.empty()) {
        throw FastXlsxError("worksheet dimension refresh requires cell references");
    }

    std::uint64_t column = 0;
    std::size_t position = 0;
    while (position < reference.size() && is_ascii_alpha(reference[position])) {
        column = column * 26U + uppercase_column_value(reference[position]);
        if (column > 16384U) {
            throw FastXlsxError("worksheet dimension refresh cell column exceeds Excel limits");
        }
        ++position;
    }
    if (position == 0 || position >= reference.size()) {
        throw FastXlsxError("worksheet dimension refresh found an invalid cell reference");
    }

    std::uint64_t row = 0;
    while (position < reference.size() && is_ascii_digit(reference[position])) {
        row = row * 10U + static_cast<std::uint32_t>(reference[position] - '0');
        if (row > 1048576U) {
            throw FastXlsxError("worksheet dimension refresh cell row exceeds Excel limits");
        }
        ++position;
    }
    if (position != reference.size() || row == 0 || column == 0) {
        throw FastXlsxError("worksheet dimension refresh found an invalid cell reference");
    }

    return CellReferenceCoordinate {
        static_cast<std::uint32_t>(row), static_cast<std::uint32_t>(column)};
}

void include_dimension_cell(
    WorksheetDimensionScan& scan, const CellReferenceCoordinate& coordinate)
{
    scan.has_cells = true;
    scan.first_row = std::min(scan.first_row, coordinate.row);
    scan.first_column = std::min(scan.first_column, coordinate.column);
    scan.last_row = std::max(scan.last_row, coordinate.row);
    scan.last_column = std::max(scan.last_column, coordinate.column);
}

std::string worksheet_dimension_reference(const WorksheetDimensionScan& scan)
{
    if (!scan.has_cells) {
        return "A1";
    }
    return range_reference(scan.first_row, scan.first_column, scan.last_row, scan.last_column);
}

bool is_closing_raw_tag(std::string_view raw_xml)
{
    return raw_xml.size() > 2 && raw_xml[0] == '<' && raw_xml[1] == '/';
}

std::string element_prefix(std::string_view raw_xml)
{
    if (raw_xml.empty() || raw_xml.front() != '<') {
        return {};
    }

    std::size_t position = 1;
    if (position < raw_xml.size() && raw_xml[position] == '/') {
        ++position;
    }
    const std::size_t name_begin = position;
    while (position < raw_xml.size() && !std::isspace(static_cast<unsigned char>(raw_xml[position]))
        && raw_xml[position] != '/' && raw_xml[position] != '>' && raw_xml[position] != '?') {
        ++position;
    }
    const std::string_view qualified_name = raw_xml.substr(name_begin, position - name_begin);
    const std::size_t colon = qualified_name.find(':');
    if (colon == std::string_view::npos) {
        return {};
    }
    return std::string(qualified_name.substr(0, colon));
}

std::string dimension_tag(std::string_view prefix, std::string_view reference)
{
    std::string tag = "<";
    if (!prefix.empty()) {
        tag += prefix;
        tag += ':';
    }
    tag += "dimension ref=\"";
    tag += reference;
    tag += "\"/>";
    return tag;
}

WorksheetEventReaderOptions package_editor_cell_replacement_reader_options()
{
    WorksheetEventReaderOptions options;
    options.max_window_bytes = package_editor_cell_replacement_event_window_byte_limit;
    return options;
}

struct WorksheetCellReplacementStreamAnalysis {
    WorksheetTransformSummary summary;
    WorksheetDimensionScan dimension_scan;
    std::uint64_t scanned_source_cell_count = 0;
    std::size_t worksheet_start_end = std::string_view::npos;
    std::string worksheet_prefix;
    std::size_t dimension_begin = std::string_view::npos;
    std::size_t dimension_end = std::string_view::npos;
    std::string dimension_prefix;
    WorksheetPayloadDependencyAuditResult payload_audit;
};

void audit_worksheet_replacement_metadata_event(
    WorksheetPayloadDependencyAuditResult& result,
    const PartName& worksheet_part,
    std::string_view element_name)
{
    if (element_name == "dimension") {
        return;
    }
    for (const WorksheetLocalMetadataAudit& audit : worksheet_replacement_range_metadata_audits) {
        if (audit.element == element_name) {
            append_worksheet_replacement_range_metadata_audit(
                result, worksheet_part, audit.element, audit.description);
            return;
        }
    }
    for (const WorksheetLocalMetadataAudit& audit :
        worksheet_replacement_relationship_metadata_audits) {
        if (audit.element == element_name) {
            append_worksheet_replacement_relationship_metadata_audit(
                result, worksheet_part, audit.element, audit.description);
            return;
        }
    }
}

void consume_worksheet_cell_replacement_analysis_action(
    WorksheetCellReplacementStreamAnalysis& analysis,
    const PartName& worksheet_part,
    const WorksheetTransformAction& action)
{
    if (action.kind == WorksheetTransformActionKind::ReplaceCell
        || action.kind == WorksheetTransformActionKind::InsertCell) {
        if (action.kind == WorksheetTransformActionKind::ReplaceCell) {
            ++analysis.scanned_source_cell_count;
        }
        if (!action.cell_reference.empty()) {
            include_dimension_cell(analysis.dimension_scan,
                parse_cell_reference_coordinate(action.cell_reference));
        }
        append_payload_audit_result(analysis.payload_audit,
            worksheet_replacement_cell_payload_audit(
                worksheet_part, action.replacement_payload));
        return;
    }

    if (action.event_kind == WorksheetEventKind::WorksheetStart
        && analysis.worksheet_start_end == std::string_view::npos) {
        analysis.worksheet_start_end = 0;
        analysis.worksheet_prefix = element_prefix(action.raw_xml);
        return;
    }
    if (action.event_kind == WorksheetEventKind::Metadata) {
        audit_worksheet_replacement_metadata_event(
            analysis.payload_audit, worksheet_part, action.element_name);
        if (action.element_name == "dimension") {
            if (analysis.dimension_begin == std::string_view::npos
                && !is_closing_raw_tag(action.raw_xml)) {
                analysis.dimension_begin = 0;
                analysis.dimension_prefix = element_prefix(action.raw_xml);
                if (action.self_closing) {
                    analysis.dimension_end = 0;
                }
                return;
            }
            if (analysis.dimension_begin != std::string_view::npos
                && analysis.dimension_end == std::string_view::npos
                && is_closing_raw_tag(action.raw_xml)) {
                analysis.dimension_end = 0;
                return;
            }
        }
        return;
    }
    if (action.event_kind == WorksheetEventKind::CellStart) {
        ++analysis.scanned_source_cell_count;
        if (!action.cell_reference.empty()) {
            include_dimension_cell(analysis.dimension_scan,
                parse_cell_reference_coordinate(action.cell_reference));
        }
        if (cell_start_attribute_equals_for_audit(action.raw_xml, "t", "s")) {
            append_worksheet_replacement_shared_strings_audit(
                analysis.payload_audit, worksheet_part);
        }
        if (cell_start_has_attribute_for_audit(action.raw_xml, "s")) {
            append_worksheet_replacement_styles_audit(
                analysis.payload_audit, worksheet_part);
        }
        return;
    }
    if (action.event_kind == WorksheetEventKind::CellValueMarkup
        && action.element_name == "f") {
        append_worksheet_replacement_formula_audit(
            analysis.payload_audit, worksheet_part);
    }
}

void finalize_worksheet_cell_replacement_stream_analysis(
    WorksheetCellReplacementStreamAnalysis& analysis,
    const PartName& worksheet_part)
{
    if (analysis.worksheet_start_end == std::string_view::npos) {
        throw FastXlsxError("worksheet dimension refresh requires a worksheet root");
    }
    if (analysis.dimension_begin != std::string_view::npos
        && analysis.dimension_end == std::string_view::npos) {
        throw FastXlsxError("worksheet dimension refresh found an unclosed dimension element");
    }

    append_worksheet_replacement_range_metadata_audit(analysis.payload_audit, worksheet_part,
        "dimension", "worksheet dimension metadata");
}

WorksheetCellReplacementStreamAnalysis analyze_worksheet_cell_replacement_stream_from_chunk_source(
    const PartName& worksheet_part,
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetCellReplacementPlan& replacement_plan,
    WorksheetCellReplacementMode mode)
{
    WorksheetCellReplacementStreamAnalysis analysis;
    analysis.summary = scan_cell_replacement_actions_from_chunk_source(
        read_next_chunk, replacement_plan, [&](const WorksheetTransformAction& action) {
            consume_worksheet_cell_replacement_analysis_action(
                analysis, worksheet_part, action);
        },
        package_editor_cell_replacement_reader_options(), mode);

    finalize_worksheet_cell_replacement_stream_analysis(analysis, worksheet_part);
    return analysis;
}

void write_file_chunk(std::ofstream& output, std::string_view chunk)
{
    while (!chunk.empty()) {
        const std::size_t write_size = std::min<std::size_t>(chunk.size(),
            static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()));
        output.write(chunk.data(), static_cast<std::streamsize>(write_size));
        if (!output) {
            throw FastXlsxError("failed to write temporary worksheet XML");
        }
        chunk.remove_prefix(write_size);
    }
}

void write_file_chunk_and_scan_relationships(std::ofstream& output,
    WorksheetRelationshipReferenceScanner* relationship_scanner,
    bool& relationship_scan_failed,
    std::string_view chunk)
{
    if (relationship_scanner != nullptr && !relationship_scan_failed) {
        try {
            scan_xml_relationship_references(*relationship_scanner, chunk);
        } catch (const std::exception&) {
            relationship_scan_failed = true;
        }
    }
    write_file_chunk(output, chunk);
}

WorksheetReplacementChunkAuditResult write_worksheet_replacement_file_and_audit_from_chunk_source(
    const std::filesystem::path& path,
    const PartName& worksheet_part,
    const WorksheetInputChunkCallback& read_next_chunk,
    const RelationshipSet* worksheet_relationships,
    std::string_view failure_context)
{
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw FastXlsxError("failed to create temporary " + std::string(failure_context));
    }

    WorksheetReplacementPayloadAuditCollector payload_collector(worksheet_part);
    WorksheetRelationshipReferenceScanner relationship_scanner;
    bool relationship_scan_failed = false;
    const WorksheetInputChunkCallback write_and_audit_source =
        [&](std::string& chunk) {
            bool has_chunk = false;
            try {
                has_chunk = read_next_chunk(chunk);
            } catch (const std::exception& error) {
                throw FastXlsxError("failed while reading " + std::string(failure_context)
                    + ": " + error.what());
            }
            if (!has_chunk) {
                return false;
            }

            write_file_chunk(output, chunk);
            return true;
        };

    scan_worksheet_events_from_chunk_source(
        write_and_audit_source,
        [&](const WorksheetEvent& event) {
            if (raw_xml_is_start_tag_for_audit(event.raw_xml)) {
                const XmlTagRange tag {0, event.raw_xml.size() - 1};
                payload_collector.process_start_tag(event.raw_xml, tag);
            }
            if (!relationship_scan_failed) {
                try {
                    scan_xml_relationship_references(relationship_scanner, event.raw_xml);
                } catch (const std::exception&) {
                    relationship_scan_failed = true;
                }
            }
        },
        package_editor_cell_replacement_reader_options());
    finish_xml_relationship_references(relationship_scanner, relationship_scan_failed);

    output.close();
    if (!output) {
        throw FastXlsxError("failed to finalize temporary " + std::string(failure_context));
    }

    WorksheetReplacementChunkAuditResult result;
    result.payload_audit = std::move(payload_collector).finish();
    result.relationship_reference_audit =
        relationship_scan_failed
        ? worksheet_relationship_reference_parse_failure_audit()
        : worksheet_relationship_reference_audit_from_references(
            worksheet_part, relationship_scanner.references(), worksheet_relationships);
    return result;
}

WorksheetPayloadDependencyAuditResult
write_sheet_data_replacement_chunks_and_audit_from_chunk_source(
    std::ofstream& output,
    const PartName& worksheet_part,
    WorksheetRelationshipReferenceScanner* relationship_scanner,
    bool& relationship_scan_failed,
    const WorksheetInputChunkCallback& read_next_chunk,
    std::string_view failure_context,
    std::string_view payload_name)
{
    std::uint64_t byte_size = 0;
    const WorksheetInputChunkCallback write_and_audit_source =
        [&](std::string& chunk) {
            bool has_chunk = false;
            try {
                has_chunk = read_next_chunk(chunk);
            } catch (const std::exception& error) {
                throw FastXlsxError("failed while reading " + std::string(failure_context)
                    + ": " + error.what());
            }
            if (!has_chunk) {
                return false;
            }

            const std::uint64_t next_size =
                byte_size + static_cast<std::uint64_t>(chunk.size());
            if (next_size < byte_size) {
                throw FastXlsxError("worksheet sheetData replacement size overflow");
            }
            require_sheet_data_replacement_payload_size(payload_name, next_size);
            write_file_chunk_and_scan_relationships(
                output, relationship_scanner, relationship_scan_failed, chunk);
            byte_size = next_size;
            return true;
        };

    return worksheet_sheet_data_replacement_audit_from_chunk_source(
        worksheet_part, write_and_audit_source);
}

WorksheetPayloadDependencyAuditResult write_worksheet_sheet_data_replacement_stream_from_chunk_source(
    const std::filesystem::path& path,
    const PartName& worksheet_part,
    WorksheetRelationshipReferenceScanner* relationship_scanner,
    bool& relationship_scan_failed,
    WorksheetSheetDataPreservationAuditCollector* preservation_collector,
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetInputChunkCallback& read_replacement_sheet_data_chunk,
    std::optional<std::string_view> dimension_reference,
    bool source_has_dimension)
{
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw FastXlsxError("failed to create temporary rewritten worksheet XML");
    }

    const bool refresh_dimension = dimension_reference.has_value();
    WorksheetPayloadDependencyAuditResult replacement_audit;
    bool skipping_dimension = false;
    bool skipping_sheet_data = false;
    bool saw_sheet_data = false;
    bool replacement_failure = false;
    const auto write_replacement_sheet_data = [&]() {
        try {
            replacement_audit =
                write_sheet_data_replacement_chunks_and_audit_from_chunk_source(
                    output, worksheet_part, relationship_scanner,
                    relationship_scan_failed, read_replacement_sheet_data_chunk,
                    "sheetData replacement XML", "replacement sheetData XML");
        } catch (const std::exception&) {
            replacement_failure = true;
            throw;
        }
    };
    try {
        scan_worksheet_events_from_chunk_source(
            read_next_chunk,
            [&](const WorksheetEvent& event) {
                if (skipping_dimension) {
                    if (event.kind == WorksheetEventKind::Metadata
                        && event.element_name == "dimension"
                        && is_closing_raw_tag(event.raw_xml)) {
                        skipping_dimension = false;
                    }
                    return;
                }

                if (preservation_collector != nullptr
                    && !(refresh_dimension
                        && event.kind == WorksheetEventKind::Metadata
                        && event.element_name == "dimension")) {
                    preservation_collector->process_event(event);
                }
                if (event.kind == WorksheetEventKind::SheetDataStart) {
                    saw_sheet_data = true;
                    write_replacement_sheet_data();
                    skipping_sheet_data = true;
                    return;
                }
                if (event.kind == WorksheetEventKind::SheetDataEnd) {
                    skipping_sheet_data = false;
                    return;
                }
                if (skipping_sheet_data) {
                    return;
                }

                if (refresh_dimension
                    && event.kind == WorksheetEventKind::WorksheetStart) {
                    write_file_chunk_and_scan_relationships(
                        output, relationship_scanner, relationship_scan_failed,
                        event.raw_xml);
                    if (!source_has_dimension) {
                        write_file_chunk_and_scan_relationships(
                            output, relationship_scanner, relationship_scan_failed,
                            dimension_tag(element_prefix(event.raw_xml), *dimension_reference));
                    }
                    return;
                }

                if (refresh_dimension
                    && event.kind == WorksheetEventKind::Metadata
                    && event.element_name == "dimension"
                    && !is_closing_raw_tag(event.raw_xml)) {
                    write_file_chunk_and_scan_relationships(
                        output, relationship_scanner, relationship_scan_failed,
                        dimension_tag(element_prefix(event.raw_xml), *dimension_reference));
                    if (!event.self_closing) {
                        skipping_dimension = true;
                    }
                    return;
                }

                if (refresh_dimension
                    && !saw_sheet_data
                    && event.kind == WorksheetEventKind::WorksheetEnd) {
                    write_replacement_sheet_data();
                    saw_sheet_data = true;
                }

                write_file_chunk_and_scan_relationships(
                    output, relationship_scanner, relationship_scan_failed, event.raw_xml);
            },
            package_editor_cell_replacement_reader_options());
    } catch (const std::exception& error) {
        if (replacement_failure) {
            throw;
        }
        throw FastXlsxError(failed_to_consume_current_worksheet_input_message(
            worksheet_sheet_data_replacement_output_input_context, error.what()));
    }

    if (!saw_sheet_data) {
        throw FastXlsxError(failed_to_consume_current_worksheet_input_message(
            worksheet_sheet_data_replacement_output_input_context,
            "worksheet XML sheetData element is missing"));
    }
    if (relationship_scanner != nullptr) {
        finish_xml_relationship_references(*relationship_scanner, relationship_scan_failed);
    }

    output.close();
    if (!output) {
        throw FastXlsxError("failed to finalize temporary rewritten worksheet XML");
    }
    return replacement_audit;
}

bool worksheet_source_has_dimension_metadata(const WorksheetInputChunkCallback& read_next_chunk)
{
    bool has_dimension = false;
    scan_worksheet_events_from_chunk_source(
        read_next_chunk,
        [&](const WorksheetEvent& event) {
            if (event.kind == WorksheetEventKind::Metadata
                && event.element_name == "dimension"
                && !is_closing_raw_tag(event.raw_xml)) {
                has_dimension = true;
            }
        },
        package_editor_cell_replacement_reader_options());
    return has_dimension;
}

void write_worksheet_cell_replacement_action(std::ofstream& output,
    WorksheetRelationshipReferenceScanner* relationship_scanner,
    const WorksheetTransformAction& action,
    const WorksheetCellReplacementStreamAnalysis& analysis,
    std::string_view dimension_reference,
    bool& skipping_dimension,
    bool& relationship_scan_failed)
{
    if (skipping_dimension) {
        if (action.event_kind == WorksheetEventKind::Metadata
            && action.element_name == "dimension"
            && is_closing_raw_tag(action.raw_xml)) {
            skipping_dimension = false;
        }
        return;
    }

    if (action.kind == WorksheetTransformActionKind::ReplaceCell
        || action.kind == WorksheetTransformActionKind::InsertCell) {
        action.replacement_payload.for_each_chunk([&](std::string_view chunk) {
            write_file_chunk_and_scan_relationships(
                output, relationship_scanner, relationship_scan_failed, chunk);
        });
        return;
    }

    if (action.event_kind == WorksheetEventKind::WorksheetStart) {
        write_file_chunk_and_scan_relationships(
            output, relationship_scanner, relationship_scan_failed, action.raw_xml);
        if (analysis.dimension_begin == std::string_view::npos) {
            write_file_chunk_and_scan_relationships(output, relationship_scanner,
                relationship_scan_failed,
                dimension_tag(analysis.worksheet_prefix, dimension_reference));
        }
        return;
    }

    if (action.event_kind == WorksheetEventKind::Metadata
        && action.element_name == "dimension"
        && !is_closing_raw_tag(action.raw_xml)) {
        write_file_chunk_and_scan_relationships(output, relationship_scanner,
            relationship_scan_failed,
            dimension_tag(analysis.dimension_prefix, dimension_reference));
        if (!action.self_closing) {
            skipping_dimension = true;
        }
        return;
    }

    write_file_chunk_and_scan_relationships(
        output, relationship_scanner, relationship_scan_failed, action.raw_xml);
}

WorksheetRelationshipReferenceAuditResult write_worksheet_cell_replacement_stream_from_chunk_source(
    const std::filesystem::path& path,
    const PartName& worksheet_part,
    const RelationshipSet* worksheet_relationships,
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetCellReplacementPlan& replacement_plan,
    const WorksheetCellReplacementStreamAnalysis& analysis,
    WorksheetCellReplacementMode mode)
{
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw FastXlsxError("failed to create temporary worksheet cell replacement XML");
    }

    const std::string dimension_reference =
        worksheet_dimension_reference(analysis.dimension_scan);
    bool skipping_dimension = false;
    bool relationship_scan_failed = false;
    WorksheetRelationshipReferenceScanner relationship_scanner;

    (void)scan_cell_replacement_actions_from_chunk_source(
        read_next_chunk, replacement_plan, [&](const WorksheetTransformAction& action) {
            write_worksheet_cell_replacement_action(
                output, &relationship_scanner, action, analysis,
                dimension_reference, skipping_dimension, relationship_scan_failed);
        },
        package_editor_cell_replacement_reader_options(), mode);
    finish_xml_relationship_references(relationship_scanner, relationship_scan_failed);

    output.close();
    if (!output) {
        throw FastXlsxError("failed to finalize temporary worksheet cell replacement XML");
    }

    if (relationship_scan_failed) {
        return worksheet_relationship_reference_parse_failure_audit();
    }
    return worksheet_relationship_reference_audit_from_references(
        worksheet_part, relationship_scanner.references(), worksheet_relationships);
}

struct SinglePassWorksheetTransformResult {
    WorksheetCellReplacementStreamAnalysis analysis;
    WorksheetRelationshipReferenceAuditResult relationship_reference_audit;
    std::uint64_t temporary_output_bytes = 0;
    std::uint64_t dimension_insertion_offset = 0;
    std::string dimension_xml;
};

void write_counted_worksheet_chunk(std::ofstream& output,
    WorksheetRelationshipReferenceScanner& relationship_scanner,
    bool& relationship_scan_failed,
    std::uint64_t& output_bytes,
    std::string_view chunk)
{
    if (chunk.size() > std::numeric_limits<std::uint64_t>::max() - output_bytes) {
        throw FastXlsxError("single-pass worksheet transform output size overflow");
    }
    write_file_chunk_and_scan_relationships(
        output, &relationship_scanner, relationship_scan_failed, chunk);
    output_bytes += static_cast<std::uint64_t>(chunk.size());
}

SinglePassWorksheetTransformResult write_worksheet_cell_transform_single_pass(
    const std::filesystem::path& path,
    const PartName& worksheet_part,
    const RelationshipSet* worksheet_relationships,
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetCellReplacementPlan& replacement_plan,
    WorksheetCellReplacementMode mode)
{
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw FastXlsxError("failed to create temporary single-pass worksheet transform XML");
    }

    SinglePassWorksheetTransformResult result;
    WorksheetRelationshipReferenceScanner relationship_scanner;
    bool relationship_scan_failed = false;
    bool skipping_dimension = false;
    std::optional<std::uint64_t> worksheet_root_end;
    std::optional<std::uint64_t> source_dimension_offset;

    result.analysis.summary = scan_cell_replacement_actions_from_chunk_source(
        read_next_chunk, replacement_plan, [&](const WorksheetTransformAction& action) {
            consume_worksheet_cell_replacement_analysis_action(
                result.analysis, worksheet_part, action);

            if (skipping_dimension) {
                if (action.event_kind == WorksheetEventKind::Metadata
                    && action.element_name == "dimension"
                    && is_closing_raw_tag(action.raw_xml)) {
                    skipping_dimension = false;
                }
                return;
            }

            if (action.event_kind == WorksheetEventKind::Metadata
                && action.element_name == "dimension") {
                if (!is_closing_raw_tag(action.raw_xml)) {
                    if (!source_dimension_offset.has_value()) {
                        source_dimension_offset = result.temporary_output_bytes;
                    }
                    skipping_dimension = !action.self_closing;
                }
                return;
            }

            if (action.kind == WorksheetTransformActionKind::ReplaceCell
                || action.kind == WorksheetTransformActionKind::InsertCell) {
                action.replacement_payload.for_each_chunk([&](std::string_view chunk) {
                    write_counted_worksheet_chunk(output, relationship_scanner,
                        relationship_scan_failed, result.temporary_output_bytes, chunk);
                });
                return;
            }

            write_counted_worksheet_chunk(output, relationship_scanner,
                relationship_scan_failed, result.temporary_output_bytes, action.raw_xml);
            if (action.event_kind == WorksheetEventKind::WorksheetStart
                && !worksheet_root_end.has_value()) {
                worksheet_root_end = result.temporary_output_bytes;
            }
        },
        package_editor_cell_replacement_reader_options(), mode);

    finalize_worksheet_cell_replacement_stream_analysis(result.analysis, worksheet_part);
    finish_xml_relationship_references(relationship_scanner, relationship_scan_failed);
    output.close();
    if (!output) {
        throw FastXlsxError("failed to finalize temporary single-pass worksheet transform XML");
    }
    if (!worksheet_root_end.has_value()) {
        throw FastXlsxError("single-pass worksheet transform requires a worksheet root");
    }

    result.dimension_insertion_offset =
        source_dimension_offset.value_or(*worksheet_root_end);
    const std::string_view dimension_prefix =
        source_dimension_offset.has_value()
        ? std::string_view(result.analysis.dimension_prefix)
        : std::string_view(result.analysis.worksheet_prefix);
    result.dimension_xml = dimension_tag(
        dimension_prefix, worksheet_dimension_reference(result.analysis.dimension_scan));
    result.relationship_reference_audit = relationship_scan_failed
        ? worksheet_relationship_reference_parse_failure_audit()
        : worksheet_relationship_reference_audit_from_references(
            worksheet_part, relationship_scanner.references(), worksheet_relationships);
    return result;
}

std::vector<PackageEntryChunk> make_dimension_inserted_worksheet_chunks(
    const std::filesystem::path& path,
    std::uint64_t temporary_output_bytes,
    std::uint64_t dimension_insertion_offset,
    std::string dimension_xml)
{
    if (dimension_insertion_offset > temporary_output_bytes) {
        throw FastXlsxError(
            "single-pass worksheet dimension insertion offset exceeds temporary output");
    }

    std::vector<PackageEntryChunk> chunks;
    chunks.reserve(3);
    if (dimension_insertion_offset > 0) {
        chunks.push_back(PackageEntryChunk::file_range(
            path, 0, dimension_insertion_offset));
    }
    chunks.push_back(PackageEntryChunk::memory(std::move(dimension_xml)));
    const std::uint64_t tail_bytes = temporary_output_bytes - dimension_insertion_offset;
    if (tail_bytes > 0) {
        chunks.push_back(PackageEntryChunk::file_range(
            path, dimension_insertion_offset, tail_bytes));
    }
    return chunks;
}

bool contains_planned_output_entry(
    const std::vector<PackageEditorOutputEntryPlan>& entries, std::string_view entry_name)
{
    return std::any_of(entries.begin(), entries.end(),
        [entry_name](const PackageEditorOutputEntryPlan& entry) {
            return entry.entry_name == entry_name;
        });
}

const EditPlanEntry* find_part_plan_by_entry_name(
    const EditPlan& edit_plan, std::string_view entry_name)
{
    const auto item = std::find_if(edit_plan.entries().begin(), edit_plan.entries().end(),
        [entry_name](const EditPlanEntry& entry) {
            const std::string zip_path = entry.part_name.zip_path();
            return std::string_view(zip_path) == entry_name;
        });
    return item == edit_plan.entries().end() ? nullptr : &*item;
}

const EditPlanRemovedPart* find_removed_part_plan_by_entry_name(
    const EditPlan& edit_plan, std::string_view entry_name)
{
    const auto item =
        std::find_if(edit_plan.removed_parts().begin(), edit_plan.removed_parts().end(),
            [entry_name](const EditPlanRemovedPart& entry) {
                const std::string zip_path = entry.part_name.zip_path();
                return std::string_view(zip_path) == entry_name;
            });
    return item == edit_plan.removed_parts().end() ? nullptr : &*item;
}

void apply_part_plan(PackageEditorOutputEntryPlan& output_plan, const EditPlanEntry& part_plan)
{
    output_plan.package_part = true;
    output_plan.part_name = part_plan.part_name.value();
    output_plan.write_mode = part_plan.write_mode;
    output_plan.generated = part_plan.write_mode == PartWriteMode::GenerateSmallXml;
    output_plan.reason = part_plan.reason;
    output_plan.relationship_owner_part = part_plan.relationship_owner_part;
    output_plan.relationship_id = part_plan.relationship_id;
    output_plan.relationship_type = part_plan.relationship_type;
    output_plan.relationship_target = part_plan.relationship_target;
}

void apply_package_entry_plan(
    PackageEditorOutputEntryPlan& output_plan, const EditPlanPackageEntry& package_entry)
{
    output_plan.write_mode = package_entry.write_mode;
    output_plan.reason = package_entry.reason;
    output_plan.audit_kind = package_entry.audit_kind;
    output_plan.owner_part = package_entry.owner_part;
}

void apply_removed_package_entry_plan(PackageEditorOutputEntryPlan& output_plan,
    const EditPlanRemovedPackageEntry& removed_package_entry)
{
    output_plan.reason = removed_package_entry.reason;
    output_plan.audit_kind = removed_package_entry.audit_kind;
    output_plan.owner_part = removed_package_entry.owner_part;
}

std::string source_entry_file_backed_copy_reason(const PackageReader& reader,
    const PackageEditorOutputEntryPlan& plan)
{
    if (!plan.source_entry || !plan.copied_from_source || plan.omitted
        || plan.write_mode != PartWriteMode::CopyOriginal) {
        return {};
    }

    if (plan.part_name.empty()) {
        if (plan.entry_name == content_types_entry_name) {
            return "copy-original content types metadata entry uses file-backed source copy";
        }
        if (plan.entry_name == package_relationships_entry_name) {
            return "copy-original package relationships metadata entry uses file-backed source copy";
        }
        if (plan.audit_kind == PackageEntryAuditKind::SourceRelationships) {
            return "copy-original source-owned relationships metadata entry uses file-backed source copy";
        }
        return "copy-original metadata package entry uses file-backed source copy";
    }

    const PackagePart* part = reader.part_index().find_part(PartName(plan.part_name));
    if (part != nullptr && part->content_type == content_type_worksheet) {
        return "copy-original worksheet source entry uses file-backed source copy";
    }
    if (part != nullptr && part->content_type == content_type_shared_strings) {
        return "copy-original sharedStrings source entry uses file-backed source copy";
    }

    return "copy-original package part source entry uses file-backed source copy";
}

const PackagePart* source_part_for_entry_name(
    const PackageReader& reader, std::string_view entry_name)
{
    return reader.part_index().find_part(PartName(entry_name));
}

bool package_part_is_materialized_small_xml_boundary(
    const PackagePart& package_part) noexcept
{
    return package_part.content_type == content_type_workbook
        || package_part.content_type == content_type_core_properties
        || package_part.content_type == content_type_extended_properties;
}

void require_materialized_source_part_replacement_allowed(
    const PackageReader& reader, const PartName& part_name)
{
    const PackagePart* source_part = reader.part_index().find_part(part_name);
    if (source_part == nullptr
        || package_part_is_materialized_small_xml_boundary(*source_part)) {
        return;
    }

    throw FastXlsxError(
        "materialized source package part replacement is not allowed; "
        "use replace_part_chunks() so source package part payloads stay staged");
}

void require_staged_package_part_replacement_allowed(const PackagePart& package_part)
{
    if (!package_part_is_materialized_small_xml_boundary(package_part)) {
        return;
    }

    throw FastXlsxError(
        "staged replacement is not allowed for workbook/core/app small XML package parts; "
        "use replace_part() so metadata stays materialized under the small XML guard");
}

void require_materialized_package_entry_replacement_allowed(
    const PackageReader& reader, std::string_view entry_name)
{
    (void)reader;
    if (is_metadata_package_entry_name(entry_name)) {
        return;
    }

    throw FastXlsxError(
        "materialized package-entry replacement is only allowed for OPC metadata entries; "
        "package parts must use package-part replacement or staged chunks");
}

std::string materialized_entry_replacement_reason(
    const PackageReader& reader, const PackageEditorOutputEntryPlan& plan)
{
    if (!plan.materialized_replacement) {
        return {};
    }

    (void)reader;

    if (plan.entry_name == content_types_entry_name) {
        return "active content types metadata replacement uses materialized payload";
    }
    if (plan.entry_name == package_relationships_entry_name) {
        return "active package relationships metadata replacement uses materialized payload";
    }
    if (plan.audit_kind == PackageEntryAuditKind::SourceRelationships) {
        return "active source-owned relationships metadata replacement uses materialized payload";
    }
    if (!is_metadata_package_entry_name(plan.entry_name)) {
        return "active disallowed package part package-entry replacement would be rejected before materialized output";
    }
    return "active metadata package-entry replacement uses materialized payload";
}

std::string materialized_part_replacement_reason(
    const PackageReader& reader, const PackagePartReplacement& replacement)
{
    if (!replacement.chunks.empty()) {
        return {};
    }

    const PackagePart* source_part =
        reader.part_index().find_part(replacement.part_name);
    if (source_part != nullptr
        && !package_part_is_materialized_small_xml_boundary(*source_part)) {
        return "active disallowed source package part materialized replacement would be rejected before planned output";
    }

    if (source_part != nullptr && source_part->content_type == content_type_workbook) {
        return "active workbook small-XML package part replacement uses materialized payload";
    }
    if (replacement.write_mode == PartWriteMode::GenerateSmallXml) {
        return "active generated small-XML package part replacement uses materialized payload";
    }

    if (source_part != nullptr) {
        if (source_part->content_type == content_type_core_properties) {
            return "active core properties package part replacement uses materialized payload";
        }
        if (source_part->content_type == content_type_extended_properties) {
            return "active extended properties package part replacement uses materialized payload";
        }
    }
    return "active disallowed source package part materialized replacement would be rejected before planned output";
}

struct PackageEntryChunkOutputStats {
    std::size_t chunk_count = 0;
    std::size_t memory_chunk_count = 0;
    std::size_t file_chunk_count = 0;
    std::size_t file_range_chunk_count = 0;
    std::uint64_t expected_bytes = 0;
    std::uint64_t memory_bytes = 0;
    std::uint64_t file_bytes = 0;
    std::uint64_t file_range_bytes = 0;
    bool expected_bytes_complete = true;
};

void add_output_stat_bytes(std::uint64_t& total, std::uint64_t value)
{
    if (value > std::numeric_limits<std::uint64_t>::max() - total) {
        throw FastXlsxError("package entry chunk output statistics overflow");
    }
    total += value;
}

void add_expected_chunk_bytes(PackageEntryChunkOutputStats& stats, std::uint64_t bytes)
{
    add_output_stat_bytes(stats.expected_bytes, bytes);
}

PackageEntryChunkOutputStats summarize_package_entry_chunks_for_output_plan(
    const std::vector<PackageEntryChunk>& chunks)
{
    PackageEntryChunkOutputStats stats;
    stats.chunk_count = chunks.size();
    for (const PackageEntryChunk& chunk : chunks) {
        switch (chunk.kind) {
        case PackageEntryChunk::Kind::Memory: {
            ++stats.memory_chunk_count;
            const auto bytes = static_cast<std::uint64_t>(chunk.data.size());
            add_output_stat_bytes(stats.memory_bytes, bytes);
            add_expected_chunk_bytes(stats, bytes);
            break;
        }
        case PackageEntryChunk::Kind::File:
            ++stats.file_chunk_count;
            if (chunk.has_file_range) {
                ++stats.file_range_chunk_count;
                add_output_stat_bytes(stats.file_range_bytes, chunk.file_size);
                add_output_stat_bytes(stats.file_bytes, chunk.file_size);
                add_expected_chunk_bytes(stats, chunk.file_size);
            } else if (chunk.has_expected_size) {
                add_output_stat_bytes(stats.file_bytes, chunk.expected_size);
                add_expected_chunk_bytes(stats, chunk.expected_size);
            } else {
                stats.expected_bytes_complete = false;
            }
            break;
        }
    }
    return stats;
}

void apply_staged_replacement_chunk_stats(
    PackageEditorOutputEntryPlan& plan, const std::vector<PackageEntryChunk>& chunks)
{
    const PackageEntryChunkOutputStats stats =
        summarize_package_entry_chunks_for_output_plan(chunks);
    plan.staged_replacement_chunk_count = stats.chunk_count;
    plan.staged_replacement_memory_chunk_count = stats.memory_chunk_count;
    plan.staged_replacement_file_chunk_count = stats.file_chunk_count;
    plan.staged_replacement_file_range_chunk_count = stats.file_range_chunk_count;
    plan.staged_replacement_expected_bytes = stats.expected_bytes;
    plan.staged_replacement_memory_bytes = stats.memory_bytes;
    plan.staged_replacement_file_bytes = stats.file_bytes;
    plan.staged_replacement_file_range_bytes = stats.file_range_bytes;
    plan.staged_replacement_expected_bytes_complete = stats.expected_bytes_complete;
}

PackageEditorOutputEntryPlan make_output_entry_plan(const PackageReader& reader,
    const EditPlan& edit_plan, const std::vector<PackagePartReplacement>& replacements,
    const std::vector<PackageEntryReplacement>& entry_replacements,
    const std::vector<std::string>& omitted_entries, std::string entry_name,
    bool source_entry)
{
    PackageEditorOutputEntryPlan plan;
    plan.entry_name = std::move(entry_name);
    plan.source_entry = source_entry;

    if (const auto* package_entry_plan = edit_plan.find_package_entry(plan.entry_name)) {
        apply_package_entry_plan(plan, *package_entry_plan);
    } else if (const auto* part_plan = find_part_plan_by_entry_name(edit_plan, plan.entry_name)) {
        apply_part_plan(plan, *part_plan);
    }

    if (contains_entry_name(omitted_entries, plan.entry_name)) {
        plan.omitted = true;
        if (const auto* removed_package_entry =
                edit_plan.find_removed_package_entry(plan.entry_name)) {
            apply_removed_package_entry_plan(plan, *removed_package_entry);
        } else if (const auto* removed_part =
                       find_removed_part_plan_by_entry_name(edit_plan, plan.entry_name)) {
            plan.package_part = true;
            plan.part_name = removed_part->part_name.value();
            plan.reason = removed_part->reason;
            plan.inbound_relationships = removed_part->inbound_relationships;
        }
        return plan;
    }

    if (const auto* replacement = find_replacement(replacements, plan.entry_name)) {
        plan.package_part = true;
        plan.part_name = replacement->part_name.value();
        plan.write_mode = replacement->write_mode;
        plan.generated = replacement->write_mode == PartWriteMode::GenerateSmallXml;
        plan.staged_replacement_chunks = !replacement->chunks.empty();
        plan.materialized_replacement = replacement->chunks.empty();
        plan.indexed_source_entry_direct_range =
            replacement->indexed_source_entry_direct_range;
        plan.indexed_source_entry_scanned_source_cell_count =
            replacement->indexed_source_entry_scanned_source_cell_count;
        plan.indexed_source_entry_matched_replacement_count =
            replacement->indexed_source_entry_matched_replacement_count;
        plan.indexed_source_entry_staged_output_bytes =
            replacement->indexed_source_entry_staged_output_bytes;
        plan.indexed_source_entry_source_range_chunk_ms =
            replacement->indexed_source_entry_source_range_chunk_ms;
        plan.indexed_source_entry_target_plan_ms =
            replacement->indexed_source_entry_target_plan_ms;
        plan.indexed_source_entry_payload_audit_ms =
            replacement->indexed_source_entry_payload_audit_ms;
        plan.indexed_source_entry_relationship_audit_ms =
            replacement->indexed_source_entry_relationship_audit_ms;
        plan.indexed_source_entry_descriptor_ms =
            replacement->indexed_source_entry_descriptor_ms;
        plan.indexed_source_entry_commit_ms =
            replacement->indexed_source_entry_commit_ms;
        plan.single_pass_worksheet_transform =
            replacement->single_pass_worksheet_transform;
        plan.single_pass_scanned_source_cell_count =
            replacement->single_pass_scanned_source_cell_count;
        plan.single_pass_matched_replacement_count =
            replacement->single_pass_matched_replacement_count;
        plan.single_pass_inserted_cell_count =
            replacement->single_pass_inserted_cell_count;
        plan.single_pass_staged_output_bytes =
            replacement->single_pass_staged_output_bytes;
        plan.single_pass_transform_ms =
            replacement->single_pass_transform_ms;
        plan.single_pass_commit_ms =
            replacement->single_pass_commit_ms;
        if (plan.staged_replacement_chunks) {
            apply_staged_replacement_chunk_stats(plan, replacement->chunks);
        }
        plan.materialized_replacement_reason =
            materialized_part_replacement_reason(reader, *replacement);
        if (plan.reason.empty()) {
            plan.reason = replacement->reason;
        }
        plan.copied_from_source = false;
        return plan;
    }

    if (find_entry_replacement(entry_replacements, plan.entry_name) != nullptr) {
        plan.copied_from_source = false;
        plan.materialized_replacement = true;
        plan.materialized_replacement_reason =
            materialized_entry_replacement_reason(reader, plan);
        if (plan.write_mode == PartWriteMode::CopyOriginal
            && find_part_plan_by_entry_name(edit_plan, plan.entry_name) == nullptr
            && edit_plan.find_package_entry(plan.entry_name) == nullptr) {
            plan.write_mode = PartWriteMode::LocalDomRewrite;
            plan.reason = "package entry replacement";
        }
        return plan;
    }

    plan.copied_from_source = reader.find_entry(plan.entry_name) != nullptr;

    // Patch audit visibility: a preserved source-owned owner `.rels` copied as-is
    // without its own EditPlan package-entry audit (for example the drawing or
    // unknown-extension owner `.rels` carried along when an ordinary media part is
    // replaced) still lands here as a Generic copy-original entry. Classify it as
    // SourceRelationships with its owning part so the output-plan snapshot exposes
    // the same source-owner context the EditPlan-managed `.rels` already carry.
    // This is read-only classification; it does not repair, prune, or invent
    // relationships, and the Generic guard leaves any already-classified entry
    // untouched.
    if (plan.copied_from_source && plan.audit_kind == PackageEntryAuditKind::Generic) {
        std::string owner_part = source_part_for_relationship_entry(reader, plan.entry_name);
        if (!owner_part.empty()) {
            plan.audit_kind = PackageEntryAuditKind::SourceRelationships;
            plan.owner_part = std::move(owner_part);
        }
    }
    plan.file_backed_source_copy_reason = source_entry_file_backed_copy_reason(reader, plan);
    plan.file_backed_source_copy = !plan.file_backed_source_copy_reason.empty();
    return plan;
}

std::filesystem::path make_package_editor_temp_path()
{
    static std::atomic<std::uint64_t> counter {0};
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto id = counter.fetch_add(1, std::memory_order_relaxed);
    return std::filesystem::temp_directory_path()
        / ("fastxlsx-package-editor-" + std::to_string(tick) + "-"
            + std::to_string(id) + ".xml");
}

void remove_temporary_files(std::vector<std::filesystem::path>& paths) noexcept
{
    for (const std::filesystem::path& path : paths) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
    paths.clear();
}

bool package_replacements_reference_path(
    const std::vector<PackagePartReplacement>& replacements,
    const std::filesystem::path& path) noexcept
{
    for (const PackagePartReplacement& replacement : replacements) {
        for (const PackageEntryChunk& chunk : replacement.chunks) {
            if (chunk.kind == PackageEntryChunk::Kind::File && chunk.path == path) {
                return true;
            }
        }
    }
    return false;
}

void retain_referenced_temporary_files(
    std::vector<std::filesystem::path>& paths,
    const std::vector<PackagePartReplacement>& replacements)
{
    std::erase_if(paths, [&](const std::filesystem::path& path) {
        return !package_replacements_reference_path(replacements, path);
    });
}

void remove_superseded_temporary_files(
    std::vector<std::filesystem::path>& previous_paths,
    const std::vector<std::filesystem::path>& current_paths) noexcept
{
    for (const std::filesystem::path& path : previous_paths) {
        if (std::find(current_paths.begin(), current_paths.end(), path)
            != current_paths.end()) {
            continue;
        }
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
    previous_paths.clear();
}

class ScopedPackageEditorTempFile {
public:
    ScopedPackageEditorTempFile()
        : path_(make_package_editor_temp_path())
    {
    }

    ~ScopedPackageEditorTempFile()
    {
        if (!released_) {
            std::error_code ignored;
            std::filesystem::remove(path_, ignored);
        }
    }

    ScopedPackageEditorTempFile(const ScopedPackageEditorTempFile&) = delete;
    ScopedPackageEditorTempFile& operator=(const ScopedPackageEditorTempFile&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

    void release() noexcept
    {
        released_ = true;
    }

private:
    std::filesystem::path path_;
    bool released_ = false;
};

#ifdef FASTXLSX_ENABLE_TEST_HOOKS
PackageEditorSourceCopyTempFilesHook& package_editor_source_copy_temp_files_hook() noexcept
{
    static PackageEditorSourceCopyTempFilesHook hook = nullptr;
    return hook;
}

PackageEditorCalcMetadataStagedHook& package_editor_calc_metadata_staged_hook() noexcept
{
    static PackageEditorCalcMetadataStagedHook hook = nullptr;
    return hook;
}

PackageEditorSheetRenameStagedHook& package_editor_sheet_rename_staged_hook() noexcept
{
    static PackageEditorSheetRenameStagedHook hook = nullptr;
    return hook;
}

PackageEditorDocumentPropertiesStagedHook&
package_editor_document_properties_staged_hook() noexcept
{
    static PackageEditorDocumentPropertiesStagedHook hook = nullptr;
    return hook;
}

PackageEditorPartRemovalStagedHook& package_editor_part_removal_staged_hook() noexcept
{
    static PackageEditorPartRemovalStagedHook hook = nullptr;
    return hook;
}

PackageEditorMaterializedPartReplacementStagedHook&
package_editor_materialized_part_replacement_staged_hook() noexcept
{
    static PackageEditorMaterializedPartReplacementStagedHook hook = nullptr;
    return hook;
}

PackageEditorChunkPartReplacementStagedHook&
package_editor_chunk_part_replacement_staged_hook() noexcept
{
    static PackageEditorChunkPartReplacementStagedHook hook = nullptr;
    return hook;
}

PackageEditorWorksheetPartReplacementStagedHook&
package_editor_worksheet_part_replacement_staged_hook() noexcept
{
    static PackageEditorWorksheetPartReplacementStagedHook hook = nullptr;
    return hook;
}

void run_package_editor_source_copy_temp_files_hook(
    std::span<const std::filesystem::path> temporary_source_files)
{
    if (auto hook = package_editor_source_copy_temp_files_hook(); hook != nullptr) {
        hook(temporary_source_files);
    }
}

void run_package_editor_calc_metadata_staged_hook()
{
    if (auto hook = package_editor_calc_metadata_staged_hook(); hook != nullptr) {
        hook();
    }
}

void run_package_editor_sheet_rename_staged_hook()
{
    if (auto hook = package_editor_sheet_rename_staged_hook(); hook != nullptr) {
        hook();
    }
}

void run_package_editor_document_properties_staged_hook()
{
    if (auto hook = package_editor_document_properties_staged_hook(); hook != nullptr) {
        hook();
    }
}

void run_package_editor_part_removal_staged_hook()
{
    if (auto hook = package_editor_part_removal_staged_hook(); hook != nullptr) {
        hook();
    }
}

void run_package_editor_materialized_part_replacement_staged_hook()
{
    if (auto hook = package_editor_materialized_part_replacement_staged_hook();
        hook != nullptr) {
        hook();
    }
}

void run_package_editor_chunk_part_replacement_staged_hook()
{
    if (auto hook = package_editor_chunk_part_replacement_staged_hook();
        hook != nullptr) {
        hook();
    }
}

void run_package_editor_worksheet_part_replacement_staged_hook()
{
    if (auto hook = package_editor_worksheet_part_replacement_staged_hook();
        hook != nullptr) {
        hook();
    }
}
#endif

void commit_package_editor_staged_state(
    PackageManifest& manifest,
    EditPlan& edit_plan,
    std::vector<PackagePartReplacement>& replacements,
    std::vector<PackageEntryReplacement>& entry_replacements,
    std::vector<std::string>& omitted_entries,
    PackageManifest& updated_manifest,
    EditPlan& updated_edit_plan,
    std::vector<PackagePartReplacement>& updated_replacements,
    std::vector<PackageEntryReplacement>& updated_entry_replacements,
    std::vector<std::string>& updated_omitted_entries) noexcept
{
    static_assert(std::is_nothrow_swappable_v<PackageManifest>);
    static_assert(std::is_nothrow_swappable_v<EditPlan>);
    static_assert(std::is_nothrow_swappable_v<std::vector<PackagePartReplacement>>);
    static_assert(std::is_nothrow_swappable_v<std::vector<PackageEntryReplacement>>);
    static_assert(std::is_nothrow_swappable_v<std::vector<std::string>>);

    using std::swap;
    swap(manifest, updated_manifest);
    swap(edit_plan, updated_edit_plan);
    swap(replacements, updated_replacements);
    swap(entry_replacements, updated_entry_replacements);
    swap(omitted_entries, updated_omitted_entries);
}

void commit_package_editor_staged_state_with_temporary_files(
    PackageManifest& manifest,
    EditPlan& edit_plan,
    std::vector<PackagePartReplacement>& replacements,
    std::vector<PackageEntryReplacement>& entry_replacements,
    std::vector<std::string>& omitted_entries,
    std::vector<std::filesystem::path>& temporary_files,
    PackageManifest& updated_manifest,
    EditPlan& updated_edit_plan,
    std::vector<PackagePartReplacement>& updated_replacements,
    std::vector<PackageEntryReplacement>& updated_entry_replacements,
    std::vector<std::string>& updated_omitted_entries,
    std::vector<std::filesystem::path>& updated_temporary_files) noexcept
{
    static_assert(std::is_nothrow_swappable_v<std::vector<std::filesystem::path>>);

    commit_package_editor_staged_state(manifest, edit_plan, replacements,
        entry_replacements, omitted_entries, updated_manifest, updated_edit_plan,
        updated_replacements, updated_entry_replacements, updated_omitted_entries);

    using std::swap;
    swap(temporary_files, updated_temporary_files);
    remove_superseded_temporary_files(updated_temporary_files, temporary_files);
}

PackageEntry materialize_active_package_entry_replacement(const PackageReader& reader,
    const PackageEntryReplacement& replacement, std::string entry_name)
{
    require_materialized_package_entry_replacement_allowed(reader, replacement.entry_name);
    return PackageEntry {std::move(entry_name), replacement.materialized_data};
}

PackageEntry materialize_active_part_replacement(const PackageReader& reader,
    const PackagePartReplacement& replacement, std::string entry_name)
{
    if (!replacement.chunks.empty()) {
        return PackageEntry {std::move(entry_name), replacement.chunks};
    }
    const PackagePart* source_part =
        reader.part_index().find_part(replacement.part_name);
    if (source_part != nullptr
        && !package_part_is_materialized_small_xml_boundary(*source_part)) {
        throw FastXlsxError(
            "source package part planned output replacement must use staged chunks, "
            "not materialized string data");
    }
    return PackageEntry {std::move(entry_name), replacement.materialized_small_xml};
}

PackageEntry materialize_planned_output_entry(const PackageReader& reader,
    const std::vector<PackagePartReplacement>& replacements,
    const std::vector<PackageEntryReplacement>& entry_replacements,
    const PackageEditorOutputEntryPlan& plan,
    std::vector<std::filesystem::path>& temporary_source_files)
{
    if (plan.omitted) {
        throw FastXlsxError("omitted package entry cannot be materialized");
    }
    if (const auto* replacement = find_replacement(replacements, plan.entry_name)) {
        return materialize_active_part_replacement(reader, *replacement, plan.entry_name);
    }
    if (const auto* entry_replacement =
            find_entry_replacement(entry_replacements, plan.entry_name)) {
        return materialize_active_package_entry_replacement(
            reader, *entry_replacement, plan.entry_name);
    }
    if (plan.source_entry) {
        try {
            if (!plan.file_backed_source_copy) {
                throw FastXlsxError(
                    "planned output source copy must use file-backed chunks");
            }
            const PackageReaderEntry* source_entry = reader.find_entry(plan.entry_name);
            if (source_entry == nullptr) {
                throw FastXlsxError("planned output source entry is missing");
            }
            if (plan.raw_compressed_source_copy) {
                return PackageEntry::raw_compressed_copy(plan.entry_name,
                    PackageRawCompressedEntrySource {
                        reader.path(),
                        source_entry->data_offset,
                        source_entry->compressed_size,
                        source_entry->uncompressed_size,
                        source_entry->crc32,
                        source_entry->compression_method,
                    });
            }
            bool use_stored_direct_range =
                source_entry->compression_method == stored_compression_method;
#ifdef FASTXLSX_ENABLE_TEST_HOOKS
            if (package_editor_source_copy_temp_files_hook() != nullptr) {
                use_stored_direct_range = false;
            }
#endif
            if (use_stored_direct_range) {
                if (source_entry->compressed_size != source_entry->uncompressed_size) {
                    throw FastXlsxError(
                        "stored planned output source entry has mismatched "
                        "compressed/uncompressed sizes");
                }
                PackageEntryChunk chunk = PackageEntryChunk::file_range(
                    reader.path(), source_entry->data_offset,
                    source_entry->uncompressed_size);
                chunk.has_expected_crc32 = true;
                chunk.expected_crc32 = source_entry->crc32;
                return PackageEntry {plan.entry_name, std::vector<PackageEntryChunk> {
                    std::move(chunk)}};
            }
            ScopedPackageEditorTempFile temp_file;
            reader.extract_entry_to_file(plan.entry_name, temp_file.path());
            std::vector<PackageEntryChunk> chunks;
            PackageEntryChunk chunk = PackageEntryChunk::file(temp_file.path());
            chunk.has_expected_size = true;
            chunk.expected_size = source_entry->uncompressed_size;
            chunk.has_expected_crc32 = true;
            chunk.expected_crc32 = source_entry->crc32;
            chunks.push_back(std::move(chunk));
            temporary_source_files.push_back(temp_file.path());
            temp_file.release();
            return PackageEntry {plan.entry_name, std::move(chunks)};
        } catch (const std::exception& error) {
            throw FastXlsxError("failed to copy source package entry '" + plan.entry_name
                + "': " + error.what());
        }
    }
    throw FastXlsxError("planned output entry has no payload: " + plan.entry_name);
}

std::string planned_output_entry_materialization_kind(
    const PackageEditorOutputEntryPlan& plan)
{
    if (plan.source_entry) {
        return "source-copy";
    }
    if (plan.staged_replacement_chunks) {
        return "staged-replacement";
    }
    if (plan.materialized_replacement) {
        return "materialized-replacement";
    }
    if (plan.generated) {
        return "generated";
    }
    return "payload";
}

PackageEntry materialize_planned_output_entry_with_context(const PackageReader& reader,
    const std::vector<PackagePartReplacement>& replacements,
    const std::vector<PackageEntryReplacement>& entry_replacements,
    const PackageEditorOutputEntryPlan& plan,
    std::vector<std::filesystem::path>& temporary_source_files)
{
    try {
        return materialize_planned_output_entry(
            reader, replacements, entry_replacements, plan, temporary_source_files);
    } catch (const std::exception& error) {
        throw FastXlsxError("failed to materialize planned output "
            + planned_output_entry_materialization_kind(plan) + " entry '"
            + plan.entry_name + "': " + error.what());
    }
}

bool same_existing_path(
    const std::filesystem::path& left, const std::filesystem::path& right) noexcept
{
    std::error_code error;
    const bool same = std::filesystem::equivalent(left, right, error);
    return !error && same;
}

bool path_is_existing_directory(const std::filesystem::path& path) noexcept
{
    std::error_code error;
    const bool directory = std::filesystem::is_directory(path, error);
    return !error && directory;
}

bool path_parent_is_not_directory(const std::filesystem::path& path) noexcept
{
    const std::filesystem::path parent = path.parent_path();
    if (parent.empty()) {
        return false;
    }

    std::error_code error;
    const bool directory = std::filesystem::is_directory(parent, error);
    return error || !directory;
}

std::filesystem::path make_package_editor_output_sibling_path(
    const std::filesystem::path& output_path, std::string_view tag)
{
    if (output_path.empty()) {
        throw FastXlsxError("PackageEditor output path cannot be empty");
    }

    for (std::uint32_t attempt = 0; attempt != 1024U; ++attempt) {
        std::filesystem::path candidate = output_path;
        candidate += ".fastxlsx-";
        candidate += std::string(tag);
        candidate += "-";
        candidate += std::to_string(attempt);

        std::error_code error;
        const bool exists = std::filesystem::exists(candidate, error);
        if (error) {
            throw FastXlsxError(
                "failed to inspect PackageEditor temporary output path");
        }
        if (!exists) {
            return candidate;
        }
    }

    throw FastXlsxError("failed to allocate PackageEditor temporary output path");
}

void remove_package_editor_output_file_noexcept(
    const std::filesystem::path& path) noexcept
{
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

void commit_package_editor_output_file(
    const std::filesystem::path& temporary_path, const std::filesystem::path& output_path)
{
    std::error_code error;
    const bool output_exists = std::filesystem::exists(output_path, error);
    if (error) {
        throw FastXlsxError("failed to inspect PackageEditor output path");
    }

    std::filesystem::path backup_path;
    bool has_backup = false;
    if (output_exists) {
        backup_path = make_package_editor_output_sibling_path(output_path, "backup");
        std::filesystem::rename(output_path, backup_path, error);
        if (error) {
            throw FastXlsxError("failed to move existing PackageEditor output aside");
        }
        has_backup = true;
    }

    error.clear();
    std::filesystem::rename(temporary_path, output_path, error);
    if (error) {
        if (has_backup) {
            std::error_code restore_error;
            std::filesystem::rename(backup_path, output_path, restore_error);
            if (restore_error) {
                throw FastXlsxError(
                    "failed to commit PackageEditor output and failed to restore previous output");
            }
        }
        throw FastXlsxError("failed to commit PackageEditor output");
    }

    if (has_backup) {
        remove_package_editor_output_file_noexcept(backup_path);
    }
}

class PackageEntryChunkReader {
public:
    explicit PackageEntryChunkReader(const std::vector<PackageEntryChunk>& chunks)
        : chunks_(chunks)
    {
        expected_sizes_.reserve(chunks_.size());
        expected_crcs_.reserve(chunks_.size());
        for (std::size_t chunk_index = 0; chunk_index < chunks_.size(); ++chunk_index) {
            const PackageEntryChunk& chunk = chunks_[chunk_index];
            if (chunk.kind != PackageEntryChunk::Kind::Memory
                && chunk.kind != PackageEntryChunk::Kind::File) {
                expected_sizes_.push_back(0);
                expected_crcs_.push_back(0);
                continue;
            }
            expected_sizes_.push_back(
                chunk.has_expected_size
                    ? chunk.expected_size
                    : package_entry_chunk_size_with_context(chunk, chunk_index));
            expected_crcs_.push_back(
                chunk.has_expected_crc32
                    ? chunk.expected_crc32
                    : package_entry_chunk_crc32_with_context(chunk, chunk_index));
            record_expected_size(expected_sizes_.back());
        }
    }

    ~PackageEntryChunkReader()
    {
        close_current_file_noexcept();
    }

    bool operator()(std::string& output_chunk)
    {
        output_chunk.clear();
        ++replay_read_attempts_;
        while (chunk_index_ < chunks_.size()) {
            const PackageEntryChunk& chunk = chunks_[chunk_index_];
            switch (chunk.kind) {
            case PackageEntryChunk::Kind::Memory:
                reset_file_read_progress();
                if (!chunk.path.empty()) {
                    replay_error(
                        staged_package_entry_chunk_context(chunk, chunk_index_)
                        + ": staged package-entry chunk cannot mix memory and file sources");
                }
                if (chunk.has_file_range) {
                    replay_error(
                        staged_package_entry_chunk_context(chunk, chunk_index_)
                        + ": staged package-entry memory chunk cannot carry a file range");
                }
                ++chunk_index_;
                try {
                    const std::size_t completed_chunk_index = chunk_index_ - 1;
                    require_staged_chunk_size_unchanged(
                        expected_sizes_[completed_chunk_index],
                        static_cast<std::uint64_t>(chunk.data.size()));
                    require_staged_chunk_crc32_unchanged(
                        expected_crcs_[completed_chunk_index], package_editor_crc32(chunk.data));
                } catch (const std::exception& error) {
                    replay_error(
                        staged_package_entry_chunk_context(chunk, chunk_index_ - 1)
                            + ": " + error.what(),
                        chunk_index_ - 1);
                }
                if (chunk.data.empty()) {
                    continue;
                }
                output_chunk = chunk.data;
                record_replayed_chunk(output_chunk);
                return true;
            case PackageEntryChunk::Kind::File:
                if (!chunk.data.empty()) {
                    replay_error(
                        staged_package_entry_chunk_context(chunk, chunk_index_)
                        + ": staged package-entry chunk cannot mix memory and file sources");
                }
                const std::uint64_t expected_size = expected_sizes_[chunk_index_];
                if (!input_.is_open()) {
                    reset_file_read_progress();
                    file_crc_.reset();
                    input_.clear();
                    input_.open(chunk.path, std::ios::binary);
                    if (!input_) {
                        replay_error(
                            staged_package_entry_chunk_context(chunk, chunk_index_)
                            + ": failed to open staged package-entry chunk file; expected "
                            + std::to_string(expected_size) + " bytes during "
                            + staged_file_read_progress_description());
                    }
                    try {
                        seek_package_entry_file_chunk_start(input_, chunk);
                    } catch (const std::exception& error) {
                        replay_error(
                            staged_package_entry_chunk_context(chunk, chunk_index_)
                            + ": " + error.what() + " during "
                            + staged_file_read_progress_description());
                    }
                }

                if (file_bytes_read_ == expected_size) {
                    ++file_read_attempts_;
                    if (!chunk.has_file_range) {
                        const int next = input_.peek();
                        if (next != std::char_traits<char>::eof()) {
                            replay_error(
                                staged_package_entry_chunk_context(chunk, chunk_index_)
                                + ": "
                                + expected_at_least_size_message(
                                    "staged package-entry chunk file produced more bytes than expected",
                                    expected_size)
                                + " during " + staged_file_read_progress_description());
                        }
                        if (input_.bad()) {
                            replay_error(
                                staged_package_entry_chunk_context(chunk, chunk_index_)
                                + ": failed to read staged package-entry chunk file during "
                                + staged_file_read_progress_description());
                        }
                    }
                    try {
                        require_staged_chunk_crc32_unchanged(
                            expected_crcs_[chunk_index_], file_crc_.value());
                    } catch (const std::exception& error) {
                        replay_error(
                            staged_package_entry_chunk_context(chunk, chunk_index_)
                            + ": " + error.what() + " during "
                            + staged_file_read_progress_description());
                    }
                    close_current_file_noexcept();
                    ++chunk_index_;
                    reset_file_read_progress();
                    continue;
                }

                const std::uint64_t remaining = expected_size - file_bytes_read_;
                const std::size_t requested = static_cast<std::size_t>(
                    std::min<std::uint64_t>(
                        remaining, static_cast<std::uint64_t>(package_entry_reader_chunk_size)));
                output_chunk.resize(requested);
                ++file_read_attempts_;
                input_.read(output_chunk.data(),
                    static_cast<std::streamsize>(output_chunk.size()));
                const std::streamsize read_size = input_.gcount();
                if (input_.bad()) {
                    replay_error(
                        staged_package_entry_chunk_context(chunk, chunk_index_)
                        + ": failed to read staged package-entry chunk file during "
                        + staged_file_read_progress_description());
                }
                if (read_size <= 0
                    || static_cast<std::uint64_t>(read_size)
                        != static_cast<std::uint64_t>(requested)) {
                    replay_error(
                        staged_package_entry_chunk_context(chunk, chunk_index_)
                        + ": "
                        + expected_actual_size_message(
                            "staged package-entry chunk file ended before expected bytes",
                            expected_size,
                            file_bytes_read_ + static_cast<std::uint64_t>(
                                                   std::max<std::streamsize>(read_size, 0)))
                        + " during " + staged_file_read_progress_description());
                }
                output_chunk.resize(static_cast<std::size_t>(read_size));
                file_crc_.update(output_chunk);
                file_bytes_read_ += static_cast<std::uint64_t>(read_size);
                record_replayed_chunk(output_chunk);
                return true;
            }

            replay_error(staged_package_entry_chunk_context(chunk, chunk_index_)
                + ": unsupported staged package-entry chunk kind");
        }
        return false;
    }

private:
    void record_expected_size(std::uint64_t expected_size) noexcept
    {
        if (expected_size
            > std::numeric_limits<std::uint64_t>::max() - expected_total_bytes_) {
            expected_total_bytes_overflow_ = true;
        } else if (!expected_total_bytes_overflow_) {
            expected_total_bytes_ += expected_size;
        }
    }

    [[nodiscard]] std::string expected_payload_description() const
    {
        std::string text = "expected staged payload total ";
        if (expected_total_bytes_overflow_) {
            text += "overflow";
        } else {
            text += std::to_string(expected_total_bytes_);
        }
        text += " bytes";
        return text;
    }

    [[nodiscard]] std::string expected_payload_remaining_description() const
    {
        std::string text = "expected staged payload remaining ";
        if (expected_total_bytes_overflow_ || replayed_bytes_overflow_) {
            text += "unknown";
        } else {
            const std::uint64_t remaining =
                expected_total_bytes_ > replayed_bytes_
                    ? expected_total_bytes_ - replayed_bytes_
                    : 0;
            text += std::to_string(remaining);
        }
        text += " bytes";
        return text;
    }

    [[nodiscard]] std::string replay_chunk_cursor_description(
        std::size_t failing_chunk_index) const
    {
        return "staged package-entry chunk index "
            + std::to_string(failing_chunk_index) + " of "
            + std::to_string(chunks_.size());
    }

    [[nodiscard]] std::string current_chunk_expected_size_description(
        std::size_t failing_chunk_index) const
    {
        std::string text = "expected chunk ";
        if (failing_chunk_index < expected_sizes_.size()) {
            text += std::to_string(expected_sizes_[failing_chunk_index]);
        } else {
            text += "unknown";
        }
        text += " bytes";
        return text;
    }

    [[nodiscard]] std::string replay_progress_description() const
    {
        std::string text = "staged package-entry chunk replay read attempt "
            + std::to_string(replay_read_attempts_) + " after emitting "
            + std::to_string(replayed_chunks_) + " chunk";
        if (replayed_chunks_ != 1) {
            text += "s";
        }
        text += " and ";
        if (replayed_bytes_overflow_) {
            text += "overflow";
        } else {
            text += std::to_string(replayed_bytes_);
        }
        text += " bytes";
        text += "; ";
        text += expected_payload_description();
        text += "; ";
        text += expected_payload_remaining_description();
        if (replayed_chunks_ > 0) {
            text += "; last chunk ";
            text += std::to_string(last_replayed_chunk_bytes_);
            text += " bytes";
        }
        return text;
    }

    [[noreturn]] void replay_error(std::string detail) const
    {
        replay_error(std::move(detail), chunk_index_);
    }

    [[noreturn]] void replay_error(
        std::string detail, std::size_t failing_chunk_index) const
    {
        throw FastXlsxError(
            std::move(detail) + " at "
            + replay_chunk_cursor_description(failing_chunk_index)
            + "; " + current_chunk_expected_size_description(failing_chunk_index)
            + " during " + replay_progress_description());
    }

    void record_replayed_chunk(std::string_view output_chunk)
    {
        ++replayed_chunks_;
        last_replayed_chunk_bytes_ = static_cast<std::uint64_t>(output_chunk.size());
        if (output_chunk.size()
            > std::numeric_limits<std::uint64_t>::max() - replayed_bytes_) {
            replayed_bytes_overflow_ = true;
        } else if (!replayed_bytes_overflow_) {
            replayed_bytes_ += static_cast<std::uint64_t>(output_chunk.size());
        }
    }

    [[nodiscard]] std::string staged_file_read_progress_description() const
    {
        return "staged package-entry file chunk read attempt "
            + std::to_string(file_read_attempts_) + " after reading "
            + std::to_string(file_bytes_read_) + " bytes";
    }

    void reset_file_read_progress() noexcept
    {
        file_bytes_read_ = 0;
        file_read_attempts_ = 0;
    }

    void close_current_file_noexcept() noexcept
    {
        if (input_.is_open()) {
            input_.close();
        }
        input_.clear();
    }

    const std::vector<PackageEntryChunk>& chunks_;
    std::vector<std::uint64_t> expected_sizes_;
    std::vector<std::uint32_t> expected_crcs_;
    std::size_t chunk_index_ = 0;
    std::uint64_t file_bytes_read_ = 0;
    std::size_t file_read_attempts_ = 0;
    std::size_t replay_read_attempts_ = 0;
    std::size_t replayed_chunks_ = 0;
    std::uint64_t replayed_bytes_ = 0;
    std::uint64_t last_replayed_chunk_bytes_ = 0;
    std::uint64_t expected_total_bytes_ = 0;
    bool replayed_bytes_overflow_ = false;
    bool expected_total_bytes_overflow_ = false;
    PackageEditorCrc32Accumulator file_crc_;
    std::ifstream input_;
};

using PackageEntryChunkRangeCallback = std::function<void(std::string_view)>;

struct PackageEntryChunkRangeLayout {
    std::vector<std::uint64_t> sizes;
    std::uint64_t total_size = 0;
};

PackageEntryChunkRangeLayout package_entry_chunk_range_layout(
    const std::vector<PackageEntryChunk>& chunks)
{
    PackageEntryChunkRangeLayout layout;
    layout.sizes.reserve(chunks.size());
    for (std::size_t chunk_index = 0; chunk_index < chunks.size(); ++chunk_index) {
        const std::uint64_t size =
            package_entry_chunk_size_with_context(chunks[chunk_index], chunk_index);
        if (size > std::numeric_limits<std::uint64_t>::max() - layout.total_size) {
            throw FastXlsxError("staged package-entry chunk range source is too large");
        }
        layout.sizes.push_back(size);
        layout.total_size += size;
    }
    return layout;
}

std::string package_entry_chunk_range_description(
    std::uint64_t offset, std::uint64_t size, std::uint64_t total_size)
{
    return "offset " + std::to_string(offset)
        + ", length " + std::to_string(size)
        + ", total " + std::to_string(total_size);
}

void emit_file_chunk_range(
    const PackageEntryChunk& chunk,
    std::size_t chunk_index,
    std::uint64_t offset,
    std::uint64_t size,
    const PackageEntryChunkRangeCallback& callback)
{
    if (size == 0) {
        return;
    }
    const std::uint64_t base_offset = package_entry_file_chunk_payload_offset(chunk);
    if (offset > std::numeric_limits<std::uint64_t>::max() - base_offset) {
        throw FastXlsxError(staged_package_entry_chunk_context(chunk, chunk_index)
            + ": staged package-entry chunk file range offset overflow");
    }
    const std::uint64_t absolute_offset = base_offset + offset;
    if (absolute_offset > static_cast<std::uint64_t>(
            std::numeric_limits<std::streamoff>::max())) {
        throw FastXlsxError(staged_package_entry_chunk_context(chunk, chunk_index)
            + ": staged package-entry chunk file range offset is too large");
    }

    std::ifstream input(chunk.path, std::ios::binary);
    if (!input) {
        throw FastXlsxError(staged_package_entry_chunk_context(chunk, chunk_index)
            + ": failed to open staged package-entry chunk file for range read");
    }
    input.seekg(static_cast<std::streamoff>(absolute_offset), std::ios::beg);
    if (!input) {
        throw FastXlsxError(staged_package_entry_chunk_context(chunk, chunk_index)
            + ": failed to seek staged package-entry chunk file range to offset "
            + std::to_string(absolute_offset));
    }

    std::vector<char> buffer(package_entry_file_chunk_size);
    std::uint64_t remaining = size;
    std::uint64_t emitted = 0;
    while (remaining > 0) {
        const std::size_t requested = static_cast<std::size_t>(
            std::min<std::uint64_t>(
                remaining, static_cast<std::uint64_t>(buffer.size())));
        input.read(buffer.data(), static_cast<std::streamsize>(requested));
        const std::streamsize read_size = input.gcount();
        if (input.bad()) {
            throw FastXlsxError(staged_package_entry_chunk_context(chunk, chunk_index)
                + ": failed to read staged package-entry chunk file range");
        }
        if (read_size <= 0
            || static_cast<std::uint64_t>(read_size)
                != static_cast<std::uint64_t>(requested)) {
            throw FastXlsxError(staged_package_entry_chunk_context(chunk, chunk_index)
                + ": "
                + expected_actual_size_message(
                    "staged package-entry chunk file range ended before requested bytes",
                    size,
                    emitted + static_cast<std::uint64_t>(
                                  std::max<std::streamsize>(read_size, 0)))
                + " at local offset " + std::to_string(offset));
        }

        const std::string_view output(
            buffer.data(), static_cast<std::size_t>(read_size));
        callback(output);
        emitted += static_cast<std::uint64_t>(read_size);
        remaining -= static_cast<std::uint64_t>(read_size);
    }
}

void emit_package_entry_chunk_range_with_layout(
    const std::vector<PackageEntryChunk>& chunks,
    const PackageEntryChunkRangeLayout& layout,
    std::uint64_t offset,
    std::uint64_t size,
    const PackageEntryChunkRangeCallback& callback)
{
    if (offset > layout.total_size
        || size > layout.total_size - offset) {
        throw FastXlsxError(
            "staged package-entry chunk range exceeds staged payload size: "
            + package_entry_chunk_range_description(offset, size, layout.total_size));
    }
    if (size == 0) {
        return;
    }

    const std::uint64_t range_end = offset + size;
    std::uint64_t chunk_start = 0;
    for (std::size_t chunk_index = 0; chunk_index < chunks.size(); ++chunk_index) {
        const std::uint64_t chunk_size = layout.sizes[chunk_index];
        const std::uint64_t chunk_end = chunk_start + chunk_size;
        if (chunk_end <= offset) {
            chunk_start = chunk_end;
            continue;
        }
        if (chunk_start >= range_end) {
            break;
        }

        const std::uint64_t overlap_start = std::max(offset, chunk_start);
        const std::uint64_t overlap_end = std::min(range_end, chunk_end);
        const std::uint64_t local_offset = overlap_start - chunk_start;
        const std::uint64_t local_size = overlap_end - overlap_start;
        const PackageEntryChunk& chunk = chunks[chunk_index];
        switch (chunk.kind) {
        case PackageEntryChunk::Kind::Memory: {
            const auto begin = static_cast<std::size_t>(local_offset);
            const auto length = static_cast<std::size_t>(local_size);
            callback(std::string_view(chunk.data.data() + begin, length));
            break;
        }
        case PackageEntryChunk::Kind::File:
            emit_file_chunk_range(chunk, chunk_index, local_offset, local_size, callback);
            break;
        default:
            throw FastXlsxError(staged_package_entry_chunk_context(chunk, chunk_index)
                + ": unsupported staged package-entry chunk kind");
        }

        chunk_start = chunk_end;
    }
}

void emit_package_entry_chunk_range(
    const std::vector<PackageEntryChunk>& chunks,
    std::uint64_t offset,
    std::uint64_t size,
    const PackageEntryChunkRangeCallback& callback)
{
    const PackageEntryChunkRangeLayout layout =
        package_entry_chunk_range_layout(chunks);
    emit_package_entry_chunk_range_with_layout(chunks, layout, offset, size, callback);
}

struct PackageEntryIndexedCellReplacementPlan {
    PackageEntryChunkRangeLayout source_layout;
    std::vector<WorksheetIndexedCellRewrite> rewrites;
};

PackageEntryIndexedCellReplacementPlan plan_indexed_cell_replacement_from_chunks(
    const std::vector<PackageEntryChunk>& source_chunks,
    std::span<const WorksheetIndexedCellRewrite> rewrites,
    const WorksheetCellReplacementPlan& replacement_plan)
{
    PackageEntryIndexedCellReplacementPlan plan;
    plan.rewrites.assign(rewrites.begin(), rewrites.end());
    plan.source_layout = package_entry_chunk_range_layout(source_chunks);

    std::uint64_t cursor = 0;
    for (const WorksheetIndexedCellRewrite& rewrite : plan.rewrites) {
        if (rewrite.source_range.end_offset < rewrite.source_range.start_offset) {
            throw FastXlsxError(
                "indexed package-entry replacement source range is invalid");
        }
        if (rewrite.source_range.start_offset < cursor) {
            throw FastXlsxError(
                "indexed package-entry replacement source ranges are not ordered");
        }
        if (rewrite.source_range.end_offset > plan.source_layout.total_size) {
            throw FastXlsxError(
                "indexed package-entry replacement source range exceeds staged chunks: "
                + package_entry_chunk_range_description(
                    rewrite.source_range.start_offset,
                    rewrite.source_range.end_offset
                        - rewrite.source_range.start_offset,
                    plan.source_layout.total_size));
        }
        const auto replacement =
            replacement_plan.replacement_payloads_by_reference.find(
                std::string_view(rewrite.cell_reference));
        if (replacement == replacement_plan.replacement_payloads_by_reference.end()) {
            throw FastXlsxError(
                "indexed package-entry replacement plan lost target cell: "
                + rewrite.cell_reference);
        }
        cursor = rewrite.source_range.end_offset;
    }

    return plan;
}

void add_indexed_chunk_output_bytes(std::uint64_t& total, std::uint64_t size)
{
    if (size > std::numeric_limits<std::uint64_t>::max() - total) {
        throw FastXlsxError("indexed package-entry replacement output is too large");
    }
    total += size;
}

void append_indexed_memory_output_chunk(
    std::vector<PackageEntryChunk>& output_chunks,
    std::string_view data)
{
    if (data.empty()) {
        return;
    }

    if (!output_chunks.empty()) {
        PackageEntryChunk& previous = output_chunks.back();
        if (previous.kind == PackageEntryChunk::Kind::Memory
            && previous.path.empty()
            && !previous.has_file_range) {
            previous.data.append(data.data(), data.size());
            previous.expected_size = static_cast<std::uint64_t>(previous.data.size());
            previous.has_expected_size = true;
            previous.expected_crc32 = package_editor_crc32(previous.data);
            previous.has_expected_crc32 = true;
            return;
        }
    }

    PackageEntryChunk output_chunk = PackageEntryChunk::memory(std::string(data));
    output_chunk.expected_crc32 = package_editor_crc32(output_chunk.data);
    output_chunk.has_expected_crc32 = true;
    output_chunks.push_back(std::move(output_chunk));
}

void append_package_entry_chunk_range_descriptors_with_layout(
    std::vector<PackageEntryChunk>& output_chunks,
    std::uint64_t& output_bytes,
    const std::vector<PackageEntryChunk>& source_chunks,
    const PackageEntryChunkRangeLayout& layout,
    std::uint64_t offset,
    std::uint64_t size)
{
    if (offset > layout.total_size
        || size > layout.total_size - offset) {
        throw FastXlsxError(
            "staged package-entry chunk descriptor range exceeds staged payload size: "
            + package_entry_chunk_range_description(offset, size, layout.total_size));
    }
    if (size == 0) {
        return;
    }

    const std::uint64_t range_end = offset + size;
    std::uint64_t chunk_start = 0;
    for (std::size_t chunk_index = 0; chunk_index < source_chunks.size(); ++chunk_index) {
        const std::uint64_t chunk_size = layout.sizes[chunk_index];
        const std::uint64_t chunk_end = chunk_start + chunk_size;
        if (chunk_end <= offset) {
            chunk_start = chunk_end;
            continue;
        }
        if (chunk_start >= range_end) {
            break;
        }

        const std::uint64_t overlap_start = std::max(offset, chunk_start);
        const std::uint64_t overlap_end = std::min(range_end, chunk_end);
        const std::uint64_t local_offset = overlap_start - chunk_start;
        const std::uint64_t local_size = overlap_end - overlap_start;
        const PackageEntryChunk& source_chunk = source_chunks[chunk_index];

        switch (source_chunk.kind) {
        case PackageEntryChunk::Kind::Memory: {
            const auto begin = static_cast<std::size_t>(local_offset);
            const auto length = static_cast<std::size_t>(local_size);
            append_indexed_memory_output_chunk(
                output_chunks, std::string_view(source_chunk.data.data() + begin, length));
            add_indexed_chunk_output_bytes(output_bytes, local_size);
            break;
        }
        case PackageEntryChunk::Kind::File: {
            const std::uint64_t base_offset =
                package_entry_file_chunk_payload_offset(source_chunk);
            if (local_offset > std::numeric_limits<std::uint64_t>::max() - base_offset) {
                throw FastXlsxError(staged_package_entry_chunk_context(
                    source_chunk, chunk_index)
                    + ": indexed package-entry descriptor file range offset overflow");
            }
            PackageEntryChunk output_chunk = PackageEntryChunk::file_range(
                source_chunk.path, base_offset + local_offset, local_size);
            output_chunks.push_back(std::move(output_chunk));
            add_indexed_chunk_output_bytes(output_bytes, local_size);
            break;
        }
        default:
            throw FastXlsxError(staged_package_entry_chunk_context(source_chunk, chunk_index)
                + ": unsupported staged package-entry chunk kind");
        }

        chunk_start = chunk_end;
    }
}

void append_replacement_payload_descriptors(
    std::vector<PackageEntryChunk>& output_chunks,
    std::uint64_t& output_bytes,
    const WorksheetCellReplacementPayload& payload)
{
    payload.for_each_chunk([&](std::string_view chunk) {
        append_indexed_memory_output_chunk(output_chunks, chunk);
        add_indexed_chunk_output_bytes(
            output_bytes, static_cast<std::uint64_t>(chunk.size()));
    });
}

WorksheetTransformSummary emit_indexed_cell_replacement_from_package_entry_chunks_impl(
    const std::vector<PackageEntryChunk>& source_chunks,
    std::span<const WorksheetIndexedCellRewrite> rewrites,
    const WorksheetCellReplacementPlan& replacement_plan,
    const WorksheetOutputChunkCallback& callback)
{
    if (!callback) {
        throw FastXlsxError(
            "indexed package-entry replacement output emitter requires a callback");
    }

    const PackageEntryIndexedCellReplacementPlan plan =
        plan_indexed_cell_replacement_from_chunks(
            source_chunks, rewrites, replacement_plan);

    std::uint64_t cursor = 0;
    for (const WorksheetIndexedCellRewrite& rewrite : plan.rewrites) {
        const std::uint64_t pass_through_size =
            rewrite.source_range.start_offset - cursor;
        emit_package_entry_chunk_range_with_layout(
            source_chunks, plan.source_layout, cursor, pass_through_size, callback);

        const auto replacement =
            replacement_plan.replacement_payloads_by_reference.find(
                std::string_view(rewrite.cell_reference));
        replacement->second.for_each_chunk(
            [&](std::string_view chunk) { callback(chunk); });
        cursor = rewrite.source_range.end_offset;
    }

    emit_package_entry_chunk_range_with_layout(
        source_chunks,
        plan.source_layout,
        cursor,
        plan.source_layout.total_size - cursor,
        callback);

    WorksheetTransformSummary summary;
    summary.matched_replacement_count = plan.rewrites.size();
    return summary;
}

WorksheetTransformSummary emit_indexed_cell_replacement_from_package_entry_chunks_impl(
    const std::vector<PackageEntryChunk>& source_chunks,
    const WorksheetCellIndex& index,
    const WorksheetCellReplacementPlan& replacement_plan,
    const WorksheetOutputChunkCallback& callback)
{
    std::vector<std::string_view> target_references;
    target_references.reserve(replacement_plan.replacement_payloads_by_reference.size());
    for (const auto& replacement : replacement_plan.replacement_payloads_by_reference) {
        target_references.push_back(replacement.first);
    }
    const std::vector<WorksheetIndexedCellRewrite> rewrites =
        plan_indexed_cell_rewrites(index, target_references);
    return emit_indexed_cell_replacement_from_package_entry_chunks_impl(
        source_chunks, rewrites, replacement_plan, callback);
}

WorksheetInputChunkCallback package_entry_chunk_source(
    const PackageReader& reader, std::string_view entry_name)
{
    PackageReaderChunkCallback source = reader.entry_chunk_source(entry_name);
    return [source = std::move(source)](std::string& output_chunk) mutable {
        return source(output_chunk);
    };
}

std::string planned_chunk_composition_description(
    const std::vector<PackageEntryChunk>& chunks)
{
    std::size_t memory_chunks = 0;
    std::size_t file_chunks = 0;
    std::size_t unknown_chunks = 0;
    std::uint64_t expected_bytes = 0;
    std::size_t chunks_without_expected_size = 0;
    bool expected_bytes_overflow = false;
    for (const PackageEntryChunk& chunk : chunks) {
        switch (chunk.kind) {
        case PackageEntryChunk::Kind::Memory:
            ++memory_chunks;
            break;
        case PackageEntryChunk::Kind::File:
            ++file_chunks;
            break;
        default:
            ++unknown_chunks;
            break;
        }
        if (chunk.has_expected_size) {
            if (!expected_bytes_overflow && chunk.expected_size
                    > std::numeric_limits<std::uint64_t>::max() - expected_bytes) {
                expected_bytes_overflow = true;
            } else if (!expected_bytes_overflow) {
                expected_bytes += chunk.expected_size;
            }
        } else {
            ++chunks_without_expected_size;
        }
    }

    std::string description = std::to_string(chunks.size()) + " chunks; memory="
        + std::to_string(memory_chunks) + ", file=" + std::to_string(file_chunks);
    if (unknown_chunks > 0) {
        description += ", unknown=" + std::to_string(unknown_chunks);
    }
    if (expected_bytes_overflow) {
        description += ", expected_bytes=overflow";
    } else {
        description += ", expected_bytes=" + std::to_string(expected_bytes);
    }
    if (chunks_without_expected_size > 0) {
        description += ", chunks_without_expected_size="
            + std::to_string(chunks_without_expected_size);
    }
    return description;
}

std::string current_worksheet_input_source_description(
    const PartName& worksheet_part, const CurrentWorksheetInputSource& source)
{
    switch (source.kind) {
    case CurrentWorksheetInputSourceKind::SourceEntry:
        return source_worksheet_entry_description(worksheet_part);
    case CurrentWorksheetInputSourceKind::PlannedChunks:
    {
        std::string description =
            "planned worksheet staged chunks for '" + worksheet_part.zip_path() + "'";
        if (source.planned_chunks != nullptr) {
            description += " ("
                + planned_chunk_composition_description(*source.planned_chunks) + ")";
        }
        description += " for " + worksheet_part_diagnostic_context(worksheet_part);
        return description;
    }
    }

    return "unknown current worksheet input source for '" + worksheet_part.zip_path() + "'";
}

class CurrentWorksheetInputChunkReader {
public:
    CurrentWorksheetInputChunkReader(const PackageReader& reader,
        const PartName& worksheet_part,
        const CurrentWorksheetInputSource& source,
        std::string purpose)
        : purpose_(std::move(purpose))
        , source_description_(
              current_worksheet_input_source_description(worksheet_part, source))
    {
        try {
            switch (source.kind) {
            case CurrentWorksheetInputSourceKind::SourceEntry:
                source_entry_reader_ =
                    package_entry_chunk_source(reader, worksheet_part.zip_path());
                return;
            case CurrentWorksheetInputSourceKind::PlannedChunks:
                if (source.planned_chunks == nullptr) {
                    throw FastXlsxError("current worksheet planned chunk source is missing");
                }
                planned_chunk_reader_.emplace(*source.planned_chunks);
                return;
            }

            throw FastXlsxError("unsupported current worksheet input source kind");
        } catch (const std::exception& error) {
            throw FastXlsxError(
                "failed to initialize " + purpose_ + " from "
                + source_description_ + ": " + error.what());
        }
    }

    bool operator()(std::string& output_chunk)
    {
        ++read_attempts_;
        try {
            bool has_chunk = false;
            if (source_entry_reader_) {
                has_chunk = source_entry_reader_(output_chunk);
            } else if (planned_chunk_reader_) {
                has_chunk = (*planned_chunk_reader_)(output_chunk);
            } else {
                throw FastXlsxError("current worksheet input chunk reader is not initialized");
            }
            if (has_chunk) {
                ++emitted_chunks_;
                last_emitted_chunk_bytes_ =
                    static_cast<std::uint64_t>(output_chunk.size());
                if (output_chunk.size()
                    > std::numeric_limits<std::uint64_t>::max() - emitted_bytes_) {
                    emitted_bytes_overflow_ = true;
                } else if (!emitted_bytes_overflow_) {
                    emitted_bytes_ += static_cast<std::uint64_t>(output_chunk.size());
                }
            }
            return has_chunk;
        } catch (const std::exception& error) {
            throw FastXlsxError(
                "failed to read " + purpose_ + " from "
                + source_description_ + " during current-input read attempt "
                + std::to_string(read_attempts_) + " after "
                + emitted_progress_description() + ": " + error.what());
        }
    }

private:
    [[nodiscard]] std::string emitted_progress_description() const
    {
        std::string description = "emitting " + std::to_string(emitted_chunks_)
            + " current-input chunk";
        if (emitted_chunks_ != 1) {
            description += "s";
        }
        description += " and ";
        if (emitted_bytes_overflow_) {
            description += "overflow";
        } else {
            description += std::to_string(emitted_bytes_);
        }
        description += " bytes";
        if (emitted_chunks_ > 0) {
            description += "; last chunk ";
            description += std::to_string(last_emitted_chunk_bytes_);
            description += " bytes";
        }
        return description;
    }

    WorksheetInputChunkCallback source_entry_reader_;
    std::optional<PackageEntryChunkReader> planned_chunk_reader_;
    std::string purpose_;
    std::string source_description_;
    std::size_t read_attempts_ = 0;
    std::size_t emitted_chunks_ = 0;
    std::uint64_t emitted_bytes_ = 0;
    std::uint64_t last_emitted_chunk_bytes_ = 0;
    bool emitted_bytes_overflow_ = false;
};

void validate_worksheet_replacement_preconditions(const PackageManifest& manifest,
    const PackageReader& reader,
    const PartName& worksheet_part,
    const ReferencePolicy& policy)
{
    const PackagePart* worksheet = manifest.find_part(worksheet_part);
    if (worksheet == nullptr) {
        worksheet = reader.part_index().find_part(worksheet_part);
    }
    if (worksheet == nullptr) {
        throw FastXlsxError(
            "worksheet replacement target is not present in the source package for "
            + worksheet_part_diagnostic_context(worksheet_part));
    }
    if (worksheet->content_type != content_type_worksheet) {
        throw FastXlsxError("worksheet replacement target is not a worksheet part for "
            + worksheet_part_diagnostic_context(worksheet_part));
    }
    if (policy.calc_chain_action == CalcChainAction::Rebuild) {
        throw FastXlsxError("calcChain rebuild is not implemented for worksheet replacement");
    }

    const PartName workbook_part = reader.workbook_part();
    if (manifest.find_part(workbook_part) == nullptr
        || reader.find_entry(workbook_part.zip_path()) == nullptr) {
        throw FastXlsxError("worksheet replacement requires the officeDocument workbook part");
    }
}

} // namespace

WorksheetTransformSummary emit_indexed_cell_replacement_from_package_entry_chunks(
    const std::vector<PackageEntryChunk>& source_chunks,
    const WorksheetCellIndex& index,
    const WorksheetCellReplacementPlan& replacement_plan,
    const WorksheetOutputChunkCallback& callback)
{
    return emit_indexed_cell_replacement_from_package_entry_chunks_impl(
        source_chunks, index, replacement_plan, callback);
}

WorksheetTransformSummary emit_indexed_cell_replacement_from_package_entry_chunks(
    const std::vector<PackageEntryChunk>& source_chunks,
    std::span<const WorksheetIndexedCellRewrite> rewrites,
    const WorksheetCellReplacementPlan& replacement_plan,
    const WorksheetOutputChunkCallback& callback)
{
    return emit_indexed_cell_replacement_from_package_entry_chunks_impl(
        source_chunks, rewrites, replacement_plan, callback);
}

IndexedPackageEntryChunkReplacementResult make_indexed_cell_replacement_package_entry_chunks(
    const std::vector<PackageEntryChunk>& source_chunks,
    std::span<const WorksheetIndexedCellRewrite> rewrites,
    const WorksheetCellReplacementPlan& replacement_plan)
{
    const PackageEntryIndexedCellReplacementPlan plan =
        plan_indexed_cell_replacement_from_chunks(
            source_chunks, rewrites, replacement_plan);

    IndexedPackageEntryChunkReplacementResult result;
    result.chunks.reserve(plan.rewrites.size() * 2U + 1U);

    std::uint64_t cursor = 0;
    for (const WorksheetIndexedCellRewrite& rewrite : plan.rewrites) {
        const std::uint64_t pass_through_size =
            rewrite.source_range.start_offset - cursor;
        append_package_entry_chunk_range_descriptors_with_layout(
            result.chunks,
            result.output_bytes,
            source_chunks,
            plan.source_layout,
            cursor,
            pass_through_size);

        const auto replacement =
            replacement_plan.replacement_payloads_by_reference.find(
                std::string_view(rewrite.cell_reference));
        append_replacement_payload_descriptors(
            result.chunks, result.output_bytes, replacement->second);
        cursor = rewrite.source_range.end_offset;
    }

    append_package_entry_chunk_range_descriptors_with_layout(
        result.chunks,
        result.output_bytes,
        source_chunks,
        plan.source_layout,
        cursor,
        plan.source_layout.total_size - cursor);

    result.summary.matched_replacement_count = plan.rewrites.size();
    return result;
}

PackageEditor PackageEditor::open(std::filesystem::path path)
{
    return PackageEditor(PackageReader::open(std::move(path)));
}

#ifdef FASTXLSX_ENABLE_TEST_HOOKS
ReplacementCellPayloadScannerTestResult testing_scan_replacement_cell_payload_start_tags(
    const WorksheetCellReplacementPayload& payload)
{
    ReplacementCellPayloadScannerTestResult result;
    scan_replacement_cell_payload_start_tags(
        payload, [&](std::string_view tag_xml, const XmlTagRange& tag) {
            ++result.start_tag_count;
            const std::string_view element = local_xml_name(start_tag_name(tag_xml, tag));
            if (element == "f") {
                ++result.formula_tag_count;
            }
            if (is_worksheet_relationship_reference_element(element)) {
                ++result.relationship_reference_tag_count;
            }
        });
    return result;
}

SheetDataStartTagScannerTestResult testing_scan_sheet_data_start_tags_from_chunks(
    std::span<const std::string_view> chunks,
    std::size_t max_window_bytes)
{
    SheetDataStartTagScannerTestResult result;
    std::size_t chunk_index = 0;
    WorksheetEventReaderOptions options;
    options.max_window_bytes = max_window_bytes;

    scan_sheet_data_start_tags_from_chunk_source(
        [&](std::string& chunk) {
            if (chunk_index >= chunks.size()) {
                return false;
            }
            chunk.assign(chunks[chunk_index]);
            ++chunk_index;
            return true;
        },
        [&](std::string_view tag_xml, const XmlTagRange& tag) {
            ++result.start_tag_count;
            const std::string_view element = local_xml_name(start_tag_name(tag_xml, tag));
            if (element == "f") {
                ++result.formula_tag_count;
            }
            if (element == "c" && cell_start_attribute_equals_for_audit(tag_xml, "t", "s")) {
                ++result.shared_string_cell_tag_count;
            }
        },
        options);

    return result;
}

RelationshipReferenceScannerTestResult testing_scan_worksheet_relationship_references_from_chunks(
    std::span<const std::string_view> chunks)
{
    WorksheetRelationshipReferenceScanner scanner;
    for (const std::string_view chunk : chunks) {
        scanner.process(chunk);
    }
    scanner.finish();

    RelationshipReferenceScannerTestResult result;
    for (const WorksheetRelationshipReference& reference : scanner.references()) {
        result.elements.push_back(reference.element);
        result.relationship_ids.push_back(reference.relationship_id);
    }
    return result;
}

std::string testing_read_package_entry_chunks_to_string(
    std::vector<PackageEntryChunk> chunks)
{
    PackageEntryChunkReader reader(chunks);
    std::string result;
    std::string chunk;
    while (reader(chunk)) {
        result += chunk;
    }
    return result;
}

std::string testing_read_package_entry_chunk_range_to_string(
    std::vector<PackageEntryChunk> chunks,
    std::uint64_t offset,
    std::uint64_t size)
{
    std::string result;
    emit_package_entry_chunk_range(chunks, offset, size,
        [&](std::string_view chunk) {
            result += chunk;
        });
    return result;
}

IndexedChunkReplacementTestResult
testing_emit_indexed_cell_replacement_package_entry_chunks_to_string(
    std::vector<PackageEntryChunk> source_chunks,
    const WorksheetCellIndex& index,
    const WorksheetCellReplacementPlan& replacement_plan)
{
    IndexedChunkReplacementTestResult result;
    result.summary = emit_indexed_cell_replacement_from_package_entry_chunks(
        source_chunks,
        index,
        replacement_plan,
        [&](std::string_view chunk) {
            result.xml += chunk;
        });
    return result;
}

std::string testing_read_first_package_entry_chunk_for_lifecycle(
    std::vector<PackageEntryChunk> chunks)
{
    PackageEntryChunkReader reader(chunks);
    std::string chunk;
    (void)reader(chunk);
    return chunk;
}
#endif

PackageEditor::~PackageEditor()
{
    remove_temporary_files(temporary_files_);
}

PackageEditor::PackageEditor(PackageEditor&& other)
    : reader_(std::move(other.reader_))
    , manifest_(std::move(other.manifest_))
    , edit_plan_(std::move(other.edit_plan_))
    , replacements_(std::move(other.replacements_))
    , entry_replacements_(std::move(other.entry_replacements_))
    , omitted_entries_(std::move(other.omitted_entries_))
    , temporary_files_(std::move(other.temporary_files_))
{
    other.temporary_files_.clear();
}

PackageEditor& PackageEditor::operator=(PackageEditor&& other)
{
    if (this != &other) {
        remove_temporary_files(temporary_files_);
        reader_ = std::move(other.reader_);
        manifest_ = std::move(other.manifest_);
        edit_plan_ = std::move(other.edit_plan_);
        replacements_ = std::move(other.replacements_);
        entry_replacements_ = std::move(other.entry_replacements_);
        omitted_entries_ = std::move(other.omitted_entries_);
        temporary_files_ = std::move(other.temporary_files_);
        other.temporary_files_.clear();
    }
    return *this;
}

PackageEditor::PackageEditor(PackageReader reader)
    : reader_(std::move(reader))
    , manifest_(build_manifest_from_reader(reader_))
    , edit_plan_(PartRewritePlanner(manifest_).plan_copy_original())
{
}

const PackageReader& PackageEditor::reader() const noexcept
{
    return reader_;
}

const PackageManifest& PackageEditor::manifest() const noexcept
{
    return manifest_;
}

const EditPlan& PackageEditor::edit_plan() const noexcept
{
    return edit_plan_;
}

std::string PackageEditor::current_workbook_xml_for_diagnostics(
    std::string_view purpose) const
{
    const PartName workbook_part = reader_.workbook_part();
    if (manifest_.find_part(workbook_part) == nullptr) {
        throw FastXlsxError(
            "current workbook XML is unavailable for " + std::string(purpose)
            + " because the officeDocument workbook part has been removed");
    }
    return current_planned_materialized_workbook_xml(reader_, replacements_, purpose);
}

std::vector<PackageEntryChunk> PackageEditor::source_part_stored_entry_chunks(
    PartName part_name) const
{
    return source_stored_entry_range_chunks(
        reader_, part_name.zip_path(), "source part stored-entry chunk range");
}

std::vector<PackageEntryChunk>
PackageEditor::source_worksheet_part_stored_entry_chunks_by_name(
    std::string_view sheet_name) const
{
    const PartName worksheet_part =
        resolve_worksheet_part_by_name_for_patch(
            reader_, manifest_, replacements_, sheet_name);
    return source_stored_entry_range_chunks(
        reader_, worksheet_part.zip_path(), "source worksheet stored-entry chunk range");
}

void PackageEditor::replace_part(
    PartName part_name, std::string materialized_small_xml, PartWriteMode write_mode,
    std::string reason)
{
    if (write_mode == PartWriteMode::CopyOriginal) {
        throw FastXlsxError("replacement part cannot use copy-original write mode");
    }
    if (is_metadata_part_replacement_target(part_name)) {
        throw FastXlsxError("metadata package entries cannot be replaced as ordinary parts");
    }

    const PackagePart* planned_part = manifest_.find_part(part_name);
    const PackagePart* source_part = nullptr;
    if (planned_part == nullptr) {
        source_part = reader_.part_index().find_part(part_name);
        if (source_part == nullptr) {
            throw FastXlsxError("replacement part is not present in the source package");
        }
    }

    if (reason.empty()) {
        reason = default_replacement_reason(write_mode);
    }

    const PackagePart* target_part = planned_part != nullptr ? planned_part : source_part;
    std::optional<PartName> workbook_part;
    if (target_part != nullptr && target_part->content_type == content_type_workbook) {
        workbook_part = reader_.workbook_part();
    }
    const bool stage_ordinary_worksheet_replacement =
        target_part != nullptr && target_part->content_type == content_type_worksheet;
    if (stage_ordinary_worksheet_replacement) {
        throw FastXlsxError(
            "ordinary replace_part cannot target worksheet parts; use "
            "replace_worksheet_part_from_chunk_source or "
            "replace_worksheet_part_chunks");
    }
    if (write_mode == PartWriteMode::StreamRewrite) {
        throw FastXlsxError(
            "materialized replace_part cannot use stream-rewrite write mode; "
            "use replace_part_chunks() for stream-rewrite payloads");
    }
    require_materialized_source_part_replacement_allowed(reader_, part_name);

    const std::string_view size_guard_purpose =
        workbook_part.has_value() && part_name == *workbook_part
        ? std::string_view("workbook package-part replacement")
        : std::string_view("package-part replacement");
    require_materialized_part_replacement_payload_size(part_name,
        materialized_small_xml.size(), write_mode, size_guard_purpose,
        workbook_part.has_value() ? &*workbook_part : nullptr);
    if (workbook_part.has_value() && part_name == *workbook_part
        && edit_plan_.full_calculation_on_load()) {
        materialized_small_xml =
            request_full_calculation_in_workbook_xml(std::move(materialized_small_xml));
        require_materialized_workbook_xml_size(
            materialized_small_xml.size(), "workbook package-part replacement");
    }

    PackageManifest updated_manifest = manifest_;
    EditPlan updated_edit_plan = edit_plan_;
    std::vector<PackagePartReplacement> updated_replacements = replacements_;
    std::vector<PackageEntryReplacement> updated_entry_replacements = entry_replacements_;
    std::vector<std::string> updated_omitted_entries = omitted_entries_;
    if (planned_part == nullptr) {
        restore_source_part_manifest_state(updated_manifest, reader_, *source_part);
        if (updated_manifest.find_part(part_name) == nullptr) {
            throw FastXlsxError("replacement part was not restored");
        }
    }

    upsert_part_replacement(updated_replacements, part_name,
        std::move(materialized_small_xml),
        write_mode, reason, workbook_part.has_value() ? &*workbook_part : nullptr);
    updated_manifest.set_part_write_mode(part_name, write_mode);
    updated_edit_plan.set_part(part_name, write_mode, reason);
    restore_active_part_entry_state_after_replacement(updated_edit_plan,
        updated_entry_replacements, updated_omitted_entries, reader_, updated_manifest,
        part_name);

#ifdef FASTXLSX_ENABLE_TEST_HOOKS
    run_package_editor_materialized_part_replacement_staged_hook();
#endif

    commit_package_editor_staged_state(manifest_, edit_plan_, replacements_,
        entry_replacements_, omitted_entries_, updated_manifest, updated_edit_plan,
        updated_replacements, updated_entry_replacements, updated_omitted_entries);
}

void PackageEditor::replace_part_chunks(
    PartName part_name, std::vector<PackageEntryChunk> chunks, std::string reason)
{
    if (chunks.empty()) {
        throw FastXlsxError("staged package part replacement requires at least one chunk");
    }
    if (is_metadata_part_replacement_target(part_name)) {
        throw FastXlsxError("metadata package entries cannot be replaced as ordinary parts");
    }

    const PackagePart* planned_part = manifest_.find_part(part_name);
    const PackagePart* source_part = nullptr;
    if (planned_part == nullptr) {
        source_part = reader_.part_index().find_part(part_name);
        if (source_part == nullptr) {
            throw FastXlsxError("replacement part is not present in the source package");
        }
    }
    const PackagePart* target_package_part =
        planned_part != nullptr ? planned_part : source_part;
    if (target_package_part->content_type == content_type_worksheet) {
        std::vector<std::string> commit_notes;
        commit_notes.emplace_back(
            "generic staged package part chunk replacement targeting a worksheet part "
            "was routed through worksheet-aware validation, dependency/relationship "
            "audit, and calc metadata handling");
        replace_worksheet_part_chunks_with_commit_notes(std::move(part_name),
            std::move(chunks), {}, {}, std::move(commit_notes));
        return;
    }

    require_staged_package_part_replacement_allowed(*target_package_part);
    require_staged_package_entry_chunks_valid(
        chunks, "generic package part staged replacement");
    if (reason.empty()) {
        reason = "target package part staged stream rewrite";
    }

    PackageManifest updated_manifest = manifest_;
    EditPlan updated_edit_plan = edit_plan_;
    std::vector<PackagePartReplacement> updated_replacements = replacements_;
    std::vector<PackageEntryReplacement> updated_entry_replacements = entry_replacements_;
    std::vector<std::string> updated_omitted_entries = omitted_entries_;
    if (planned_part == nullptr) {
        restore_source_part_manifest_state(updated_manifest, reader_, *source_part);
        if (updated_manifest.find_part(part_name) == nullptr) {
            throw FastXlsxError("replacement part was not restored");
        }
    }

    upsert_part_replacement_chunks(updated_replacements, part_name, std::move(chunks),
        PartWriteMode::StreamRewrite, reason);
    updated_manifest.set_part_write_mode(part_name, PartWriteMode::StreamRewrite);
    updated_edit_plan.set_part(part_name, PartWriteMode::StreamRewrite, reason);
    restore_active_part_entry_state_after_replacement(updated_edit_plan,
        updated_entry_replacements, updated_omitted_entries, reader_, updated_manifest,
        part_name);

#ifdef FASTXLSX_ENABLE_TEST_HOOKS
    run_package_editor_chunk_part_replacement_staged_hook();
#endif

    commit_package_editor_staged_state(manifest_, edit_plan_, replacements_,
        entry_replacements_, omitted_entries_, updated_manifest, updated_edit_plan,
        updated_replacements, updated_entry_replacements, updated_omitted_entries);
}

void PackageEditor::remove_part(
    PartName part_name, std::string reason, const ReferencePolicy& policy)
{
    if (is_metadata_part_replacement_target(part_name)) {
        throw FastXlsxError("metadata package entries cannot be removed as ordinary parts");
    }
    if (manifest_.find_part(part_name) == nullptr) {
        throw FastXlsxError("part removal target is not present in the source package");
    }
    if (reader_.find_entry(part_name.zip_path()) == nullptr) {
        throw FastXlsxError("part removal target does not have a source package entry");
    }
    if (reason.empty()) {
        reason = "registered package part removed";
    }

    const EditPlan removal_plan =
        PartRewritePlanner(manifest_).plan_part_removal(part_name, reason);
    reject_part_removal_inbound_relationships_by_policy(removal_plan, part_name, policy);

    PackageManifest updated_manifest = manifest_;
    EditPlan updated_edit_plan = edit_plan_;
    std::vector<PackagePartReplacement> updated_replacements = replacements_;
    std::vector<PackageEntryReplacement> updated_entry_replacements = entry_replacements_;
    std::vector<std::string> updated_omitted_entries = omitted_entries_;
    const bool rewrite_content_types =
        updated_manifest.content_types().override_for(part_name) != nullptr;
    const bool removed_manifest_state = updated_manifest.remove_part(part_name);
    if (!removed_manifest_state) {
        throw FastXlsxError("part removal did not update package manifest state");
    }

    merge_removed_part_audit(updated_edit_plan, removal_plan, part_name);
    for (const std::string& note : removal_plan.notes()) {
        updated_edit_plan.add_note(note);
    }

    stage_part_removal_entries(updated_edit_plan, updated_replacements,
        updated_entry_replacements, updated_omitted_entries, reader_, part_name);

    if (rewrite_content_types) {
        upsert_entry_replacement(reader_, updated_entry_replacements, "[Content_Types].xml",
            serialize_content_types(updated_manifest.content_types()));
        updated_edit_plan.set_package_entry("[Content_Types].xml",
            PartWriteMode::LocalDomRewrite, "content types updated for explicit part removal",
            PackageEntryAuditKind::ContentTypes);
    }

#ifdef FASTXLSX_ENABLE_TEST_HOOKS
    run_package_editor_part_removal_staged_hook();
#endif

    commit_package_editor_staged_state(manifest_, edit_plan_, replacements_,
        entry_replacements_, omitted_entries_, updated_manifest, updated_edit_plan,
        updated_replacements, updated_entry_replacements, updated_omitted_entries);
}

void PackageEditor::replace_worksheet_part_from_chunk_source(PartName worksheet_part,
    const WorksheetInputChunkCallback& read_next_chunk, const ReferencePolicy& policy,
    std::string reason)
{
    replace_worksheet_part_from_chunk_source_with_commit_notes(
        std::move(worksheet_part), read_next_chunk, policy, std::move(reason), {});
}

void PackageEditor::replace_worksheet_part_from_chunk_source_with_commit_notes(
    PartName worksheet_part, const WorksheetInputChunkCallback& read_next_chunk,
    const ReferencePolicy& policy, std::string reason,
    std::vector<std::string> commit_notes)
{
    validate_worksheet_replacement_preconditions(manifest_, reader_, worksheet_part, policy);
    const PartName target_worksheet_part = worksheet_part;

    ScopedPackageEditorTempFile staged_worksheet_file;
    WorksheetReplacementChunkAuditResult replacement_audit =
        write_worksheet_replacement_file_and_audit_from_chunk_source(
            staged_worksheet_file.path(), target_worksheet_part, read_next_chunk,
            reader_.relationships_for(target_worksheet_part),
            "planned worksheet replacement chunk source");

    std::vector<PackageEntryChunk> staged_worksheet_chunks;
    staged_worksheet_chunks.push_back(PackageEntryChunk::file(staged_worksheet_file.path()));

    if (reason.empty()) {
        reason =
            "target worksheet part staged stream rewrite from caller chunk source validated and audited while staging";
    }
    commit_notes.emplace_back(
        "worksheet replacement consumed caller-provided worksheet XML from a "
        "pull-based chunk source after target/workbook/calc policy preflight "
        "into a PackageEditor-owned file-backed staged chunk while validating "
        "worksheet root/events and collecting payload/relationship-id audit, "
        "then committed those prevalidated chunks for calc metadata handling "
        "and follow-up planned-input transforms");
    commit_notes.emplace_back(
        "worksheet chunk-source replacement validates worksheet root/events, "
        "collects payload dependency audit plus relationship-id audit, and "
        "writes the staged worksheet chunk in one caller chunk-source pass "
        "without reopening that staged chunk for validation or audit");

    replace_worksheet_part_prevalidated_chunks(std::move(worksheet_part),
        std::move(staged_worksheet_chunks), policy,
        std::move(replacement_audit.payload_audit.notes),
        std::move(replacement_audit.payload_audit.audits),
        std::move(replacement_audit.relationship_reference_audit.notes),
        std::move(replacement_audit.relationship_reference_audit.audits),
        std::move(reason), true, true, std::move(commit_notes),
        staged_worksheet_file.path());
    staged_worksheet_file.release();
}

void PackageEditor::replace_worksheet_part_chunks(PartName worksheet_part,
    std::vector<PackageEntryChunk> chunks, const ReferencePolicy& policy,
    std::string reason)
{
    replace_worksheet_part_chunks_with_commit_notes(std::move(worksheet_part),
        std::move(chunks), policy, std::move(reason), {});
}

void PackageEditor::replace_worksheet_part_chunks_with_commit_notes(
    PartName worksheet_part, std::vector<PackageEntryChunk> chunks,
    const ReferencePolicy& policy, std::string reason,
    std::vector<std::string> commit_notes)
{
    if (chunks.empty()) {
        throw FastXlsxError("staged worksheet replacement requires at least one chunk");
    }

    const PartName target_worksheet_part = worksheet_part;
    validate_worksheet_replacement_preconditions(manifest_, reader_, worksheet_part, policy);
    require_staged_package_entry_chunks_valid(
        chunks, "worksheet staged replacement");

    PackageEntryChunkReader audit_reader(chunks);
    const WorksheetInputChunkCallback audit_source =
        [&](std::string& chunk) { return audit_reader(chunk); };
    WorksheetReplacementChunkAuditResult replacement_audit =
        worksheet_replacement_audits_from_chunk_source(target_worksheet_part,
            audit_source, reader_.relationships_for(target_worksheet_part));

    if (reason.empty()) {
        reason =
            "target worksheet part staged stream rewrite chunks validated and audited through one event-reader chunk-source pass";
    }
    commit_notes.emplace_back(
        "worksheet staged chunk replacement validates worksheet root/events and "
        "collects payload dependency audit plus relationship-id audit together "
        "from the provided staged chunks through one chunk-source audit reader");
    replace_worksheet_part_prevalidated_chunks(std::move(worksheet_part), std::move(chunks),
        policy, std::move(replacement_audit.payload_audit.notes),
        std::move(replacement_audit.payload_audit.audits),
        std::move(replacement_audit.relationship_reference_audit.notes),
        std::move(replacement_audit.relationship_reference_audit.audits), std::move(reason),
        true, true, std::move(commit_notes));
}

void PackageEditor::replace_worksheet_part_prevalidated_chunks(PartName worksheet_part,
    std::vector<PackageEntryChunk> chunks, const ReferencePolicy& policy,
    std::vector<std::string> payload_notes,
    std::vector<WorksheetPayloadDependencyAudit> payload_audits,
    std::vector<std::string> relationship_reference_notes,
    std::vector<WorksheetRelationshipReferenceAudit> relationship_reference_audits,
    std::string replacement_reason, bool enforce_payload_policy,
    bool validate_staged_chunk_crc32, std::vector<std::string> commit_notes,
    std::optional<std::filesystem::path> owned_temporary_file,
    PartWriteMode target_write_mode,
    std::optional<IndexedSourceEntryDirectRangeStats> indexed_stats,
    std::optional<SinglePassWorksheetTransformStats> single_pass_stats)
{
    const auto staged_commit_started = std::chrono::steady_clock::now();
    if (chunks.empty()) {
        throw FastXlsxError("staged worksheet replacement requires at least one chunk");
    }

    const PartName target_worksheet_part = worksheet_part;
    validate_worksheet_replacement_preconditions(manifest_, reader_, worksheet_part, policy);
    require_staged_package_entry_chunks_valid(
        chunks, "prevalidated worksheet staged replacement", validate_staged_chunk_crc32);

    WorksheetPayloadDependencyAuditResult payload_audit;
    payload_audit.notes = std::move(payload_notes);
    payload_audit.audits = std::move(payload_audits);
    WorksheetRelationshipReferenceAuditResult relationship_reference_audit;
    relationship_reference_audit.notes = std::move(relationship_reference_notes);
    relationship_reference_audit.audits = std::move(relationship_reference_audits);
    reject_relationship_reference_audit_by_policy(relationship_reference_audit, policy);
    if (enforce_payload_policy) {
        reject_payload_dependencies_by_policy(
            payload_audit, policy, "worksheet replacement");
    }

    PackageManifest updated_manifest = manifest_;
    EditPlan updated_edit_plan = edit_plan_;
    std::vector<PackagePartReplacement> updated_replacements = replacements_;
    std::vector<PackageEntryReplacement> updated_entry_replacements = entry_replacements_;
    std::vector<std::string> updated_omitted_entries = omitted_entries_;
    std::vector<std::filesystem::path> updated_temporary_files = temporary_files_;
    if (owned_temporary_file.has_value()) {
        updated_temporary_files.push_back(std::move(*owned_temporary_file));
    }
    if (updated_manifest.find_part(target_worksheet_part) == nullptr) {
        const PackagePart* source_part =
            reader_.part_index().find_part(target_worksheet_part);
        if (source_part == nullptr) {
            throw FastXlsxError(
                "worksheet replacement target is not present in the source package for "
                + worksheet_part_diagnostic_context(target_worksheet_part));
        }
        restore_source_part_manifest_state(updated_manifest, reader_, *source_part);
        if (updated_manifest.find_part(target_worksheet_part) == nullptr) {
            throw FastXlsxError("worksheet replacement target was not restored for "
                + worksheet_part_diagnostic_context(target_worksheet_part));
        }
    }

    const EditPlan worksheet_plan =
        PartRewritePlanner(updated_manifest).plan_worksheet_stream_rewrite(
            worksheet_part, policy);
    const PartName workbook_part = reader_.workbook_part();

    std::string updated_workbook_xml;
    if (worksheet_plan.full_calculation_on_load()) {
        updated_workbook_xml = request_full_calculation_in_workbook_xml(
            current_planned_materialized_workbook_xml(reader_, updated_replacements,
                "worksheet rewrite calc metadata"));
    }

    const PartName calc_chain_part("/xl/calcChain.xml");
    const EditPlanRemovedPart* removed_calc_chain =
        worksheet_plan.find_removed_part(calc_chain_part);
    const bool remove_calc_chain = removed_calc_chain != nullptr;
    bool omit_calc_chain = false;
    bool rewrite_content_types = false;
    bool rewrite_workbook_relationships = false;
    if (policy.calc_chain_action == CalcChainAction::Remove) {
        omit_calc_chain = reader_.find_entry(calc_chain_part.zip_path()) != nullptr;
        rewrite_content_types = updated_manifest.remove_part(calc_chain_part);

        if (RelationshipSet* workbook_relationships =
                updated_manifest.relationships_for(workbook_part)) {
            rewrite_workbook_relationships =
                workbook_relationships->remove_by_type(relationship_type_calc_chain) > 0;
        }
    }

    if (replacement_reason.empty()) {
        replacement_reason = "target worksheet part stream rewrite";
    }
    upsert_part_replacement_chunks(updated_replacements, target_worksheet_part,
        std::move(chunks), target_write_mode, replacement_reason);
    retain_referenced_temporary_files(updated_temporary_files, updated_replacements);
    updated_manifest.set_part_write_mode(target_worksheet_part, target_write_mode);
    updated_edit_plan.set_part(target_worksheet_part, target_write_mode,
        replacement_reason);
    restore_active_part_entry_state_after_replacement(updated_edit_plan,
        updated_entry_replacements, updated_omitted_entries, reader_, updated_manifest,
        target_worksheet_part);

    merge_copy_original_dependency_reasons(updated_edit_plan, worksheet_plan);
    audit_preserved_relationship_entries(
        updated_edit_plan, reader_, worksheet_plan, target_worksheet_part);

    if (worksheet_plan.full_calculation_on_load()) {
        updated_edit_plan.request_full_calculation(worksheet_plan.calc_chain_action());
        updated_edit_plan.set_part(workbook_part, PartWriteMode::LocalDomRewrite,
            "workbook calcPr fullCalcOnLoad updated for worksheet rewrite; definedNames preserved for policy review");
        upsert_part_replacement(updated_replacements, workbook_part,
            std::move(updated_workbook_xml), PartWriteMode::LocalDomRewrite,
            "workbook calcPr fullCalcOnLoad updated for worksheet rewrite; definedNames preserved for policy review",
            &workbook_part);
        remove_entry_replacement(updated_entry_replacements, workbook_part.zip_path());
        remove_omitted_entry(updated_omitted_entries, workbook_part.zip_path());
        if (!rewrite_workbook_relationships) {
            audit_preserved_relationship_entry(updated_edit_plan, reader_, workbook_part);
        }
    }

    if (remove_calc_chain) {
        const std::string calc_chain_relationship_entry =
            relationship_entry_name_for_source_part(calc_chain_part);
        remove_part_replacement(updated_replacements, calc_chain_part);
        remove_entry_replacement(updated_entry_replacements, calc_chain_part.zip_path());
        remove_entry_replacement(
            updated_entry_replacements, calc_chain_relationship_entry);
        merge_removed_part_audit(updated_edit_plan, worksheet_plan, calc_chain_part);
    }

    if (omit_calc_chain) {
        const std::string calc_chain_relationship_entry =
            relationship_entry_name_for_source_part(calc_chain_part);
        add_omitted_part_entries(updated_omitted_entries, calc_chain_part);
        if (reader_.find_entry(calc_chain_relationship_entry) != nullptr) {
            updated_edit_plan.remove_package_entry(calc_chain_relationship_entry,
                "calcChain-owned relationships omitted with removed calcChain part",
                PackageEntryAuditKind::SourceRelationships, calc_chain_part.value());
        }
    }

    if (rewrite_content_types) {
        upsert_entry_replacement(reader_, updated_entry_replacements, "[Content_Types].xml",
            serialize_content_types(updated_manifest.content_types()));
        updated_edit_plan.set_package_entry(
            "[Content_Types].xml", PartWriteMode::LocalDomRewrite,
            "content types updated for worksheet calcChain removal",
            PackageEntryAuditKind::ContentTypes);
    }

    if (rewrite_workbook_relationships) {
        RelationshipSet* workbook_relationships =
            updated_manifest.relationships_for(workbook_part);
        const std::string workbook_relationship_entry =
            relationship_entry_name_for_source_part(workbook_part);
        upsert_entry_replacement(reader_, updated_entry_replacements,
            workbook_relationship_entry, serialize_relationships(*workbook_relationships));
        updated_edit_plan.set_package_entry(
            workbook_relationship_entry, PartWriteMode::LocalDomRewrite,
            "workbook relationships updated for worksheet calcChain removal",
            PackageEntryAuditKind::SourceRelationships, workbook_part.value());
    }

    updated_manifest.set_part_write_mode(target_worksheet_part, target_write_mode);
    if (worksheet_plan.full_calculation_on_load()) {
        updated_manifest.set_part_write_mode(workbook_part, PartWriteMode::LocalDomRewrite);
    }
    for (const std::string& note : worksheet_plan.notes()) {
        updated_edit_plan.add_note(note);
    }
    for (std::string& note : payload_audit.notes) {
        updated_edit_plan.add_note(std::move(note));
    }
    for (std::string& note : relationship_reference_audit.notes) {
        updated_edit_plan.add_note(std::move(note));
    }
    for (const RelationshipTargetAudit& audit : worksheet_plan.relationship_target_audits()) {
        updated_edit_plan.add_relationship_target_audit(audit);
    }
    for (const WorkbookPayloadDependencyAudit& audit :
        worksheet_plan.workbook_payload_dependency_audits()) {
        updated_edit_plan.add_workbook_payload_dependency_audit(audit);
    }
    for (WorksheetRelationshipReferenceAudit& audit :
        relationship_reference_audit.audits) {
        updated_edit_plan.add_worksheet_relationship_reference_audit(std::move(audit));
    }
    for (WorksheetPayloadDependencyAudit& audit : payload_audit.audits) {
        updated_edit_plan.add_worksheet_payload_dependency_audit(std::move(audit));
    }
    for (std::string& note : commit_notes) {
        updated_edit_plan.add_note(std::move(note));
    }
    if (indexed_stats.has_value()) {
        PackagePartReplacement* replacement =
            find_replacement(updated_replacements, target_worksheet_part);
        if (replacement == nullptr) {
            throw FastXlsxError(
                "indexed worksheet replacement staging lost the target replacement");
        }
        replacement->indexed_source_entry_direct_range = true;
        replacement->indexed_source_entry_scanned_source_cell_count =
            indexed_stats->scanned_source_cell_count;
        replacement->indexed_source_entry_matched_replacement_count =
            indexed_stats->matched_replacement_count;
        replacement->indexed_source_entry_staged_output_bytes =
            indexed_stats->staged_output_bytes;
        replacement->indexed_source_entry_source_range_chunk_ms =
            indexed_stats->source_range_chunk_ms;
        replacement->indexed_source_entry_target_plan_ms =
            indexed_stats->target_plan_ms;
        replacement->indexed_source_entry_payload_audit_ms =
            indexed_stats->payload_audit_ms;
        replacement->indexed_source_entry_relationship_audit_ms =
            indexed_stats->relationship_audit_ms;
        replacement->indexed_source_entry_descriptor_ms =
            indexed_stats->descriptor_ms;
        replacement->indexed_source_entry_commit_ms =
            steady_clock_elapsed_milliseconds(staged_commit_started);
    }
    if (single_pass_stats.has_value()) {
        PackagePartReplacement* replacement =
            find_replacement(updated_replacements, target_worksheet_part);
        if (replacement == nullptr) {
            throw FastXlsxError(
                "single-pass worksheet transform staging lost the target replacement");
        }
        replacement->single_pass_worksheet_transform = true;
        replacement->single_pass_scanned_source_cell_count =
            single_pass_stats->scanned_source_cell_count;
        replacement->single_pass_matched_replacement_count =
            single_pass_stats->matched_replacement_count;
        replacement->single_pass_inserted_cell_count =
            single_pass_stats->inserted_cell_count;
        replacement->single_pass_staged_output_bytes =
            single_pass_stats->staged_output_bytes;
        replacement->single_pass_transform_ms = single_pass_stats->transform_ms;
        replacement->single_pass_commit_ms =
            steady_clock_elapsed_milliseconds(staged_commit_started);
    }

#ifdef FASTXLSX_ENABLE_TEST_HOOKS
    run_package_editor_worksheet_part_replacement_staged_hook();
#endif

    commit_package_editor_staged_state_with_temporary_files(manifest_, edit_plan_,
        replacements_, entry_replacements_, omitted_entries_, temporary_files_,
        updated_manifest, updated_edit_plan, updated_replacements,
        updated_entry_replacements, updated_omitted_entries,
        updated_temporary_files);
}

void PackageEditor::replace_worksheet_part_from_chunk_source_by_name(
    std::string_view sheet_name,
    const WorksheetInputChunkCallback& read_next_chunk,
    const ReferencePolicy& policy,
    std::string reason)
{
    std::vector<std::string> commit_notes;
    commit_notes.emplace_back(
        "by-name worksheet chunk-source replacement resolves the worksheet part "
        "through the planned/source workbook catalog and consumes the caller "
        "chunk source without routing through any materialized worksheet string "
        "entry point");
    replace_worksheet_part_from_chunk_source_with_commit_notes(
        resolve_worksheet_part_by_name_for_patch(
            reader_, manifest_, replacements_, sheet_name),
        read_next_chunk, policy, std::move(reason), std::move(commit_notes));
}

void PackageEditor::replace_worksheet_part_chunks_by_name(std::string_view sheet_name,
    std::vector<PackageEntryChunk> chunks, const ReferencePolicy& policy,
    std::string reason)
{
    std::vector<std::string> commit_notes;
    commit_notes.emplace_back(
        "by-name worksheet staged chunk replacement resolves the worksheet part "
        "through the planned/source workbook catalog and then validates, audits, "
        "and commits the provided chunks without any materialized worksheet "
        "string entry point");
    replace_worksheet_part_chunks_with_commit_notes(
        resolve_worksheet_part_by_name_for_patch(
            reader_, manifest_, replacements_, sheet_name),
        std::move(chunks), policy, std::move(reason), std::move(commit_notes));
}

void PackageEditor::replace_worksheet_part_prevalidated_chunks_by_name(
    std::string_view sheet_name, std::vector<PackageEntryChunk> chunks,
    const ReferencePolicy& policy, std::string reason)
{
    if (reason.empty()) {
        reason =
            "target worksheet part prevalidated staged stream rewrite without staged audit reread";
    }

    std::vector<std::string> commit_notes;
    commit_notes.emplace_back(
        "by-name worksheet staged chunk prevalidated replacement resolves the worksheet part "
        "through the planned/source workbook catalog and commits already-audited staged "
        "chunks without reopening the staged worksheet for a second audit scan");
    replace_worksheet_part_prevalidated_chunks(
        resolve_worksheet_part_by_name_for_patch(
            reader_, manifest_, replacements_, sheet_name),
        std::move(chunks), policy, {}, {}, {}, {}, std::move(reason), false, false,
        std::move(commit_notes));
}

void PackageEditor::replace_worksheet_sheet_data_from_chunk_source(
    PartName worksheet_part,
    const WorksheetInputChunkCallback& read_next_sheet_data_chunk,
    const ReferencePolicy& policy,
    std::optional<std::string_view> dimension_reference)
{
    replace_worksheet_sheet_data_from_chunk_source_with_commit_notes(
        std::move(worksheet_part), read_next_sheet_data_chunk, policy,
        dimension_reference, {});
}

void PackageEditor::replace_worksheet_sheet_data_from_chunk_source_with_commit_notes(
    PartName worksheet_part,
    const WorksheetInputChunkCallback& read_next_sheet_data_chunk,
    const ReferencePolicy& policy,
    std::optional<std::string_view> dimension_reference,
    std::vector<std::string> commit_notes)
{
    const PartName target_worksheet_part = worksheet_part;
    const auto* worksheet = manifest_.find_part(worksheet_part);
    if (worksheet == nullptr) {
        throw FastXlsxError(
            "worksheet sheetData replacement target is not present in the source package for "
            + worksheet_part_diagnostic_context(target_worksheet_part));
    }
    if (worksheet->content_type != content_type_worksheet) {
        throw FastXlsxError(
            "worksheet sheetData replacement target is not a worksheet part for "
            + worksheet_part_diagnostic_context(target_worksheet_part));
    }

    const CurrentWorksheetInputSource input_source =
        require_current_worksheet_input_source(reader_, replacements_, entry_replacements_,
            target_worksheet_part, "worksheet sheetData replacement");

    bool source_has_dimension = false;
    if (dimension_reference.has_value()) {
        CurrentWorksheetInputChunkReader analysis_reader(reader_, target_worksheet_part,
            input_source, "current worksheet input for worksheet sheetData dimension refresh");
        const WorksheetInputChunkCallback analysis_source =
            [&](std::string& chunk) { return analysis_reader(chunk); };
        try {
            source_has_dimension =
                worksheet_source_has_dimension_metadata(analysis_source);
        } catch (const std::exception& error) {
            throw FastXlsxError(failed_to_consume_current_worksheet_input_message(
                "current worksheet input for worksheet sheetData dimension refresh",
                error.what()));
        }
    }

    ScopedPackageEditorTempFile staged_worksheet_file;
    CurrentWorksheetInputChunkReader output_reader(reader_, target_worksheet_part, input_source,
        std::string(worksheet_sheet_data_replacement_output_input_context));
    const WorksheetInputChunkCallback output_source =
        [&](std::string& chunk) { return output_reader(chunk); };
    WorksheetRelationshipReferenceScanner relationship_scanner;
    bool relationship_scan_failed = false;
    WorksheetSheetDataPreservationAuditCollector preservation_collector(target_worksheet_part);
    WorksheetPayloadDependencyAuditResult replacement_audit =
        write_worksheet_sheet_data_replacement_stream_from_chunk_source(
            staged_worksheet_file.path(), target_worksheet_part, &relationship_scanner,
            relationship_scan_failed, &preservation_collector, output_source,
            read_next_sheet_data_chunk, dimension_reference, source_has_dimension);
    reject_payload_dependencies_by_policy(
        replacement_audit, policy, "sheetData replacement");

    WorksheetPayloadDependencyAuditResult preservation_audit =
        std::move(preservation_collector).finish();
    for (std::string& note : replacement_audit.notes) {
        preservation_audit.notes.push_back(std::move(note));
    }
    for (WorksheetPayloadDependencyAudit& audit : replacement_audit.audits) {
        preservation_audit.audits.push_back(std::move(audit));
    }

    std::vector<PackageEntryChunk> staged_worksheet_chunks;
    staged_worksheet_chunks.push_back(PackageEntryChunk::file(staged_worksheet_file.path()));

    WorksheetRelationshipReferenceAuditResult relationship_reference_audit =
        relationship_scan_failed
        ? worksheet_relationship_reference_parse_failure_audit()
        : worksheet_relationship_reference_audit_from_references(target_worksheet_part,
            relationship_scanner.references(), reader_.relationships_for(target_worksheet_part));

    const std::string sheet_data_rewrite_reason =
        "target worksheet part local-DOM rewrite from bounded local sheetData replacement; "
        "rewritten worksheet XML is recorded as file-backed staged chunks";
    commit_notes.emplace_back(
        "sheetData replacement uses bounded local worksheet XML rewrite and "
        "chunk-source replacement/output; the replacement sheetData XML is "
        "consumed directly while writing the rewritten worksheet XML to a "
        "PackageEditor-owned file-backed staged chunk for follow-up "
        "planned-input transforms and this is "
        "not the large-file streaming worksheet transformer");
    commit_notes.emplace_back(
        "sheetData replacement validates replacement sheetData root and "
        "collects replacement payload dependency audit while inserting caller "
        "sheetData chunks into the rewritten worksheet output, without staging "
        "or replaying a separate replacement sheetData chunk");
    commit_notes.emplace_back(
        "sheetData replacement bounded local worksheet XML rewrite stores the "
        "rewritten worksheet XML as a PackageEditor-owned file-backed staged "
        "chunk for follow-up planned-input transforms");
    commit_notes.emplace_back(
        "sheetData replacement output writer collects worksheet relationship-id "
        "audit while writing the rewritten staged worksheet chunk, without a "
        "separate post-output worksheet validation or audit reread");
    commit_notes.emplace_back(
        "sheetData replacement output writer collects preserved worksheet "
        "metadata audit while consuming the current worksheet input chunks, "
        "without a separate preservation-only worksheet reread");

    replace_worksheet_part_prevalidated_chunks(std::move(worksheet_part),
        std::move(staged_worksheet_chunks), policy,
        std::move(preservation_audit.notes), std::move(preservation_audit.audits),
        std::move(relationship_reference_audit.notes),
        std::move(relationship_reference_audit.audits), sheet_data_rewrite_reason,
        false, true, std::move(commit_notes), staged_worksheet_file.path(),
        PartWriteMode::LocalDomRewrite);
    staged_worksheet_file.release();
}

void PackageEditor::replace_worksheet_sheet_data_from_chunk_source_by_name(
    std::string_view sheet_name,
    const WorksheetInputChunkCallback& read_next_sheet_data_chunk,
    const ReferencePolicy& policy,
    std::optional<std::string_view> dimension_reference)
{
    const PartName worksheet_part =
        resolve_worksheet_part_by_name_for_patch(
            reader_, manifest_, replacements_, sheet_name);
    std::vector<std::string> commit_notes;
    commit_notes.emplace_back(
        "by-name sheetData chunk-source replacement resolves the worksheet part "
        "through the planned/source workbook catalog and consumes replacement "
        "sheetData XML without routing through the materialized sheetData string "
        "entry point");
    try {
        replace_worksheet_sheet_data_from_chunk_source_with_commit_notes(
            worksheet_part, read_next_sheet_data_chunk, policy, dimension_reference,
            std::move(commit_notes));
    } catch (const std::exception& error) {
        throw FastXlsxError(
            by_name_worksheet_operation_context(
                "by-name sheetData replacement", sheet_name, worksheet_part)
            + ": " + error.what());
    }
}

bool PackageEditor::try_replace_worksheet_cells_with_indexed_chunks(
    PartName worksheet_part,
    std::vector<PackageEntryChunk> source_chunks,
    const WorksheetCellReplacementPlan& replacement_plan,
    const ReferencePolicy& policy,
    std::string input_label,
    std::optional<std::filesystem::path> owned_temporary_file,
    std::uint64_t source_preparation_ms)
{
    if (policy.unsupported_linked_part_action == ReferencePolicyAction::Fail) {
        return false;
    }
    if (source_chunks.empty()) {
        return false;
    }

    const RelationshipSet* worksheet_relationships =
        reader_.relationships_for(worksheet_part);
    if (worksheet_relationships != nullptr && !worksheet_relationships->empty()) {
        return false;
    }

    auto phase_started = std::chrono::steady_clock::now();
    std::vector<std::string_view> target_references;
    target_references.reserve(replacement_plan.replacement_payloads_by_reference.size());
    for (const auto& replacement : replacement_plan.replacement_payloads_by_reference) {
        target_references.push_back(replacement.first);
    }

    PackageEntryChunkReader targeted_plan_reader(source_chunks);
    const WorksheetInputChunkCallback targeted_plan_source =
        [&](std::string& chunk) { return targeted_plan_reader(chunk); };
    WorksheetTargetedCellRewritePlan targeted_plan;
    try {
        targeted_plan = plan_targeted_cell_rewrites_from_chunk_source(
            targeted_plan_source,
            target_references,
            package_editor_cell_replacement_reader_options());
    } catch (const std::exception&) {
        return false;
    }
    const std::uint64_t target_plan_ms =
        steady_clock_elapsed_milliseconds(phase_started);
    if (!targeted_plan.source_has_top_level_dimension) {
        return false;
    }

    phase_started = std::chrono::steady_clock::now();
    WorksheetPayloadDependencyAuditResult payload_audit =
        worksheet_replacement_cell_payloads_audit(worksheet_part, replacement_plan);
    reject_payload_dependencies_by_policy(
        payload_audit, policy, "worksheet replacement");
    const std::uint64_t payload_audit_ms =
        steady_clock_elapsed_milliseconds(phase_started);

    phase_started = std::chrono::steady_clock::now();
    WorksheetRelationshipReferenceAuditResult relationship_reference_audit =
        worksheet_replacement_cell_payloads_relationship_reference_audit(
            worksheet_part, replacement_plan, worksheet_relationships);
    reject_relationship_reference_audit_by_policy(
        relationship_reference_audit, policy);
    const std::uint64_t relationship_audit_ms =
        steady_clock_elapsed_milliseconds(phase_started);

    phase_started = std::chrono::steady_clock::now();
    IndexedPackageEntryChunkReplacementResult descriptor_result =
        make_indexed_cell_replacement_package_entry_chunks(
            source_chunks, targeted_plan.rewrites, replacement_plan);
    const std::uint64_t descriptor_ms =
        steady_clock_elapsed_milliseconds(phase_started);

    const std::uint64_t output_bytes = descriptor_result.output_bytes;
    const std::size_t matched_replacements =
        descriptor_result.summary.matched_replacement_count;
    const std::uint64_t scanned_source_cell_count =
        targeted_plan.scanned_source_cell_count;
    std::vector<std::string> commit_notes;
    commit_notes.emplace_back(
        "worksheet cell replacement used indexed " + input_label
        + " direct-range staged chunks for a current worksheet input with no "
        "worksheet relationships; "
        "target-only planning scanned "
        + std::to_string(scanned_source_cell_count)
        + " source cells, matched " + std::to_string(matched_replacements)
        + " replacement targets, and staged "
        + std::to_string(output_bytes) + " worksheet bytes");
    if (input_label == "source-entry") {
        commit_notes.emplace_back(
            "worksheet cell replacement indexed source-entry fast path preserves "
            "untouched worksheet XML as source package file ranges and inserts only "
            "replacement cell payload chunks; planned staged chunk inputs, "
            "materialized planned worksheet inputs, missing-target upsert cases, "
            "reference-policy Fail mode, and worksheets with relationships continue "
            "to use the transformer fallback");
    } else if (input_label == "decompressed-source-entry") {
        commit_notes.emplace_back(
            "worksheet cell replacement indexed decompressed-source-entry fast path "
            "inflates the compressed worksheet once into a PackageEditor-owned "
            "temporary file, preserves untouched worksheet XML as ranges of that "
            "file, and inserts only replacement cell payload chunks; memory remains "
            "bounded by chunk buffers rather than worksheet size");
    } else {
        commit_notes.emplace_back(
            "worksheet cell replacement indexed " + input_label
            + " fast path preserves planned worksheet staged chunks without "
            "materializing the rewritten worksheet as staged chunk ranges and "
            "inserts only replacement cell payload chunks; materialized planned "
            "worksheet inputs, missing-target upsert "
            "cases, reference-policy Fail mode, and worksheets with relationships "
            "continue to use the transformer fallback");
    }

    IndexedSourceEntryDirectRangeStats indexed_stats;
    indexed_stats.scanned_source_cell_count = scanned_source_cell_count;
    indexed_stats.matched_replacement_count =
        static_cast<std::uint64_t>(matched_replacements);
    indexed_stats.staged_output_bytes = output_bytes;
    indexed_stats.source_range_chunk_ms = source_preparation_ms;
    indexed_stats.target_plan_ms = target_plan_ms;
    indexed_stats.payload_audit_ms = payload_audit_ms;
    indexed_stats.relationship_audit_ms = relationship_audit_ms;
    indexed_stats.descriptor_ms = descriptor_ms;
    replace_worksheet_part_prevalidated_chunks(std::move(worksheet_part),
        std::move(descriptor_result.chunks), policy,
        std::move(payload_audit.notes), std::move(payload_audit.audits),
        std::move(relationship_reference_audit.notes),
        std::move(relationship_reference_audit.audits),
        "target worksheet part indexed direct-range stream rewrite from worksheet cell replacement",
        false, false, std::move(commit_notes), std::move(owned_temporary_file),
        PartWriteMode::StreamRewrite, indexed_stats);
    return true;
}

void PackageEditor::replace_worksheet_cells_impl(PartName worksheet_part,
    std::span<const WorksheetCellReplacement> replacements,
    const ReferencePolicy& policy,
    WorksheetCellReplacementMode mode)
{
    const bool replace_or_insert =
        mode == WorksheetCellReplacementMode::ReplaceOrInsert;
    const std::string operation_label =
        replace_or_insert ? "worksheet cell upsert" : "worksheet cell replacement";

    if (replacements.empty()) {
        throw FastXlsxError(operation_label + " requires at least one replacement");
    }

    const PartName target_worksheet_part = worksheet_part;
    const auto* worksheet = manifest_.find_part(worksheet_part);
    if (worksheet == nullptr) {
        throw FastXlsxError(
            operation_label + " target is not present in the source package for "
            + worksheet_part_diagnostic_context(target_worksheet_part));
    }
    if (worksheet->content_type != content_type_worksheet) {
        throw FastXlsxError(
            operation_label + " target is not a worksheet part for "
            + worksheet_part_diagnostic_context(target_worksheet_part));
    }

    const CurrentWorksheetInputSource input_source =
        require_current_worksheet_input_source(reader_, replacements_, entry_replacements_,
            target_worksheet_part, operation_label);
    const bool planned_chunk_source =
        input_source.kind == CurrentWorksheetInputSourceKind::PlannedChunks;
    const bool source_entry_chunk_source =
        input_source.kind == CurrentWorksheetInputSourceKind::SourceEntry;

    const WorksheetCellReplacementPlan replacement_plan =
        make_worksheet_cell_replacement_plan(replacements);
    if (source_entry_chunk_source) {
        const PackageReaderEntry* source_entry =
            reader_.find_entry(target_worksheet_part.zip_path());
        if (source_entry != nullptr
            && source_entry->compression_method == stored_compression_method
            && source_entry->compressed_size == source_entry->uncompressed_size) {
            std::vector<PackageEntryChunk> source_chunks =
                source_stored_entry_range_chunks(reader_, target_worksheet_part.zip_path(),
                    "indexed worksheet cell replacement source-entry fast path");
            if (try_replace_worksheet_cells_with_indexed_chunks(
                    worksheet_part, std::move(source_chunks), replacement_plan, policy,
                    "source-entry")) {
                return;
            }
        } else if (!replace_or_insert && source_entry != nullptr
            && source_entry->compression_method == deflate_compression_method
            && policy.unsupported_linked_part_action != ReferencePolicyAction::Fail) {
            const RelationshipSet* worksheet_relationships =
                reader_.relationships_for(target_worksheet_part);
            if (worksheet_relationships == nullptr || worksheet_relationships->empty()) {
                const auto source_preparation_started =
                    std::chrono::steady_clock::now();
                ScopedPackageEditorTempFile decompressed_source_file;
                reader_.extract_entry_to_file(
                    target_worksheet_part.zip_path(), decompressed_source_file.path());
                const std::uint64_t source_preparation_ms =
                    steady_clock_elapsed_milliseconds(source_preparation_started);

                PackageEntryChunk source_chunk =
                    PackageEntryChunk::file(decompressed_source_file.path());
                source_chunk.has_expected_size = true;
                source_chunk.expected_size = source_entry->uncompressed_size;
                source_chunk.has_expected_crc32 = true;
                source_chunk.expected_crc32 = source_entry->crc32;
                std::vector<PackageEntryChunk> source_chunks;
                source_chunks.push_back(std::move(source_chunk));
                if (try_replace_worksheet_cells_with_indexed_chunks(
                        worksheet_part, std::move(source_chunks), replacement_plan, policy,
                        "decompressed-source-entry", decompressed_source_file.path(),
                        source_preparation_ms)) {
                    decompressed_source_file.release();
                    return;
                }
            }
        }
    }

    ScopedPackageEditorTempFile temp_file;
    CurrentWorksheetInputChunkReader transform_reader(reader_, target_worksheet_part, input_source,
        std::string(worksheet_cell_replacement_output_input_context));
    const WorksheetInputChunkCallback transform_source =
        [&](std::string& chunk) { return transform_reader(chunk); };
    const auto transform_started = std::chrono::steady_clock::now();
    SinglePassWorksheetTransformResult transform_result;
    try {
        transform_result = write_worksheet_cell_transform_single_pass(
            temp_file.path(), target_worksheet_part,
            reader_.relationships_for(target_worksheet_part),
            transform_source, replacement_plan, mode);
    } catch (const std::exception& error) {
        throw FastXlsxError(
            current_worksheet_input_failure_message("single-pass transform",
                worksheet_cell_replacement_output_input_context, error.what()));
    }
    const std::uint64_t transform_ms =
        steady_clock_elapsed_milliseconds(transform_started);

    if (!replace_or_insert
        && !transform_result.analysis.summary.missing_cell_references.empty()) {
        throw FastXlsxError(missing_cell_replacement_error(
            transform_result.analysis.summary.missing_cell_references));
    }
    if (replace_or_insert
        && !transform_result.analysis.summary.missing_cell_references.empty()) {
        throw FastXlsxError(
            "worksheet cell upsert failed to emit requested cells: "
            + cell_reference_list(
                transform_result.analysis.summary.missing_cell_references));
    }

    reject_relationship_reference_audit_by_policy(
        transform_result.relationship_reference_audit, policy);
    reject_payload_dependencies_by_policy(
        transform_result.analysis.payload_audit, policy, "worksheet replacement");

    const std::uint64_t dimension_xml_bytes =
        static_cast<std::uint64_t>(transform_result.dimension_xml.size());
    if (dimension_xml_bytes
        > std::numeric_limits<std::uint64_t>::max()
            - transform_result.temporary_output_bytes) {
        throw FastXlsxError("single-pass worksheet staged output size overflow");
    }
    const std::uint64_t staged_output_bytes =
        transform_result.temporary_output_bytes + dimension_xml_bytes;
    std::vector<PackageEntryChunk> output_chunks =
        make_dimension_inserted_worksheet_chunks(temp_file.path(),
            transform_result.temporary_output_bytes,
            transform_result.dimension_insertion_offset,
            std::move(transform_result.dimension_xml));

    std::string replacement_reason =
        "target worksheet part single-pass file-backed stream rewrite from "
        + operation_label;

    std::vector<std::string> commit_notes;
    if (source_entry_chunk_source) {
        commit_notes.emplace_back(
            operation_label + " uses one source-order PackageReader ZIP-entry scan to "
            "transform cells, collect dependency/relationship audits, validate the root, "
            "and compute the exact emitted-cell dimension without materializing source XML");
    } else if (planned_chunk_source) {
        commit_notes.emplace_back(
            operation_label + " uses one source-order scan of current planned worksheet "
            "chunks to transform cells, collect audits, validate the root, and compute the "
            "exact emitted-cell dimension without materializing staged worksheet XML");
    } else {
        commit_notes.emplace_back(
            operation_label + " uses one source-order worksheet transform and audit scan");
    }
    commit_notes.emplace_back(
        "worksheet cell replacement writes transformed XML without the source dimension "
        "to one PackageEditor-owned temporary file, then stages sequential file ranges "
        "around one bounded in-memory exact dimension tag; memory remains bounded by "
        "event/chunk buffers rather than worksheet size");
    commit_notes.emplace_back(
        "worksheet cell replacement refreshed worksheet dimension from emitted cell "
        "references; range-bearing metadata such as autoFilter, tables, drawings, "
        "definedNames, and formulas is not recalculated or repaired");
    commit_notes.emplace_back(
        "worksheet cell replacement builds one prevalidated non-owning replacement lookup "
        "plan and consumes it in one transform pass without rebuilding selector lookup or "
        "reparsing bounded replacement cell payloads");
    if (replace_or_insert) {
        commit_notes.emplace_back(
            "worksheet cell upsert replaces existing cells, inserts missing cells into "
            "source-order rows, and synthesizes missing rows as minimal row records; "
            "it does not shift rows or repair range-bearing worksheet metadata");
    }

    SinglePassWorksheetTransformStats single_pass_stats;
    single_pass_stats.scanned_source_cell_count =
        transform_result.analysis.scanned_source_cell_count;
    single_pass_stats.matched_replacement_count =
        static_cast<std::uint64_t>(
            transform_result.analysis.summary.matched_replacement_count);
    single_pass_stats.inserted_cell_count =
        static_cast<std::uint64_t>(
            transform_result.analysis.summary.inserted_cell_count);
    single_pass_stats.staged_output_bytes = staged_output_bytes;
    single_pass_stats.transform_ms = transform_ms;
    replace_worksheet_part_prevalidated_chunks(std::move(worksheet_part),
        std::move(output_chunks), policy,
        std::move(transform_result.analysis.payload_audit.notes),
        std::move(transform_result.analysis.payload_audit.audits),
        std::move(transform_result.relationship_reference_audit.notes),
        std::move(transform_result.relationship_reference_audit.audits),
        std::move(replacement_reason), true, true, std::move(commit_notes),
        temp_file.path(), PartWriteMode::StreamRewrite, std::nullopt,
        single_pass_stats);
    temp_file.release();
}

void PackageEditor::replace_worksheet_cells(PartName worksheet_part,
    std::span<const WorksheetCellReplacement> replacements, const ReferencePolicy& policy)
{
    replace_worksheet_cells_impl(
        std::move(worksheet_part), replacements, policy,
        WorksheetCellReplacementMode::ReplaceExisting);
}

void PackageEditor::replace_or_insert_worksheet_cells(PartName worksheet_part,
    std::span<const WorksheetCellReplacement> replacements, const ReferencePolicy& policy)
{
    replace_worksheet_cells_impl(
        std::move(worksheet_part), replacements, policy,
        WorksheetCellReplacementMode::ReplaceOrInsert);
}

void PackageEditor::replace_worksheet_cells_by_name(std::string_view sheet_name,
    std::span<const WorksheetCellReplacement> replacements, const ReferencePolicy& policy)
{
    const PartName worksheet_part =
        resolve_worksheet_part_by_name_for_patch(
            reader_, manifest_, replacements_, sheet_name);
    try {
        replace_worksheet_cells(worksheet_part, replacements, policy);
    } catch (const std::exception& error) {
        throw FastXlsxError(
            by_name_worksheet_operation_context(
                "by-name worksheet cell replacement", sheet_name, worksheet_part)
            + ": " + error.what());
    }
}

void PackageEditor::replace_or_insert_worksheet_cells_by_name(std::string_view sheet_name,
    std::span<const WorksheetCellReplacement> replacements, const ReferencePolicy& policy)
{
    const PartName worksheet_part =
        resolve_worksheet_part_by_name_for_patch(
            reader_, manifest_, replacements_, sheet_name);
    try {
        replace_or_insert_worksheet_cells(worksheet_part, replacements, policy);
    } catch (const std::exception& error) {
        throw FastXlsxError(
            by_name_worksheet_operation_context(
                "by-name worksheet cell upsert", sheet_name, worksheet_part)
            + ": " + error.what());
    }
}

void PackageEditor::rename_sheet_catalog_entry(
    std::string_view old_name,
    std::string new_name,
    const ReferencePolicy& policy,
    SheetCatalogRenameOptions options)
{
    validate_sheet_catalog_rename_target(new_name);

    const PartName workbook_part = reader_.workbook_part();
    if (manifest_.find_part(workbook_part) == nullptr) {
        throw FastXlsxError(
            "workbook sheet catalog rename requires the officeDocument workbook part in the planned package");
    }

    std::string workbook_xml = current_planned_materialized_workbook_xml(
        reader_, replacements_, "workbook sheet catalog rename");
    const std::vector<WorkbookSheetReference> sheets =
        reader_.workbook_sheets_from_xml(workbook_xml);
    const WorkbookSheetReference target =
        select_sheet_catalog_rename_target(sheets, old_name, new_name);
    const bool rewrite_defined_names =
        options.formula_policy == SheetCatalogRenameFormulaPolicy::RewriteDefinedNames;
    const bool has_direct_defined_names =
        has_direct_workbook_child_tag(workbook_xml, "definedNames");

    if (!rewrite_defined_names
        && policy.unsupported_linked_part_action == ReferencePolicyAction::Fail
        && has_direct_defined_names) {
        throw FastXlsxError(
            "workbook sheet catalog rename does not update workbook definedNames");
    }

    std::size_t value_begin = 0;
    std::size_t value_end = 0;
    if (!find_workbook_sheet_name_attribute_value(
            workbook_xml, target, value_begin, value_end)) {
        throw FastXlsxError("workbook sheet catalog rename target attribute was not found");
    }

    workbook_xml.replace(
        value_begin, value_end - value_begin, escape_xml_attribute(new_name));

    bool defined_names_rewritten = false;
    if (rewrite_defined_names && has_direct_defined_names) {
        std::vector<FormulaSheetReferenceRewrite> rewrites {
            FormulaSheetReferenceRewrite {
                std::string(old_name),
                new_name,
            },
        };
        rewrites.insert(rewrites.end(), options.extra_formula_rewrites.begin(),
            options.extra_formula_rewrites.end());
        const std::string rewritten_workbook_xml =
            rewrite_workbook_defined_name_formula_references(workbook_xml, rewrites);
        defined_names_rewritten = rewritten_workbook_xml != workbook_xml;
        workbook_xml = rewritten_workbook_xml;
    }

    const std::string rewrite_reason = rewrite_defined_names
        ? "workbook sheet catalog name attribute and direct definedName formula "
          "references local-DOM rewrite; worksheet formulas, tables, drawings, "
          "charts, hyperlinks, and relationship targets are preserved for caller review"
        : "workbook sheet catalog name attribute local-DOM rewrite; definedNames, "
          "formulas, tables, drawings, charts, hyperlinks, and relationship targets "
          "are preserved for caller review";
    PackageManifest updated_manifest = manifest_;
    EditPlan updated_edit_plan = edit_plan_;
    std::vector<PackagePartReplacement> updated_replacements = replacements_;
    std::vector<PackageEntryReplacement> updated_entry_replacements = entry_replacements_;
    std::vector<std::string> updated_omitted_entries = omitted_entries_;

    updated_edit_plan.set_part(
        workbook_part, PartWriteMode::LocalDomRewrite, rewrite_reason);
    if (rewrite_defined_names) {
        updated_edit_plan.add_note(
            "workbook sheet catalog rename rewrites direct workbook definedName "
            "formula references with an opt-in narrow policy; worksheet formulas, "
            "tables, drawings, charts, hyperlinks, relationship targets, "
            "sharedStrings, styles, and calcChain are not synchronized");
    } else {
        updated_edit_plan.add_note(
            "workbook sheet catalog rename only rewrites the workbook <sheets><sheet "
            "name> attribute; definedNames, formulas, tables, drawings, charts, "
            "hyperlinks, relationship targets, sharedStrings, styles, and calcChain "
            "are not synchronized");
    }
    updated_edit_plan.add_workbook_payload_dependency_audit(WorkbookPayloadDependencyAudit {
        workbook_part,
        WorkbookPayloadDependencyAuditKind::SheetCatalog,
        WorkbookPayloadDependencyAuditScope::SheetCatalogRename,
        "sheets/sheet@name",
        "workbook sheet catalog rename rewrites only the sheet name attribute",
    });
    updated_edit_plan.add_workbook_payload_dependency_audit(WorkbookPayloadDependencyAudit {
        workbook_part,
        WorkbookPayloadDependencyAuditKind::DefinedNames,
        WorkbookPayloadDependencyAuditScope::SheetCatalogRename,
        "definedNames",
        rewrite_defined_names
            ? (defined_names_rewritten
                    ? "workbook sheet catalog rename rewrites direct definedName formula references with opt-in narrow sync"
                    : "workbook sheet catalog rename checked direct definedName formula references with opt-in narrow sync; no direct references changed")
            : "workbook sheet catalog rename preserves definedNames without semantic sync",
    });
    upsert_part_replacement(updated_replacements, workbook_part, std::move(workbook_xml),
        PartWriteMode::LocalDomRewrite,
        rewrite_reason,
        &workbook_part);
    remove_entry_replacement(updated_entry_replacements, workbook_part.zip_path());
    remove_omitted_entry(updated_omitted_entries, workbook_part.zip_path());
    updated_manifest.set_part_write_mode(workbook_part, PartWriteMode::LocalDomRewrite);
    audit_preserved_relationship_entry(updated_edit_plan, reader_, workbook_part);

#ifdef FASTXLSX_ENABLE_TEST_HOOKS
    run_package_editor_sheet_rename_staged_hook();
#endif

    commit_package_editor_staged_state(manifest_, edit_plan_, replacements_,
        entry_replacements_, omitted_entries_, updated_manifest, updated_edit_plan,
        updated_replacements, updated_entry_replacements, updated_omitted_entries);
}

void PackageEditor::request_full_calculation(CalcChainAction calc_chain_action)
{
    if (calc_chain_action == CalcChainAction::Rebuild) {
        throw FastXlsxError("calcChain rebuild is not implemented for workbook calc metadata");
    }

    const PartName workbook_part = reader_.workbook_part();
    if (manifest_.find_part(workbook_part) == nullptr
        || reader_.find_entry(workbook_part.zip_path()) == nullptr) {
        throw FastXlsxError("request_full_calculation requires the officeDocument workbook part");
    }

    std::string updated_workbook_xml = request_full_calculation_in_workbook_xml(
        current_planned_materialized_workbook_xml(
            reader_, replacements_, "workbook calc metadata update"));

    const PartName calc_chain_part("/xl/calcChain.xml");
    PackageManifest updated_manifest = manifest_;
    EditPlan updated_edit_plan = edit_plan_;
    std::vector<PackagePartReplacement> updated_replacements = replacements_;
    std::vector<PackageEntryReplacement> updated_entry_replacements = entry_replacements_;
    std::vector<std::string> updated_omitted_entries = omitted_entries_;
    EditPlan calc_chain_removal_plan;
    bool remove_calc_chain = false;
    bool omit_calc_chain = false;
    bool rewrite_content_types = false;
    bool rewrite_workbook_relationships = false;

    if (calc_chain_action == CalcChainAction::Remove) {
        remove_calc_chain = manifest_.find_part(calc_chain_part) != nullptr;
        if (remove_calc_chain) {
            calc_chain_removal_plan = PartRewritePlanner(manifest_).plan_part_removal(
                calc_chain_part,
                "calcChain.xml removed because workbook full calculation was requested");
        }

        omit_calc_chain = reader_.find_entry(calc_chain_part.zip_path()) != nullptr;
        rewrite_content_types = updated_manifest.remove_part(calc_chain_part);

        if (RelationshipSet* workbook_relationships =
                updated_manifest.relationships_for(workbook_part)) {
            rewrite_workbook_relationships =
                workbook_relationships->remove_by_type(relationship_type_calc_chain) > 0;
        }
    }

    updated_edit_plan.request_full_calculation(calc_chain_action);
    updated_edit_plan.set_part(workbook_part, PartWriteMode::LocalDomRewrite,
        "workbook calcPr fullCalcOnLoad updated by workbook metadata helper; definedNames preserved for policy review");
    updated_edit_plan.add_workbook_payload_dependency_audit(WorkbookPayloadDependencyAudit {
        workbook_part,
        WorkbookPayloadDependencyAuditKind::CalcMetadata,
        WorkbookPayloadDependencyAuditScope::WorkbookCalcMetadataRewrite,
        "calcPr",
        "workbook calc metadata helper rewrites direct workbook calcPr fullCalcOnLoad",
    });
    updated_edit_plan.add_workbook_payload_dependency_audit(WorkbookPayloadDependencyAudit {
        workbook_part,
        WorkbookPayloadDependencyAuditKind::DefinedNames,
        WorkbookPayloadDependencyAuditScope::WorkbookCalcMetadataRewrite,
        "definedNames",
        "workbook calc metadata helper preserves definedNames without semantic sync",
    });
    upsert_part_replacement(updated_replacements, workbook_part,
        std::move(updated_workbook_xml), PartWriteMode::LocalDomRewrite,
        "workbook calcPr fullCalcOnLoad updated by workbook metadata helper; definedNames preserved for policy review",
        &workbook_part);
    remove_entry_replacement(updated_entry_replacements, workbook_part.zip_path());
    remove_omitted_entry(updated_omitted_entries, workbook_part.zip_path());
    updated_manifest.set_part_write_mode(workbook_part, PartWriteMode::LocalDomRewrite);

    if (calc_chain_action == CalcChainAction::Remove) {
        const std::string calc_chain_relationship_entry =
            relationship_entry_name_for_source_part(calc_chain_part);
        remove_part_replacement(updated_replacements, calc_chain_part);
        remove_entry_replacement(updated_entry_replacements, calc_chain_part.zip_path());
        remove_entry_replacement(updated_entry_replacements, calc_chain_relationship_entry);

        if (remove_calc_chain) {
            merge_removed_part_audit(
                updated_edit_plan, calc_chain_removal_plan, calc_chain_part);
            for (const std::string& note : calc_chain_removal_plan.notes()) {
                updated_edit_plan.add_note(note);
            }
        }

        if (omit_calc_chain) {
            add_omitted_part_entries(updated_omitted_entries, calc_chain_part);
            if (reader_.find_entry(calc_chain_relationship_entry) != nullptr) {
                updated_edit_plan.remove_package_entry(calc_chain_relationship_entry,
                    "calcChain-owned relationships omitted with removed calcChain part",
                    PackageEntryAuditKind::SourceRelationships, calc_chain_part.value());
            }
        }

        if (rewrite_content_types) {
            upsert_entry_replacement(reader_, updated_entry_replacements, "[Content_Types].xml",
                serialize_content_types(updated_manifest.content_types()));
            updated_edit_plan.set_package_entry("[Content_Types].xml",
                PartWriteMode::LocalDomRewrite,
                "content types updated for workbook calcChain removal",
                PackageEntryAuditKind::ContentTypes);
        }

        if (rewrite_workbook_relationships) {
            RelationshipSet* workbook_relationships =
                updated_manifest.relationships_for(workbook_part);
            const std::string workbook_relationship_entry =
                relationship_entry_name_for_source_part(workbook_part);
            upsert_entry_replacement(reader_, updated_entry_replacements,
                workbook_relationship_entry,
                serialize_relationships(*workbook_relationships));
            updated_edit_plan.set_package_entry(workbook_relationship_entry,
                PartWriteMode::LocalDomRewrite,
                "workbook relationships updated for workbook calcChain removal",
                PackageEntryAuditKind::SourceRelationships, workbook_part.value());
        } else {
            audit_preserved_relationship_entry(updated_edit_plan, reader_, workbook_part);
        }
    } else {
        audit_preserved_relationship_entry(updated_edit_plan, reader_, workbook_part);
        audit_preserved_relationship_entry(updated_edit_plan, reader_, calc_chain_part);
    }

#ifdef FASTXLSX_ENABLE_TEST_HOOKS
    run_package_editor_calc_metadata_staged_hook();
#endif

    commit_package_editor_staged_state(manifest_, edit_plan_, replacements_,
        entry_replacements_, omitted_entries_, updated_manifest, updated_edit_plan,
        updated_replacements, updated_entry_replacements, updated_omitted_entries);
}

void PackageEditor::set_document_properties(const DocumentProperties& properties)
{
    const PartName core_part("/docProps/core.xml");
    const PartName app_part("/docProps/app.xml");

    PackageManifest updated_manifest = manifest_;
    const bool content_types_changed =
        ensure_generated_part(updated_manifest, core_part, content_type_core_properties)
        | ensure_generated_part(updated_manifest, app_part, content_type_extended_properties);
    const bool package_relationships_changed =
        ensure_package_relationship(
            updated_manifest, relationship_type_core_properties, "docProps/core.xml")
        | ensure_package_relationship(
            updated_manifest, relationship_type_extended_properties, "docProps/app.xml");

    std::string core_properties_xml = build_core_properties(properties);
    std::string extended_properties_xml = build_extended_properties(properties);
    require_materialized_part_replacement_payload_size(core_part,
        core_properties_xml.size(), PartWriteMode::GenerateSmallXml,
        "core document properties replacement");
    require_materialized_part_replacement_payload_size(app_part,
        extended_properties_xml.size(), PartWriteMode::GenerateSmallXml,
        "extended document properties replacement");

    std::string content_types_xml;
    if (content_types_changed) {
        content_types_xml = serialize_content_types(updated_manifest.content_types());
        require_materialized_package_entry_replacement_payload_size(reader_,
            content_types_entry_name, content_types_xml.size(),
            "document properties content types metadata replacement");
    }

    std::string package_relationships_xml;
    if (package_relationships_changed) {
        package_relationships_xml =
            serialize_relationships(updated_manifest.package_relationships());
        require_materialized_package_entry_replacement_payload_size(reader_,
            package_relationships_entry_name, package_relationships_xml.size(),
            "document properties package relationships metadata replacement");
    }

    EditPlan updated_edit_plan = edit_plan_;
    std::vector<PackagePartReplacement> updated_replacements = replacements_;
    std::vector<PackageEntryReplacement> updated_entry_replacements = entry_replacements_;
    std::vector<std::string> updated_omitted_entries = omitted_entries_;

    updated_edit_plan.set_part(core_part, PartWriteMode::GenerateSmallXml,
        "core document properties generated small XML");
    updated_edit_plan.set_part(app_part, PartWriteMode::GenerateSmallXml,
        "extended document properties generated small XML");
    upsert_part_replacement(updated_replacements, core_part, std::move(core_properties_xml),
        PartWriteMode::GenerateSmallXml, "core document properties generated small XML");
    upsert_part_replacement(updated_replacements, app_part, std::move(extended_properties_xml),
        PartWriteMode::GenerateSmallXml, "extended document properties generated small XML");
    remove_entry_replacement(updated_entry_replacements, core_part.zip_path());
    remove_entry_replacement(updated_entry_replacements, app_part.zip_path());
    remove_omitted_entry(updated_omitted_entries, core_part.zip_path());
    remove_omitted_entry(updated_omitted_entries, app_part.zip_path());

    if (content_types_changed) {
        upsert_entry_replacement(reader_, updated_entry_replacements, "[Content_Types].xml",
            std::move(content_types_xml));
        updated_edit_plan.set_package_entry(
            "[Content_Types].xml", PartWriteMode::LocalDomRewrite,
            "content types updated for document properties",
            PackageEntryAuditKind::ContentTypes);
    }
    if (package_relationships_changed) {
        upsert_entry_replacement(reader_, updated_entry_replacements, "_rels/.rels",
            std::move(package_relationships_xml));
        updated_edit_plan.set_package_entry(
            "_rels/.rels", PartWriteMode::LocalDomRewrite,
            "package relationships updated for document properties",
            PackageEntryAuditKind::PackageRelationships);
    }

#ifdef FASTXLSX_ENABLE_TEST_HOOKS
    run_package_editor_document_properties_staged_hook();
#endif

    commit_package_editor_staged_state(manifest_, edit_plan_, replacements_,
        entry_replacements_, omitted_entries_, updated_manifest, updated_edit_plan,
        updated_replacements, updated_entry_replacements, updated_omitted_entries);
}

PackageEditorOutputPlan PackageEditor::planned_output() const
{
    return planned_output(
        PackageWriterOptions {PackageWriterBackend::StoredZipBootstrap});
}

PackageEditorOutputPlan PackageEditor::planned_output(PackageWriterOptions options) const
{
    PackageEditorOutputPlan plan;
    plan.full_calculation_on_load = edit_plan_.full_calculation_on_load();
    plan.calc_chain_action = edit_plan_.calc_chain_action();
    plan.notes = edit_plan_.notes();
    plan.removed_parts = edit_plan_.removed_parts();
    plan.removed_package_entries = edit_plan_.removed_package_entries();
    plan.relationship_target_audits = edit_plan_.relationship_target_audits();
    plan.worksheet_relationship_reference_audits =
        edit_plan_.worksheet_relationship_reference_audits();
    plan.worksheet_payload_dependency_audits =
        edit_plan_.worksheet_payload_dependency_audits();
    plan.workbook_payload_dependency_audits =
        edit_plan_.workbook_payload_dependency_audits();

    std::vector<PackageEditorOutputEntryPlan>& entries = plan.entries;
    entries.reserve(reader_.entries().size() + entry_replacements_.size() + replacements_.size());

    for (const PackageReaderEntry& entry : reader_.entries()) {
        entries.push_back(make_output_entry_plan(reader_, edit_plan_, replacements_,
            entry_replacements_, omitted_entries_, entry.name, true));
        PackageEditorOutputEntryPlan& output_entry = entries.back();
        if (output_entry.copied_from_source && !output_entry.omitted) {
            output_entry.source_compression_method = entry.compression_method;
            output_entry.raw_compressed_source_copy =
                package_writer_can_raw_copy_compression_method(
                    options, entry.compression_method);
            output_entry.raw_compressed_source_bytes =
                output_entry.raw_compressed_source_copy ? entry.compressed_size : 0;
        }
    }

    for (const PackageEntryReplacement& replacement : entry_replacements_) {
        if (contains_entry_name(omitted_entries_, replacement.entry_name)
            || reader_.find_entry(replacement.entry_name) != nullptr
            || contains_planned_output_entry(entries, replacement.entry_name)) {
            continue;
        }
        entries.push_back(make_output_entry_plan(reader_, edit_plan_, replacements_,
            entry_replacements_, omitted_entries_, replacement.entry_name, false));
    }

    for (const PackagePartReplacement& replacement : replacements_) {
        const std::string entry_name = replacement.part_name.zip_path();
        if (contains_entry_name(omitted_entries_, entry_name)
            || reader_.find_entry(entry_name) != nullptr
            || contains_planned_output_entry(entries, entry_name)) {
            continue;
        }
        entries.push_back(make_output_entry_plan(reader_, edit_plan_, replacements_,
            entry_replacements_, omitted_entries_, entry_name, false));
    }

    return plan;
}

std::vector<PackageEditorOutputEntryPlan> PackageEditor::planned_output_entries() const
{
    return planned_output().entries;
}

std::vector<PackageEditorOutputEntryPlan> PackageEditor::planned_output_entries(
    PackageWriterOptions options) const
{
    return planned_output(options).entries;
}

void PackageEditor::save_as(
    const std::filesystem::path& path, PackageWriterOptions options) const
{
    if (path.empty()) {
        throw FastXlsxError("PackageEditor output path cannot be empty");
    }
    if (path_is_existing_directory(path)) {
        throw FastXlsxError("PackageEditor output path is an existing directory");
    }
    if (path_parent_is_not_directory(path)) {
        throw FastXlsxError("PackageEditor output parent path is not an existing directory");
    }
    if (same_existing_path(reader_.path(), path)) {
        throw FastXlsxError("PackageEditor cannot save over the source package");
    }

    std::vector<std::filesystem::path> temporary_source_files;
    std::filesystem::path temporary_output_path;

    try {
        {
            const PackageEditorOutputPlan plan = planned_output(options);
            std::vector<PackageEntry> output_entries;
            output_entries.reserve(plan.entries.size());
            for (const PackageEditorOutputEntryPlan& entry_plan : plan.entries) {
                if (entry_plan.omitted) {
                    continue;
                }
                output_entries.push_back(
                    materialize_planned_output_entry_with_context(reader_, replacements_,
                        entry_replacements_, entry_plan, temporary_source_files));
            }
#ifdef FASTXLSX_ENABLE_TEST_HOOKS
            run_package_editor_source_copy_temp_files_hook(temporary_source_files);
#endif
            temporary_output_path = make_package_editor_output_sibling_path(path, "output");
            write_package(temporary_output_path, output_entries, options);
        }
        commit_package_editor_output_file(temporary_output_path, path);
    } catch (const std::exception& error) {
        remove_temporary_files(temporary_source_files);
        remove_package_editor_output_file_noexcept(temporary_output_path);
        throw FastXlsxError("failed to write PackageEditor output package '"
            + path.generic_string() + "': " + error.what());
    }
    remove_temporary_files(temporary_source_files);
}

#ifdef FASTXLSX_ENABLE_TEST_HOOKS
void testing_set_package_editor_source_copy_temp_files_hook(
    PackageEditorSourceCopyTempFilesHook hook) noexcept
{
    package_editor_source_copy_temp_files_hook() = hook;
}

void testing_set_package_editor_calc_metadata_staged_hook(
    PackageEditorCalcMetadataStagedHook hook) noexcept
{
    package_editor_calc_metadata_staged_hook() = hook;
}

void testing_set_package_editor_sheet_rename_staged_hook(
    PackageEditorSheetRenameStagedHook hook) noexcept
{
    package_editor_sheet_rename_staged_hook() = hook;
}

void testing_set_package_editor_document_properties_staged_hook(
    PackageEditorDocumentPropertiesStagedHook hook) noexcept
{
    package_editor_document_properties_staged_hook() = hook;
}

void testing_set_package_editor_part_removal_staged_hook(
    PackageEditorPartRemovalStagedHook hook) noexcept
{
    package_editor_part_removal_staged_hook() = hook;
}

void testing_set_package_editor_materialized_part_replacement_staged_hook(
    PackageEditorMaterializedPartReplacementStagedHook hook) noexcept
{
    package_editor_materialized_part_replacement_staged_hook() = hook;
}

void testing_set_package_editor_chunk_part_replacement_staged_hook(
    PackageEditorChunkPartReplacementStagedHook hook) noexcept
{
    package_editor_chunk_part_replacement_staged_hook() = hook;
}

void testing_set_package_editor_worksheet_part_replacement_staged_hook(
    PackageEditorWorksheetPartReplacementStagedHook hook) noexcept
{
    package_editor_worksheet_part_replacement_staged_hook() = hook;
}
#endif

} // namespace fastxlsx::detail
