#include "package_editor.hpp"

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
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fastxlsx::detail {
namespace {

constexpr std::string_view content_type_worksheet =
    "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml";
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

void validate_sheet_data_replacement_xml(std::string_view sheet_data_xml)
{
    const std::size_t first = sheet_data_xml.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        throw FastXlsxError("sheetData replacement XML is empty");
    }
    if (sheet_data_xml[first] != '<') {
        throw FastXlsxError("sheetData replacement XML must start with a sheetData element");
    }

    XmlTagRange sheet_data;
    if (!find_start_tag(sheet_data_xml, "sheetData", sheet_data) || sheet_data.open != first) {
        throw FastXlsxError("sheetData replacement XML must start with a sheetData element");
    }

    std::size_t sheet_data_end = sheet_data.close + 1;
    if (!is_self_closing_tag(sheet_data_xml, sheet_data)
        && !find_closing_tag_end_after(
            sheet_data_xml, "sheetData", sheet_data.close + 1, sheet_data_end)) {
        throw FastXlsxError("sheetData replacement XML closing tag is missing");
    }

    for (std::size_t offset = sheet_data_end; offset < sheet_data_xml.size(); ++offset) {
        if (!is_xml_space(sheet_data_xml[offset])) {
            throw FastXlsxError("sheetData replacement XML must contain only one sheetData element");
        }
    }
}

void validate_worksheet_replacement_xml(std::string_view worksheet_xml)
{
    const std::size_t first = worksheet_xml.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        throw FastXlsxError("worksheet replacement XML is empty");
    }
    if (worksheet_xml[first] != '<') {
        throw FastXlsxError("worksheet replacement XML must start with a worksheet element");
    }

    XmlTagRange worksheet;
    if (!find_start_tag(worksheet_xml, "worksheet", worksheet)
        || !prefix_is_xml_prolog_only(worksheet_xml, worksheet.open)) {
        throw FastXlsxError("worksheet replacement XML must start with a worksheet element");
    }

    std::size_t worksheet_end = worksheet.close + 1;
    if (!is_self_closing_tag(worksheet_xml, worksheet)
        && !find_closing_tag_end_after(
            worksheet_xml, "worksheet", worksheet.close + 1, worksheet_end)) {
        throw FastXlsxError("worksheet replacement XML closing tag is missing");
    }

    for (std::size_t offset = worksheet_end; offset < worksheet_xml.size(); ++offset) {
        if (!is_xml_space(worksheet_xml[offset])) {
            throw FastXlsxError("worksheet replacement XML must contain only one worksheet element");
        }
    }
}

std::string replace_sheet_data_in_worksheet_xml(
    std::string worksheet_xml, std::string_view sheet_data_xml)
{
    validate_sheet_data_replacement_xml(sheet_data_xml);

    XmlTagRange sheet_data;
    if (!find_start_tag(worksheet_xml, "sheetData", sheet_data)) {
        throw FastXlsxError("worksheet XML sheetData element is missing");
    }

    std::size_t sheet_data_end = sheet_data.close + 1;
    if (!is_self_closing_tag(worksheet_xml, sheet_data)
        && !find_closing_tag_end_after(
            worksheet_xml, "sheetData", sheet_data.close + 1, sheet_data_end)) {
        throw FastXlsxError("worksheet XML sheetData closing tag is missing");
    }

    worksheet_xml.replace(sheet_data.open, sheet_data_end - sheet_data.open, sheet_data_xml);
    return worksheet_xml;
}

bool has_start_tag_for_audit(std::string_view xml, std::string_view local_name) noexcept
{
    XmlTagRange tag;
    try {
        return find_start_tag(xml, local_name, tag);
    } catch (const std::exception&) {
        return false;
    }
}

bool cell_attribute_equals_for_audit(std::string_view sheet_data_xml,
    std::string_view attribute_name, std::string_view expected_value) noexcept
{
    try {
        std::size_t offset = 0;
        XmlTagRange cell;
        while (find_start_tag_after(sheet_data_xml, "c", offset, cell)) {
            std::size_t value_begin = 0;
            std::size_t value_end = 0;
            if (find_attribute_value(
                    sheet_data_xml, cell, attribute_name, value_begin, value_end)
                && sheet_data_xml.substr(value_begin, value_end - value_begin)
                    == expected_value) {
                return true;
            }
        }
    } catch (const std::exception&) {
        return false;
    }
    return false;
}

bool cell_has_attribute_for_audit(
    std::string_view sheet_data_xml, std::string_view attribute_name) noexcept
{
    try {
        std::size_t offset = 0;
        XmlTagRange cell;
        while (find_start_tag_after(sheet_data_xml, "c", offset, cell)) {
            std::size_t value_begin = 0;
            std::size_t value_end = 0;
            if (find_attribute_value(
                    sheet_data_xml, cell, attribute_name, value_begin, value_end)) {
                return true;
            }
        }
    } catch (const std::exception&) {
        return false;
    }
    return false;
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
        for (std::size_t offset = 0;;) {
            const std::size_t open = xml.find('<', offset);
            if (open == std::string_view::npos) {
                break;
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

            const std::size_t close = find_xml_tag_end(xml, open);
            const char marker = xml[open + 1];
            const XmlTagRange tag {open, close};
            if (marker == '/') {
                if (namespace_stack_.empty()) {
                    throw FastXlsxError("small XML part closing tag has no matching start tag");
                }
                namespace_stack_.pop_back();
                offset = close + 1;
                continue;
            }
            if (marker == '?' || marker == '!') {
                offset = close + 1;
                continue;
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
            offset = close + 1;
        }
    }

    [[nodiscard]] const std::vector<WorksheetRelationshipReference>& references() const noexcept
    {
        return references_;
    }

private:
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

    std::vector<XmlNamespaceScope> namespace_stack_;
    std::vector<WorksheetRelationshipReference> references_;
};

void scan_xml_relationship_references(
    WorksheetRelationshipReferenceScanner& scanner, std::string_view xml)
{
    scanner.process(xml);
}

void scan_rewritten_cell_replacement_relationship_references(
    WorksheetRelationshipReferenceScanner& scanner,
    std::string_view worksheet_xml,
    std::span<const WorksheetCellReplacement> replacements)
{
    const WorksheetTransformSummary summary = scan_cell_replacement_actions(
        worksheet_xml, replacements, [&](const WorksheetTransformAction& action) {
            scan_xml_relationship_references(scanner,
                action.kind == WorksheetTransformActionKind::ReplaceCell
                    ? action.replacement_cell_xml
                    : action.raw_xml);
        });
    (void)summary;
}

std::vector<WorksheetRelationshipReference> worksheet_relationship_references(
    std::string_view worksheet_xml)
{
    WorksheetRelationshipReferenceScanner scanner;
    scanner.process(worksheet_xml);
    return scanner.references();
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

WorksheetRelationshipReferenceAuditResult worksheet_relationship_reference_audit(
    const PartName& worksheet_part, std::string_view worksheet_xml,
    const RelationshipSet* worksheet_relationships)
{
    try {
        return worksheet_relationship_reference_audit_from_references(
            worksheet_part, worksheet_relationship_references(worksheet_xml),
            worksheet_relationships);
    } catch (const std::exception&) {
        return worksheet_relationship_reference_parse_failure_audit();
    }
}

WorksheetRelationshipReferenceAuditResult worksheet_cell_replacement_relationship_reference_audit(
    const PartName& worksheet_part,
    std::string_view worksheet_xml,
    std::span<const WorksheetCellReplacement> replacements,
    const RelationshipSet* worksheet_relationships)
{
    try {
        WorksheetRelationshipReferenceScanner scanner;
        scan_rewritten_cell_replacement_relationship_references(
            scanner, worksheet_xml, replacements);
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

WorksheetPayloadDependencyAuditResult worksheet_sheet_data_preservation_audit(
    const PartName& worksheet_part, std::string_view worksheet_xml)
{
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

    WorksheetPayloadDependencyAuditResult result;
    for (const WorksheetLocalMetadataAudit& audit : audits) {
        if (!has_start_tag_for_audit(worksheet_xml, audit.element)) {
            continue;
        }

        std::string note = "sheetData replacement preserved ";
        note += audit.description;
        note += "; ranges or references may require caller review";
        append_worksheet_payload_dependency_audit(result, worksheet_part,
            is_relationship_metadata_element(audit.element)
                ? WorksheetPayloadDependencyAuditKind::RelationshipMetadata
                : WorksheetPayloadDependencyAuditKind::RangeMetadata,
            WorksheetPayloadDependencyAuditScope::PreservedWorksheetMetadata,
            audit.element, std::move(note));
    }
    return result;
}

WorksheetPayloadDependencyAuditResult worksheet_sheet_data_replacement_audit(
    const PartName& worksheet_part, std::string_view sheet_data_xml)
{
    WorksheetPayloadDependencyAuditResult result;
    if (cell_attribute_equals_for_audit(sheet_data_xml, "t", "s")) {
        append_worksheet_payload_dependency_audit(result, worksheet_part,
            WorksheetPayloadDependencyAuditKind::SharedStrings,
            WorksheetPayloadDependencyAuditScope::SheetDataReplacement, "c",
            "sheetData replacement uses shared string indexes; caller must ensure xl/sharedStrings.xml remains valid");
    }
    if (cell_has_attribute_for_audit(sheet_data_xml, "s")) {
        append_worksheet_payload_dependency_audit(result, worksheet_part,
            WorksheetPayloadDependencyAuditKind::Styles,
            WorksheetPayloadDependencyAuditScope::SheetDataReplacement, "c",
            "sheetData replacement uses style id references; caller must ensure xl/styles.xml remains valid");
    }
    if (has_start_tag_for_audit(sheet_data_xml, "f")) {
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

WorksheetPayloadDependencyAuditResult worksheet_replacement_payload_audit(
    const PartName& worksheet_part, std::string_view worksheet_xml)
{
    WorksheetPayloadDependencyAuditResult result;
    if (cell_attribute_equals_for_audit(worksheet_xml, "t", "s")) {
        append_worksheet_replacement_shared_strings_audit(result, worksheet_part);
    }
    if (cell_has_attribute_for_audit(worksheet_xml, "s")) {
        append_worksheet_replacement_styles_audit(result, worksheet_part);
    }
    if (has_start_tag_for_audit(worksheet_xml, "f")) {
        append_worksheet_replacement_formula_audit(result, worksheet_part);
    }

    for (const WorksheetLocalMetadataAudit& audit : worksheet_replacement_range_metadata_audits) {
        if (!has_start_tag_for_audit(worksheet_xml, audit.element)) {
            continue;
        }

        append_worksheet_replacement_range_metadata_audit(
            result, worksheet_part, audit.element, audit.description);
    }

    for (const WorksheetLocalMetadataAudit& audit : worksheet_replacement_relationship_metadata_audits) {
        if (!has_start_tag_for_audit(worksheet_xml, audit.element)) {
            continue;
        }

        append_worksheet_replacement_relationship_metadata_audit(
            result, worksheet_part, audit.element, audit.description);
    }
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

void upsert_part_replacement(std::vector<PackagePartReplacement>& replacements,
    PartName part_name, std::string data, PartWriteMode write_mode, std::string reason)
{
    if (auto* replacement = find_replacement(replacements, part_name)) {
        replacement->data = std::move(data);
        replacement->chunks.clear();
        replacement->write_mode = write_mode;
        replacement->reason = std::move(reason);
        return;
    }

    replacements.push_back(PackagePartReplacement {
        std::move(part_name),
        std::move(data),
        {},
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

void upsert_entry_replacement(
    std::vector<PackageEntryReplacement>& replacements, std::string entry_name, std::string data)
{
    if (auto* replacement = find_entry_replacement(replacements, entry_name)) {
        replacement->data = std::move(data);
        return;
    }
    replacements.push_back(PackageEntryReplacement {std::move(entry_name), std::move(data)});
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

    upsert_entry_replacement(entry_replacements, std::string(content_types_entry_name),
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

std::string current_planned_part_data(const PackageReader& reader,
    const std::vector<PackagePartReplacement>& replacements,
    const std::vector<PackageEntryReplacement>& entry_replacements,
    const PartName& part_name)
{
    if (const auto* entry_replacement =
            find_entry_replacement(entry_replacements, part_name.zip_path())) {
        return entry_replacement->data;
    }
    if (const auto* replacement = find_replacement(replacements, part_name.zip_path())) {
        if (!replacement->chunks.empty()) {
            throw FastXlsxError(
                "planned package part uses staged chunks and cannot be materialized");
        }
        return replacement->data;
    }
    return reader.read_entry(part_name.zip_path());
}

std::string current_planned_part_data_from_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw FastXlsxError("failed to open PackageReader file-backed worksheet source");
    }

    std::string data;
    std::array<char, 64U * 1024U> buffer {};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize read_size = input.gcount();
        if (read_size > 0) {
            data.append(buffer.data(), static_cast<std::size_t>(read_size));
        }
    }
    if (input.bad()) {
        throw FastXlsxError("failed to read PackageReader file-backed worksheet source");
    }
    return data;
}

std::uint64_t current_planned_part_data_size(const PackageReader& reader,
    const std::vector<PackagePartReplacement>& replacements,
    const std::vector<PackageEntryReplacement>& entry_replacements,
    const PartName& part_name)
{
    if (const auto* entry_replacement =
            find_entry_replacement(entry_replacements, part_name.zip_path())) {
        return entry_replacement->data.size();
    }
    if (const auto* replacement = find_replacement(replacements, part_name.zip_path())) {
        if (!replacement->chunks.empty()) {
            throw FastXlsxError(
                "planned package part uses staged chunks and cannot be measured as materialized XML");
        }
        return replacement->data.size();
    }
    const PackageReaderEntry* entry = reader.find_entry(part_name.zip_path());
    if (entry == nullptr) {
        throw FastXlsxError("worksheet sheetData replacement source entry is missing");
    }
    return entry->uncompressed_size;
}

bool current_planned_part_is_source_entry(
    const std::vector<PackagePartReplacement>& replacements,
    const std::vector<PackageEntryReplacement>& entry_replacements,
    const PartName& part_name) noexcept
{
    return find_entry_replacement(entry_replacements, part_name.zip_path()) == nullptr
        && find_replacement(replacements, part_name.zip_path()) == nullptr;
}

PartName resolve_worksheet_part_by_name_for_patch(const PackageReader& reader,
    const PackageManifest& manifest, const std::vector<PackagePartReplacement>& replacements,
    const std::vector<PackageEntryReplacement>& entry_replacements,
    std::string_view sheet_name)
{
    const PartName workbook_part("/xl/workbook.xml");
    if (manifest.find_part(workbook_part) == nullptr) {
        throw FastXlsxError(
            "worksheet by-name patch requires a planned workbook sheet catalog; "
            "xl/workbook.xml has been removed");
    }
    if (find_entry_replacement(entry_replacements, workbook_part.zip_path()) != nullptr
        || find_replacement(replacements, workbook_part.zip_path()) != nullptr) {
        return reader.worksheet_part_by_sheet_name_from_xml(
            sheet_name,
            current_planned_part_data(reader, replacements, entry_replacements,
                workbook_part));
    }

    return reader.worksheet_part_by_sheet_name(sheet_name);
}

void require_sheet_data_local_rewrite_size(
    std::string_view payload_name, std::uint64_t byte_size)
{
    if (byte_size
        <= static_cast<std::uint64_t>(
            package_editor_sheet_data_local_rewrite_byte_limit)) {
        return;
    }

    throw FastXlsxError(
        "worksheet sheetData replacement exceeds bounded local rewrite limit for "
        + std::string(payload_name)
        + "; current helper materializes planned worksheet XML and is not the "
          "large-file streaming worksheet transformer");
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

struct WorksheetCellReplacementStreamAnalysis {
    WorksheetTransformSummary summary;
    WorksheetDimensionScan dimension_scan;
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

WorksheetCellReplacementStreamAnalysis analyze_worksheet_cell_replacement_stream(
    const PartName& worksheet_part,
    std::string_view worksheet_xml,
    std::span<const WorksheetCellReplacement> replacements)
{
    validate_worksheet_replacement_xml(worksheet_xml);

    WorksheetCellReplacementStreamAnalysis analysis;
    analysis.summary = scan_cell_replacement_actions(
        worksheet_xml, replacements, [&](const WorksheetTransformAction& action) {
            if (action.kind == WorksheetTransformActionKind::ReplaceCell) {
                if (!action.cell_reference.empty()) {
                    include_dimension_cell(analysis.dimension_scan,
                        parse_cell_reference_coordinate(action.cell_reference));
                }
                append_payload_audit_result(analysis.payload_audit,
                    worksheet_replacement_payload_audit(
                        worksheet_part, action.replacement_cell_xml));
                return;
            }

            const std::size_t event_offset =
                static_cast<std::size_t>(action.raw_xml.data() - worksheet_xml.data());
            if (action.event_kind == WorksheetEventKind::WorksheetStart
                && analysis.worksheet_start_end == std::string_view::npos) {
                analysis.worksheet_start_end = event_offset + action.raw_xml.size();
                analysis.worksheet_prefix = element_prefix(action.raw_xml);
                return;
            }
            if (action.event_kind == WorksheetEventKind::Metadata) {
                audit_worksheet_replacement_metadata_event(
                    analysis.payload_audit, worksheet_part, action.element_name);
                if (action.element_name == "dimension") {
                    if (analysis.dimension_begin == std::string_view::npos
                        && !is_closing_raw_tag(action.raw_xml)) {
                        analysis.dimension_begin = event_offset;
                        analysis.dimension_prefix = element_prefix(action.raw_xml);
                        if (action.self_closing) {
                            analysis.dimension_end = event_offset + action.raw_xml.size();
                        }
                        return;
                    }
                    if (analysis.dimension_begin != std::string_view::npos
                        && analysis.dimension_end == std::string_view::npos
                        && is_closing_raw_tag(action.raw_xml)) {
                        analysis.dimension_end = event_offset + action.raw_xml.size();
                        return;
                    }
                }
                return;
            }
            if (action.event_kind == WorksheetEventKind::CellStart) {
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
        });

    if (analysis.worksheet_start_end == std::string_view::npos) {
        throw FastXlsxError("worksheet dimension refresh requires a worksheet root");
    }
    if (analysis.dimension_begin != std::string_view::npos
        && analysis.dimension_end == std::string_view::npos) {
        throw FastXlsxError("worksheet dimension refresh found an unclosed dimension element");
    }

    append_worksheet_replacement_range_metadata_audit(analysis.payload_audit, worksheet_part,
        "dimension", "worksheet dimension metadata");
    return analysis;
}

void write_file_chunk(std::ofstream& output, std::string_view chunk)
{
    while (!chunk.empty()) {
        const std::size_t write_size = std::min<std::size_t>(chunk.size(),
            static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()));
        output.write(chunk.data(), static_cast<std::streamsize>(write_size));
        if (!output) {
            throw FastXlsxError("failed to write temporary worksheet cell replacement XML");
        }
        chunk.remove_prefix(write_size);
    }
}

void write_worksheet_cell_replacement_stream(const std::filesystem::path& path,
    std::string_view worksheet_xml,
    std::span<const WorksheetCellReplacement> replacements,
    const WorksheetCellReplacementStreamAnalysis& analysis)
{
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw FastXlsxError("failed to create temporary worksheet cell replacement XML");
    }

    const std::string dimension_reference =
        worksheet_dimension_reference(analysis.dimension_scan);
    bool skipping_dimension = false;

    (void)scan_cell_replacement_actions(
        worksheet_xml, replacements, [&](const WorksheetTransformAction& action) {
            if (skipping_dimension) {
                if (action.event_kind == WorksheetEventKind::Metadata
                    && action.element_name == "dimension"
                    && is_closing_raw_tag(action.raw_xml)) {
                    skipping_dimension = false;
                }
                return;
            }

            if (action.kind == WorksheetTransformActionKind::ReplaceCell) {
                write_file_chunk(output, action.replacement_cell_xml);
                return;
            }

            if (action.event_kind == WorksheetEventKind::WorksheetStart) {
                write_file_chunk(output, action.raw_xml);
                if (analysis.dimension_begin == std::string_view::npos) {
                    write_file_chunk(output,
                        dimension_tag(analysis.worksheet_prefix, dimension_reference));
                }
                return;
            }

            if (action.event_kind == WorksheetEventKind::Metadata
                && action.element_name == "dimension"
                && !is_closing_raw_tag(action.raw_xml)) {
                write_file_chunk(output,
                    dimension_tag(analysis.dimension_prefix, dimension_reference));
                if (!action.self_closing) {
                    skipping_dimension = true;
                }
                return;
            }

            write_file_chunk(output, action.raw_xml);
        });

    output.close();
    if (!output) {
        throw FastXlsxError("failed to finalize temporary worksheet cell replacement XML");
    }
}

std::string refresh_worksheet_dimension_for_cell_replacements(std::string worksheet_xml)
{
    std::string_view worksheet_view(worksheet_xml);
    WorksheetDimensionScan dimension_scan;
    std::size_t worksheet_start_end = std::string::npos;
    std::string worksheet_prefix;
    std::size_t dimension_begin = std::string::npos;
    std::size_t dimension_end = std::string::npos;
    std::string dimension_prefix;

    scan_worksheet_events(worksheet_view, [&](const WorksheetEvent& event) {
        const std::size_t event_offset =
            static_cast<std::size_t>(event.raw_xml.data() - worksheet_view.data());
        if (event.kind == WorksheetEventKind::WorksheetStart
            && worksheet_start_end == std::string::npos) {
            worksheet_start_end = event_offset + event.raw_xml.size();
            worksheet_prefix = element_prefix(event.raw_xml);
            return;
        }
        if (event.kind == WorksheetEventKind::Metadata && event.element_name == "dimension") {
            if (dimension_begin == std::string::npos && !is_closing_raw_tag(event.raw_xml)) {
                dimension_begin = event_offset;
                dimension_prefix = element_prefix(event.raw_xml);
                if (event.self_closing) {
                    dimension_end = event_offset + event.raw_xml.size();
                }
                return;
            }
            if (dimension_begin != std::string::npos && dimension_end == std::string::npos
                && is_closing_raw_tag(event.raw_xml)) {
                dimension_end = event_offset + event.raw_xml.size();
                return;
            }
        }
        if (event.kind == WorksheetEventKind::CellStart && !event.cell_reference.empty()) {
            include_dimension_cell(
                dimension_scan, parse_cell_reference_coordinate(event.cell_reference));
        }
    });

    if (worksheet_start_end == std::string::npos) {
        throw FastXlsxError("worksheet dimension refresh requires a worksheet root");
    }
    if (dimension_begin != std::string::npos && dimension_end == std::string::npos) {
        throw FastXlsxError("worksheet dimension refresh found an unclosed dimension element");
    }

    const std::string reference = worksheet_dimension_reference(dimension_scan);
    if (dimension_begin != std::string::npos) {
        worksheet_xml.replace(dimension_begin,
            dimension_end - dimension_begin,
            dimension_tag(dimension_prefix, reference));
        return worksheet_xml;
    }

    worksheet_xml.insert(worksheet_start_end, dimension_tag(worksheet_prefix, reference));
    return worksheet_xml;
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

    if (find_entry_replacement(entry_replacements, plan.entry_name) != nullptr) {
        plan.copied_from_source = false;
        if (plan.write_mode == PartWriteMode::CopyOriginal
            && find_part_plan_by_entry_name(edit_plan, plan.entry_name) == nullptr
            && edit_plan.find_package_entry(plan.entry_name) == nullptr) {
            plan.write_mode = PartWriteMode::LocalDomRewrite;
            plan.reason = "package entry replacement";
        }
        return plan;
    }

    if (const auto* replacement = find_replacement(replacements, plan.entry_name)) {
        plan.package_part = true;
        plan.part_name = replacement->part_name.value();
        plan.write_mode = replacement->write_mode;
        plan.generated = replacement->write_mode == PartWriteMode::GenerateSmallXml;
        if (plan.reason.empty()) {
            plan.reason = replacement->reason;
        }
        plan.copied_from_source = false;
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
    return plan;
}

PackageEntry materialize_planned_output_entry(const PackageReader& reader,
    const std::vector<PackagePartReplacement>& replacements,
    const std::vector<PackageEntryReplacement>& entry_replacements,
    const PackageEditorOutputEntryPlan& plan)
{
    if (plan.omitted) {
        throw FastXlsxError("omitted package entry cannot be materialized");
    }
    if (const auto* entry_replacement =
            find_entry_replacement(entry_replacements, plan.entry_name)) {
        return PackageEntry {plan.entry_name, entry_replacement->data};
    }
    if (const auto* replacement = find_replacement(replacements, plan.entry_name)) {
        if (!replacement->chunks.empty()) {
            return PackageEntry {plan.entry_name, replacement->chunks};
        }
        return PackageEntry {plan.entry_name, replacement->data};
    }
    if (plan.source_entry) {
        try {
            return PackageEntry {plan.entry_name, reader.read_entry(plan.entry_name)};
        } catch (const std::exception& error) {
            throw FastXlsxError("failed to copy source package entry '" + plan.entry_name
                + "': " + error.what());
        }
    }
    throw FastXlsxError("planned output entry has no payload: " + plan.entry_name);
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

    std::filesystem::path release() noexcept
    {
        released_ = true;
        return path_;
    }

private:
    std::filesystem::path path_;
    bool released_ = false;
};

} // namespace

PackageEditor PackageEditor::open(std::filesystem::path path)
{
    return PackageEditor(PackageReader::open(std::move(path)));
}

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

void PackageEditor::replace_part(
    PartName part_name, std::string data, PartWriteMode write_mode, std::string reason)
{
    if (write_mode == PartWriteMode::CopyOriginal) {
        throw FastXlsxError("replacement part cannot use copy-original write mode");
    }
    if (is_metadata_part_replacement_target(part_name)) {
        throw FastXlsxError("metadata package entries cannot be replaced as ordinary parts");
    }

    const PartName workbook_part("/xl/workbook.xml");
    if (part_name == workbook_part && edit_plan_.full_calculation_on_load()) {
        data = request_full_calculation_in_workbook_xml(std::move(data));
    }

    if (manifest_.find_part(part_name) == nullptr) {
        const PackagePart* source_part = reader_.part_index().find_part(part_name);
        if (source_part == nullptr) {
            throw FastXlsxError("replacement part is not present in the source package");
        }
        restore_source_part_manifest_state(manifest_, reader_, *source_part);
    }

    if (reason.empty()) {
        reason = default_replacement_reason(write_mode);
    }

    upsert_part_replacement(replacements_, part_name, std::move(data), write_mode, reason);

    manifest_.set_part_write_mode(part_name, write_mode);
    edit_plan_.set_part(part_name, write_mode, reason);
    restore_active_part_entry_state_after_replacement(edit_plan_, entry_replacements_,
        omitted_entries_, reader_, manifest_, part_name);
}

void PackageEditor::replace_part_chunks(
    PartName part_name, std::vector<PackageEntryChunk> chunks, std::string reason)
{
    if (chunks.empty()) {
        throw FastXlsxError("staged package part replacement requires at least one chunk");
    }
    if (part_name == PartName("/xl/workbook.xml") && edit_plan_.full_calculation_on_load()) {
        throw FastXlsxError(
            "staged workbook replacement cannot apply fullCalcOnLoad metadata");
    }
    if (reason.empty()) {
        reason = "target package part staged stream rewrite";
    }

    const PartName target_part = part_name;
    replace_part(part_name, std::string {}, PartWriteMode::StreamRewrite, std::move(reason));
    PackagePartReplacement* replacement = find_replacement(replacements_, target_part);
    if (replacement == nullptr) {
        throw FastXlsxError("staged package part replacement was not recorded");
    }
    replacement->data.clear();
    replacement->chunks = std::move(chunks);
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
    const bool rewrite_content_types =
        updated_manifest.content_types().override_for(part_name) != nullptr;
    const bool removed_manifest_state = updated_manifest.remove_part(part_name);
    if (!removed_manifest_state) {
        throw FastXlsxError("part removal did not update package manifest state");
    }

    merge_removed_part_audit(edit_plan_, removal_plan, part_name);
    for (const std::string& note : removal_plan.notes()) {
        edit_plan_.add_note(note);
    }

    stage_part_removal_entries(edit_plan_, replacements_, entry_replacements_,
        omitted_entries_, reader_, part_name);

    if (rewrite_content_types) {
        upsert_entry_replacement(entry_replacements_, "[Content_Types].xml",
            serialize_content_types(updated_manifest.content_types()));
        edit_plan_.set_package_entry("[Content_Types].xml", PartWriteMode::LocalDomRewrite,
            "content types updated for explicit part removal",
            PackageEntryAuditKind::ContentTypes);
    }

    manifest_ = std::move(updated_manifest);
}

void PackageEditor::replace_worksheet_part(
    PartName worksheet_part, std::string worksheet_xml, const ReferencePolicy& policy)
{
    replace_worksheet_part_impl(
        std::move(worksheet_part), std::move(worksheet_xml), policy, true);
}

void PackageEditor::replace_worksheet_part_chunks(PartName worksheet_part,
    std::string worksheet_xml, std::vector<PackageEntryChunk> chunks,
    const ReferencePolicy& policy)
{
    if (chunks.empty()) {
        throw FastXlsxError("staged worksheet replacement requires at least one chunk");
    }

    const PartName target_worksheet_part = worksheet_part;
    replace_worksheet_part_impl(
        std::move(worksheet_part), std::move(worksheet_xml), policy, true);

    PackagePartReplacement* replacement = find_replacement(replacements_, target_worksheet_part);
    if (replacement == nullptr) {
        throw FastXlsxError("staged worksheet replacement was not recorded");
    }

    const std::string replacement_reason =
        "target worksheet part staged stream rewrite chunks; current helper uses materialized XML for validation and audit";
    replacement->data.clear();
    replacement->chunks = std::move(chunks);
    replacement->write_mode = PartWriteMode::StreamRewrite;
    replacement->reason = replacement_reason;
    manifest_.set_part_write_mode(target_worksheet_part, PartWriteMode::StreamRewrite);
    edit_plan_.set_part(target_worksheet_part, PartWriteMode::StreamRewrite, replacement_reason);
    edit_plan_.add_note(
        "worksheet staged chunk replacement uses materialized worksheet XML for "
        "validation, dependency audit, and calc metadata; it is not the final "
        "low-memory package-entry staged worksheet transformer");
}

void PackageEditor::replace_worksheet_part_prevalidated_chunks(PartName worksheet_part,
    std::vector<PackageEntryChunk> chunks, const ReferencePolicy& policy,
    std::vector<std::string> payload_notes,
    std::vector<WorksheetPayloadDependencyAudit> payload_audits,
    std::vector<std::string> relationship_reference_notes,
    std::vector<WorksheetRelationshipReferenceAudit> relationship_reference_audits,
    std::string replacement_reason)
{
    if (chunks.empty()) {
        throw FastXlsxError("staged worksheet replacement requires at least one chunk");
    }

    const PartName target_worksheet_part = worksheet_part;
    const auto* worksheet = manifest_.find_part(worksheet_part);
    if (worksheet == nullptr) {
        throw FastXlsxError("worksheet replacement target is not present in the source package");
    }
    if (worksheet->content_type != content_type_worksheet) {
        throw FastXlsxError("worksheet replacement target is not a worksheet part");
    }
    if (policy.calc_chain_action == CalcChainAction::Rebuild) {
        throw FastXlsxError("calcChain rebuild is not implemented for worksheet replacement");
    }

    const PartName workbook_part("/xl/workbook.xml");
    if (manifest_.find_part(workbook_part) == nullptr
        || reader_.find_entry(workbook_part.zip_path()) == nullptr) {
        throw FastXlsxError("worksheet replacement requires xl/workbook.xml");
    }

    WorksheetPayloadDependencyAuditResult payload_audit;
    payload_audit.notes = std::move(payload_notes);
    payload_audit.audits = std::move(payload_audits);
    WorksheetRelationshipReferenceAuditResult relationship_reference_audit;
    relationship_reference_audit.notes = std::move(relationship_reference_notes);
    relationship_reference_audit.audits = std::move(relationship_reference_audits);
    reject_relationship_reference_audit_by_policy(relationship_reference_audit, policy);
    reject_payload_dependencies_by_policy(
        payload_audit, policy, "worksheet replacement");

    const EditPlan worksheet_plan =
        PartRewritePlanner(manifest_).plan_worksheet_stream_rewrite(worksheet_part, policy);
    PackageManifest updated_manifest = manifest_;

    std::string updated_workbook_xml;
    if (worksheet_plan.full_calculation_on_load()) {
        updated_workbook_xml = request_full_calculation_in_workbook_xml(
            current_planned_part_data(reader_, replacements_, entry_replacements_, workbook_part));
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
    replace_part(std::move(worksheet_part), std::string {}, PartWriteMode::StreamRewrite,
        replacement_reason);
    PackagePartReplacement* replacement =
        find_replacement(replacements_, target_worksheet_part);
    if (replacement == nullptr) {
        throw FastXlsxError("staged worksheet replacement was not recorded");
    }
    replacement->data.clear();
    replacement->chunks = std::move(chunks);
    replacement->write_mode = PartWriteMode::StreamRewrite;
    replacement->reason = replacement_reason;

    merge_copy_original_dependency_reasons(edit_plan_, worksheet_plan);
    audit_preserved_relationship_entries(
        edit_plan_, reader_, worksheet_plan, target_worksheet_part);

    if (worksheet_plan.full_calculation_on_load()) {
        edit_plan_.request_full_calculation(worksheet_plan.calc_chain_action());
        edit_plan_.set_part(workbook_part, PartWriteMode::LocalDomRewrite,
            "workbook calcPr fullCalcOnLoad updated for worksheet rewrite; definedNames preserved for policy review");
        upsert_entry_replacement(entry_replacements_, workbook_part.zip_path(),
            std::move(updated_workbook_xml));
        remove_part_replacement(replacements_, workbook_part);
        if (!rewrite_workbook_relationships) {
            audit_preserved_relationship_entry(edit_plan_, reader_, workbook_part);
        }
    }

    if (remove_calc_chain) {
        const std::string calc_chain_relationship_entry =
            relationship_entry_name_for_source_part(calc_chain_part);
        remove_part_replacement(replacements_, calc_chain_part);
        remove_entry_replacement(entry_replacements_, calc_chain_part.zip_path());
        remove_entry_replacement(entry_replacements_, calc_chain_relationship_entry);
        merge_removed_part_audit(edit_plan_, worksheet_plan, calc_chain_part);
    }

    if (omit_calc_chain) {
        const std::string calc_chain_relationship_entry =
            relationship_entry_name_for_source_part(calc_chain_part);
        add_omitted_part_entries(omitted_entries_, calc_chain_part);
        if (reader_.find_entry(calc_chain_relationship_entry) != nullptr) {
            edit_plan_.remove_package_entry(calc_chain_relationship_entry,
                "calcChain-owned relationships omitted with removed calcChain part",
                PackageEntryAuditKind::SourceRelationships, calc_chain_part.value());
        }
    }

    if (rewrite_content_types) {
        upsert_entry_replacement(entry_replacements_, "[Content_Types].xml",
            serialize_content_types(updated_manifest.content_types()));
        edit_plan_.set_package_entry("[Content_Types].xml", PartWriteMode::LocalDomRewrite,
            "content types updated for worksheet calcChain removal",
            PackageEntryAuditKind::ContentTypes);
    }

    if (rewrite_workbook_relationships) {
        RelationshipSet* workbook_relationships =
            updated_manifest.relationships_for(workbook_part);
        const std::string workbook_relationship_entry =
            relationship_entry_name_for_source_part(workbook_part);
        upsert_entry_replacement(entry_replacements_, workbook_relationship_entry,
            serialize_relationships(*workbook_relationships));
        edit_plan_.set_package_entry(workbook_relationship_entry, PartWriteMode::LocalDomRewrite,
            "workbook relationships updated for worksheet calcChain removal",
            PackageEntryAuditKind::SourceRelationships, workbook_part.value());
    }

    updated_manifest.set_part_write_mode(target_worksheet_part, PartWriteMode::StreamRewrite);
    if (worksheet_plan.full_calculation_on_load()) {
        updated_manifest.set_part_write_mode(workbook_part, PartWriteMode::LocalDomRewrite);
    }
    for (const std::string& note : worksheet_plan.notes()) {
        edit_plan_.add_note(note);
    }
    for (std::string& note : payload_audit.notes) {
        edit_plan_.add_note(std::move(note));
    }
    for (std::string& note : relationship_reference_audit.notes) {
        edit_plan_.add_note(std::move(note));
    }
    for (const RelationshipTargetAudit& audit : worksheet_plan.relationship_target_audits()) {
        edit_plan_.add_relationship_target_audit(audit);
    }
    for (const WorkbookPayloadDependencyAudit& audit :
        worksheet_plan.workbook_payload_dependency_audits()) {
        edit_plan_.add_workbook_payload_dependency_audit(audit);
    }
    for (WorksheetRelationshipReferenceAudit& audit :
        relationship_reference_audit.audits) {
        edit_plan_.add_worksheet_relationship_reference_audit(std::move(audit));
    }
    for (WorksheetPayloadDependencyAudit& audit : payload_audit.audits) {
        edit_plan_.add_worksheet_payload_dependency_audit(std::move(audit));
    }

    manifest_ = std::move(updated_manifest);
}

void PackageEditor::replace_worksheet_part_impl(PartName worksheet_part,
    std::string worksheet_xml, const ReferencePolicy& policy, bool enforce_payload_policy)
{
    const PartName target_worksheet_part = worksheet_part;
    const auto* worksheet = manifest_.find_part(worksheet_part);
    if (worksheet == nullptr) {
        throw FastXlsxError("worksheet replacement target is not present in the source package");
    }
    if (worksheet->content_type != content_type_worksheet) {
        throw FastXlsxError("worksheet replacement target is not a worksheet part");
    }
    if (policy.calc_chain_action == CalcChainAction::Rebuild) {
        throw FastXlsxError("calcChain rebuild is not implemented for worksheet replacement");
    }

    const PartName workbook_part("/xl/workbook.xml");
    if (manifest_.find_part(workbook_part) == nullptr
        || reader_.find_entry(workbook_part.zip_path()) == nullptr) {
        throw FastXlsxError("worksheet replacement requires xl/workbook.xml");
    }
    validate_worksheet_replacement_xml(worksheet_xml);
    const WorksheetPayloadDependencyAuditResult payload_audit =
        worksheet_replacement_payload_audit(target_worksheet_part, worksheet_xml);
    const WorksheetRelationshipReferenceAuditResult relationship_reference_audit =
        worksheet_relationship_reference_audit(
            target_worksheet_part, worksheet_xml, reader_.relationships_for(worksheet_part));
    reject_relationship_reference_audit_by_policy(relationship_reference_audit, policy);
    if (enforce_payload_policy) {
        reject_payload_dependencies_by_policy(
            payload_audit, policy, "worksheet replacement");
    }

    const EditPlan worksheet_plan =
        PartRewritePlanner(manifest_).plan_worksheet_stream_rewrite(worksheet_part, policy);
    PackageManifest updated_manifest = manifest_;

    std::string updated_workbook_xml;
    if (worksheet_plan.full_calculation_on_load()) {
        updated_workbook_xml = request_full_calculation_in_workbook_xml(
            current_planned_part_data(
                reader_, replacements_, entry_replacements_, workbook_part));
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

    replace_part(std::move(worksheet_part), std::move(worksheet_xml),
        PartWriteMode::StreamRewrite, "target worksheet part stream rewrite");
    merge_copy_original_dependency_reasons(edit_plan_, worksheet_plan);
    audit_preserved_relationship_entries(
        edit_plan_, reader_, worksheet_plan, target_worksheet_part);

    if (worksheet_plan.full_calculation_on_load()) {
        edit_plan_.request_full_calculation(worksheet_plan.calc_chain_action());
        edit_plan_.set_part(workbook_part, PartWriteMode::LocalDomRewrite,
            "workbook calcPr fullCalcOnLoad updated for worksheet rewrite; definedNames preserved for policy review");
        upsert_entry_replacement(entry_replacements_, workbook_part.zip_path(),
            std::move(updated_workbook_xml));
        remove_part_replacement(replacements_, workbook_part);
        if (!rewrite_workbook_relationships) {
            audit_preserved_relationship_entry(edit_plan_, reader_, workbook_part);
        }
    }

    if (remove_calc_chain) {
        const std::string calc_chain_relationship_entry =
            relationship_entry_name_for_source_part(calc_chain_part);
        remove_part_replacement(replacements_, calc_chain_part);
        remove_entry_replacement(entry_replacements_, calc_chain_part.zip_path());
        remove_entry_replacement(entry_replacements_, calc_chain_relationship_entry);
        merge_removed_part_audit(edit_plan_, worksheet_plan, calc_chain_part);
    }

    if (omit_calc_chain) {
        const std::string calc_chain_relationship_entry =
            relationship_entry_name_for_source_part(calc_chain_part);
        add_omitted_part_entries(omitted_entries_, calc_chain_part);
        if (reader_.find_entry(calc_chain_relationship_entry) != nullptr) {
            edit_plan_.remove_package_entry(calc_chain_relationship_entry,
                "calcChain-owned relationships omitted with removed calcChain part",
                PackageEntryAuditKind::SourceRelationships, calc_chain_part.value());
        }
    }

    if (rewrite_content_types) {
        upsert_entry_replacement(entry_replacements_, "[Content_Types].xml",
            serialize_content_types(updated_manifest.content_types()));
        edit_plan_.set_package_entry("[Content_Types].xml", PartWriteMode::LocalDomRewrite,
            "content types updated for worksheet calcChain removal",
            PackageEntryAuditKind::ContentTypes);
    }

    if (rewrite_workbook_relationships) {
        RelationshipSet* workbook_relationships =
            updated_manifest.relationships_for(workbook_part);
        const std::string workbook_relationship_entry =
            relationship_entry_name_for_source_part(workbook_part);
        upsert_entry_replacement(entry_replacements_, workbook_relationship_entry,
            serialize_relationships(*workbook_relationships));
        edit_plan_.set_package_entry(workbook_relationship_entry, PartWriteMode::LocalDomRewrite,
            "workbook relationships updated for worksheet calcChain removal",
            PackageEntryAuditKind::SourceRelationships, workbook_part.value());
    }

    updated_manifest.set_part_write_mode(target_worksheet_part, PartWriteMode::StreamRewrite);
    if (worksheet_plan.full_calculation_on_load()) {
        updated_manifest.set_part_write_mode(workbook_part, PartWriteMode::LocalDomRewrite);
    }
    for (const std::string& note : worksheet_plan.notes()) {
        edit_plan_.add_note(note);
    }
    for (const std::string& note : payload_audit.notes) {
        edit_plan_.add_note(note);
    }
    for (const std::string& note : relationship_reference_audit.notes) {
        edit_plan_.add_note(note);
    }
    for (const RelationshipTargetAudit& audit : worksheet_plan.relationship_target_audits()) {
        edit_plan_.add_relationship_target_audit(audit);
    }
    for (const WorkbookPayloadDependencyAudit& audit :
        worksheet_plan.workbook_payload_dependency_audits()) {
        edit_plan_.add_workbook_payload_dependency_audit(audit);
    }
    for (const WorksheetRelationshipReferenceAudit& audit : relationship_reference_audit.audits) {
        edit_plan_.add_worksheet_relationship_reference_audit(audit);
    }
    for (const WorksheetPayloadDependencyAudit& audit : payload_audit.audits) {
        edit_plan_.add_worksheet_payload_dependency_audit(audit);
    }

    manifest_ = std::move(updated_manifest);
}

void PackageEditor::replace_worksheet_part_by_name(
    std::string_view sheet_name, std::string worksheet_xml, const ReferencePolicy& policy)
{
    replace_worksheet_part(
        resolve_worksheet_part_by_name_for_patch(
            reader_, manifest_, replacements_, entry_replacements_, sheet_name),
        std::move(worksheet_xml), policy);
}

void PackageEditor::replace_worksheet_sheet_data(
    PartName worksheet_part, std::string sheet_data_xml, const ReferencePolicy& policy)
{
    const PartName target_worksheet_part = worksheet_part;
    const auto* worksheet = manifest_.find_part(worksheet_part);
    if (worksheet == nullptr) {
        throw FastXlsxError("worksheet sheetData replacement target is not present in the source package");
    }
    if (worksheet->content_type != content_type_worksheet) {
        throw FastXlsxError("worksheet sheetData replacement target is not a worksheet part");
    }

    validate_sheet_data_replacement_xml(sheet_data_xml);
    require_sheet_data_local_rewrite_size(
        "replacement sheetData XML", sheet_data_xml.size());
    require_sheet_data_local_rewrite_size("current planned worksheet XML",
        current_planned_part_data_size(
            reader_, replacements_, entry_replacements_, worksheet_part));

    std::string worksheet_xml = current_planned_part_data(
        reader_, replacements_, entry_replacements_, worksheet_part);
    const WorksheetPayloadDependencyAuditResult preservation_audit =
        worksheet_sheet_data_preservation_audit(target_worksheet_part, worksheet_xml);
    worksheet_xml = replace_sheet_data_in_worksheet_xml(
        std::move(worksheet_xml), sheet_data_xml);
    require_sheet_data_local_rewrite_size("rewritten worksheet XML", worksheet_xml.size());
    const WorksheetPayloadDependencyAuditResult replacement_audit =
        worksheet_sheet_data_replacement_audit(target_worksheet_part, sheet_data_xml);
    reject_payload_dependencies_by_policy(
        replacement_audit, policy, "sheetData replacement");

    replace_worksheet_part_impl(std::move(worksheet_part), std::move(worksheet_xml), policy,
        false);
    const std::string sheet_data_rewrite_reason =
        "target worksheet part local-DOM rewrite from bounded local sheetData replacement; "
        "current helper materializes planned worksheet XML";
    edit_plan_.set_part(
        target_worksheet_part, PartWriteMode::LocalDomRewrite, sheet_data_rewrite_reason);
    manifest_.set_part_write_mode(target_worksheet_part, PartWriteMode::LocalDomRewrite);
    if (auto* replacement = find_replacement(replacements_, target_worksheet_part)) {
        replacement->write_mode = PartWriteMode::LocalDomRewrite;
        replacement->reason = sheet_data_rewrite_reason;
    }
    edit_plan_.add_note(
        "sheetData replacement uses bounded local worksheet XML rewrite; current helper "
        "materializes the planned worksheet XML and is not the large-file streaming "
        "worksheet transformer");
    for (const std::string& note : preservation_audit.notes) {
        edit_plan_.add_note(note);
    }
    for (const std::string& note : replacement_audit.notes) {
        edit_plan_.add_note(note);
    }
    for (const WorksheetPayloadDependencyAudit& audit : preservation_audit.audits) {
        edit_plan_.add_worksheet_payload_dependency_audit(audit);
    }
    for (const WorksheetPayloadDependencyAudit& audit : replacement_audit.audits) {
        edit_plan_.add_worksheet_payload_dependency_audit(audit);
    }
}

void PackageEditor::replace_worksheet_sheet_data_by_name(
    std::string_view sheet_name, std::string sheet_data_xml, const ReferencePolicy& policy)
{
    replace_worksheet_sheet_data(
        resolve_worksheet_part_by_name_for_patch(
            reader_, manifest_, replacements_, entry_replacements_, sheet_name),
        std::move(sheet_data_xml), policy);
}

void PackageEditor::replace_worksheet_cells(PartName worksheet_part,
    std::span<const WorksheetCellReplacement> replacements, const ReferencePolicy& policy)
{
    if (replacements.empty()) {
        throw FastXlsxError("worksheet cell replacement requires at least one replacement");
    }

    const PartName target_worksheet_part = worksheet_part;
    const auto* worksheet = manifest_.find_part(worksheet_part);
    if (worksheet == nullptr) {
        throw FastXlsxError("worksheet cell replacement target is not present in the source package");
    }
    if (worksheet->content_type != content_type_worksheet) {
        throw FastXlsxError("worksheet cell replacement target is not a worksheet part");
    }

    ScopedPackageEditorTempFile source_file;
    bool source_entry_file_backed = false;
    if (current_planned_part_is_source_entry(
            replacements_, entry_replacements_, target_worksheet_part)) {
        reader_.extract_entry_to_file(target_worksheet_part.zip_path(), source_file.path());
        source_entry_file_backed = true;
    }

    const std::string worksheet_xml = source_entry_file_backed
        ? current_planned_part_data_from_file(source_file.path())
        : current_planned_part_data(reader_, replacements_, entry_replacements_, worksheet_part);
    const WorksheetCellReplacementStreamAnalysis stream_analysis =
        analyze_worksheet_cell_replacement_stream(
            target_worksheet_part, worksheet_xml, replacements);
    if (!stream_analysis.summary.missing_cell_references.empty()) {
        throw FastXlsxError(
            missing_cell_replacement_error(stream_analysis.summary.missing_cell_references));
    }

    const std::string replacement_reason =
        "target worksheet part file-backed stream rewrite from worksheet cell replacement";
    const WorksheetRelationshipReferenceAuditResult relationship_reference_audit =
        worksheet_cell_replacement_relationship_reference_audit(
            target_worksheet_part, worksheet_xml, replacements,
            reader_.relationships_for(worksheet_part));
    reject_relationship_reference_audit_by_policy(relationship_reference_audit, policy);
    reject_payload_dependencies_by_policy(
        stream_analysis.payload_audit, policy, "worksheet replacement");

    ScopedPackageEditorTempFile temp_file;
    write_worksheet_cell_replacement_stream(
        temp_file.path(), worksheet_xml, replacements, stream_analysis);

    std::vector<PackageEntryChunk> output_chunks;
    output_chunks.push_back(PackageEntryChunk::file(temp_file.path()));
    temporary_files_.push_back(temp_file.path());
    temp_file.release();
    replace_worksheet_part_prevalidated_chunks(std::move(worksheet_part),
        std::move(output_chunks), policy, stream_analysis.payload_audit.notes,
        stream_analysis.payload_audit.audits, relationship_reference_audit.notes,
        relationship_reference_audit.audits, replacement_reason);
    if (source_entry_file_backed) {
        edit_plan_.add_note(
            "worksheet cell replacement streams dimension-refreshed output to a "
            "PackageEditor-owned temporary file-backed package-entry chunk; source package "
            "worksheet entries are first extracted through the PackageReader file-backed "
            "entry source before the current event reader materializes validation input");
    } else {
        edit_plan_.add_note(
            "worksheet cell replacement streams dimension-refreshed output to a "
            "PackageEditor-owned temporary file-backed package-entry chunk; current planned "
            "worksheet replacement input is still materialized before the event reader runs");
    }
    edit_plan_.add_note(
        "worksheet cell replacement refreshed worksheet dimension from emitted cell "
        "references; range-bearing metadata such as autoFilter, tables, drawings, "
        "definedNames, and formulas is not recalculated or repaired");
}

void PackageEditor::replace_worksheet_cells_by_name(std::string_view sheet_name,
    std::span<const WorksheetCellReplacement> replacements, const ReferencePolicy& policy)
{
    replace_worksheet_cells(
        resolve_worksheet_part_by_name_for_patch(
            reader_, manifest_, replacements_, entry_replacements_, sheet_name),
        replacements, policy);
}

void PackageEditor::rename_sheet_catalog_entry(
    std::string_view old_name, std::string new_name, const ReferencePolicy& policy)
{
    validate_sheet_catalog_rename_target(new_name);

    const PartName workbook_part("/xl/workbook.xml");
    if (manifest_.find_part(workbook_part) == nullptr) {
        throw FastXlsxError(
            "workbook sheet catalog rename requires xl/workbook.xml in the planned package");
    }

    std::string workbook_xml = current_planned_part_data(
        reader_, replacements_, entry_replacements_, workbook_part);
    const std::vector<WorkbookSheetReference> sheets =
        reader_.workbook_sheets_from_xml(workbook_xml);
    const WorkbookSheetReference target =
        select_sheet_catalog_rename_target(sheets, old_name, new_name);

    if (policy.unsupported_linked_part_action == ReferencePolicyAction::Fail
        && has_direct_workbook_child_tag(workbook_xml, "definedNames")) {
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

    edit_plan_.set_part(workbook_part, PartWriteMode::LocalDomRewrite,
        "workbook sheet catalog name attribute local-DOM rewrite; definedNames, "
        "formulas, tables, drawings, charts, hyperlinks, and relationship targets "
        "are preserved for caller review");
    edit_plan_.add_note(
        "workbook sheet catalog rename only rewrites the workbook <sheets><sheet "
        "name> attribute; definedNames, formulas, tables, drawings, charts, "
        "hyperlinks, relationship targets, sharedStrings, styles, and calcChain "
        "are not synchronized");
    edit_plan_.add_workbook_payload_dependency_audit(WorkbookPayloadDependencyAudit {
        workbook_part,
        WorkbookPayloadDependencyAuditKind::SheetCatalog,
        WorkbookPayloadDependencyAuditScope::SheetCatalogRename,
        "sheets/sheet@name",
        "workbook sheet catalog rename rewrites only the sheet name attribute",
    });
    edit_plan_.add_workbook_payload_dependency_audit(WorkbookPayloadDependencyAudit {
        workbook_part,
        WorkbookPayloadDependencyAuditKind::DefinedNames,
        WorkbookPayloadDependencyAuditScope::SheetCatalogRename,
        "definedNames",
        "workbook sheet catalog rename preserves definedNames without semantic sync",
    });
    upsert_entry_replacement(entry_replacements_, workbook_part.zip_path(),
        std::move(workbook_xml));
    remove_part_replacement(replacements_, workbook_part);
    manifest_.set_part_write_mode(workbook_part, PartWriteMode::LocalDomRewrite);
    audit_preserved_relationship_entry(edit_plan_, reader_, workbook_part);
}

void PackageEditor::request_full_calculation(CalcChainAction calc_chain_action)
{
    if (calc_chain_action == CalcChainAction::Rebuild) {
        throw FastXlsxError("calcChain rebuild is not implemented for workbook calc metadata");
    }

    const PartName workbook_part("/xl/workbook.xml");
    if (manifest_.find_part(workbook_part) == nullptr
        || reader_.find_entry(workbook_part.zip_path()) == nullptr) {
        throw FastXlsxError("request_full_calculation requires xl/workbook.xml");
    }

    std::string updated_workbook_xml = request_full_calculation_in_workbook_xml(
        current_planned_part_data(reader_, replacements_, entry_replacements_, workbook_part));

    const PartName calc_chain_part("/xl/calcChain.xml");
    PackageManifest updated_manifest = manifest_;
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

    edit_plan_.request_full_calculation(calc_chain_action);
    edit_plan_.set_part(workbook_part, PartWriteMode::LocalDomRewrite,
        "workbook calcPr fullCalcOnLoad updated by workbook metadata helper; definedNames preserved for policy review");
    edit_plan_.add_workbook_payload_dependency_audit(WorkbookPayloadDependencyAudit {
        workbook_part,
        WorkbookPayloadDependencyAuditKind::CalcMetadata,
        WorkbookPayloadDependencyAuditScope::WorkbookCalcMetadataRewrite,
        "calcPr",
        "workbook calc metadata helper rewrites direct workbook calcPr fullCalcOnLoad",
    });
    edit_plan_.add_workbook_payload_dependency_audit(WorkbookPayloadDependencyAudit {
        workbook_part,
        WorkbookPayloadDependencyAuditKind::DefinedNames,
        WorkbookPayloadDependencyAuditScope::WorkbookCalcMetadataRewrite,
        "definedNames",
        "workbook calc metadata helper preserves definedNames without semantic sync",
    });
    upsert_entry_replacement(entry_replacements_, workbook_part.zip_path(),
        std::move(updated_workbook_xml));
    remove_part_replacement(replacements_, workbook_part);
    updated_manifest.set_part_write_mode(workbook_part, PartWriteMode::LocalDomRewrite);

    if (calc_chain_action == CalcChainAction::Remove) {
        const std::string calc_chain_relationship_entry =
            relationship_entry_name_for_source_part(calc_chain_part);
        remove_part_replacement(replacements_, calc_chain_part);
        remove_entry_replacement(entry_replacements_, calc_chain_part.zip_path());
        remove_entry_replacement(entry_replacements_, calc_chain_relationship_entry);

        if (remove_calc_chain) {
            merge_removed_part_audit(edit_plan_, calc_chain_removal_plan, calc_chain_part);
            for (const std::string& note : calc_chain_removal_plan.notes()) {
                edit_plan_.add_note(note);
            }
        }

        if (omit_calc_chain) {
            add_omitted_part_entries(omitted_entries_, calc_chain_part);
            if (reader_.find_entry(calc_chain_relationship_entry) != nullptr) {
                edit_plan_.remove_package_entry(calc_chain_relationship_entry,
                    "calcChain-owned relationships omitted with removed calcChain part",
                    PackageEntryAuditKind::SourceRelationships, calc_chain_part.value());
            }
        }

        if (rewrite_content_types) {
            upsert_entry_replacement(entry_replacements_, "[Content_Types].xml",
                serialize_content_types(updated_manifest.content_types()));
            edit_plan_.set_package_entry("[Content_Types].xml",
                PartWriteMode::LocalDomRewrite,
                "content types updated for workbook calcChain removal",
                PackageEntryAuditKind::ContentTypes);
        }

        if (rewrite_workbook_relationships) {
            RelationshipSet* workbook_relationships =
                updated_manifest.relationships_for(workbook_part);
            const std::string workbook_relationship_entry =
                relationship_entry_name_for_source_part(workbook_part);
            upsert_entry_replacement(entry_replacements_, workbook_relationship_entry,
                serialize_relationships(*workbook_relationships));
            edit_plan_.set_package_entry(workbook_relationship_entry,
                PartWriteMode::LocalDomRewrite,
                "workbook relationships updated for workbook calcChain removal",
                PackageEntryAuditKind::SourceRelationships, workbook_part.value());
        } else {
            audit_preserved_relationship_entry(edit_plan_, reader_, workbook_part);
        }
    } else {
        audit_preserved_relationship_entry(edit_plan_, reader_, workbook_part);
        audit_preserved_relationship_entry(edit_plan_, reader_, calc_chain_part);
    }

    manifest_ = std::move(updated_manifest);
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

    edit_plan_.set_part(core_part, PartWriteMode::GenerateSmallXml,
        "core document properties generated small XML");
    edit_plan_.set_part(app_part, PartWriteMode::GenerateSmallXml,
        "extended document properties generated small XML");
    upsert_entry_replacement(
        entry_replacements_, core_part.zip_path(), build_core_properties(properties));
    upsert_entry_replacement(
        entry_replacements_, app_part.zip_path(), build_extended_properties(properties));
    remove_part_replacement(replacements_, core_part);
    remove_part_replacement(replacements_, app_part);
    remove_omitted_entry(omitted_entries_, core_part.zip_path());
    remove_omitted_entry(omitted_entries_, app_part.zip_path());

    if (content_types_changed) {
        upsert_entry_replacement(entry_replacements_, "[Content_Types].xml",
            serialize_content_types(updated_manifest.content_types()));
        edit_plan_.set_package_entry("[Content_Types].xml", PartWriteMode::LocalDomRewrite,
            "content types updated for document properties",
            PackageEntryAuditKind::ContentTypes);
    }
    if (package_relationships_changed) {
        upsert_entry_replacement(entry_replacements_, "_rels/.rels",
            serialize_relationships(updated_manifest.package_relationships()));
        edit_plan_.set_package_entry("_rels/.rels", PartWriteMode::LocalDomRewrite,
            "package relationships updated for document properties",
            PackageEntryAuditKind::PackageRelationships);
    }

    manifest_ = std::move(updated_manifest);
}

PackageEditorOutputPlan PackageEditor::planned_output() const
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

    const PackageEditorOutputPlan plan = planned_output();
    std::vector<PackageEntry> output_entries;
    output_entries.reserve(plan.entries.size());
    for (const PackageEditorOutputEntryPlan& entry_plan : plan.entries) {
        if (entry_plan.omitted) {
            continue;
        }
        output_entries.push_back(
            materialize_planned_output_entry(
                reader_, replacements_, entry_replacements_, entry_plan));
    }

    try {
        write_package(path, output_entries, options);
    } catch (const std::exception& error) {
        throw FastXlsxError("failed to write PackageEditor output package '"
            + path.generic_string() + "': " + error.what());
    }
}

} // namespace fastxlsx::detail
