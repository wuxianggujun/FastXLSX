#include <fastxlsx/detail/opc.hpp>

#include <fastxlsx/detail/xml.hpp>
#include <fastxlsx/workbook.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fastxlsx::detail {
namespace {

void append_optional_text_element(std::string& xml, std::string_view element_name, std::string_view value)
{
    if (value.empty()) {
        return;
    }

    xml += "<";
    xml += element_name;
    xml += ">";
    xml += escape_xml_text(value);
    xml += "</";
    xml += element_name;
    xml += ">";
}

char to_ascii_lower(char ch) noexcept
{
    if (ch >= 'A' && ch <= 'Z') {
        return static_cast<char>(ch - 'A' + 'a');
    }
    return ch;
}

std::string normalize_extension(std::string_view extension)
{
    while (!extension.empty() && extension.front() == '.') {
        extension.remove_prefix(1);
    }

    if (extension.empty()) {
        throw FastXlsxError("content type default extension cannot be empty");
    }

    std::string normalized;
    normalized.reserve(extension.size());
    for (const char ch : extension) {
        if (ch == '/' || ch == '\\') {
            throw FastXlsxError("content type default extension cannot contain a path separator");
        }
        normalized.push_back(to_ascii_lower(ch));
    }

    return normalized;
}

void validate_content_type(std::string_view content_type)
{
    if (content_type.empty()) {
        throw FastXlsxError("content type cannot be empty");
    }
}

void validate_relationship(const Relationship& relationship)
{
    if (relationship.id.empty()) {
        throw FastXlsxError("relationship id cannot be empty");
    }
    if (relationship.type.empty()) {
        throw FastXlsxError("relationship type cannot be empty");
    }
    if (relationship.target.empty()) {
        throw FastXlsxError("relationship target cannot be empty");
    }
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

std::string normalize_part_name(std::string_view value)
{
    if (value.empty()) {
        throw FastXlsxError("part name cannot be empty");
    }

    std::string path;
    path.reserve(value.size() + 1);
    for (const char ch : value) {
        if (ch == '\0') {
            throw FastXlsxError("part name cannot contain null bytes");
        }
        if (ch == '?' || ch == '#') {
            throw FastXlsxError("part name cannot contain query or fragment components");
        }
        path.push_back(ch == '\\' ? '/' : ch);
    }

    if (!path.empty() && path.back() == '/') {
        throw FastXlsxError("part name cannot end with a path separator");
    }

    if (path.front() != '/') {
        path.insert(path.begin(), '/');
    }

    std::vector<std::string> segments;
    std::size_t offset = 0;
    while (offset < path.size()) {
        while (offset < path.size() && path[offset] == '/') {
            ++offset;
        }

        const std::size_t begin = offset;
        while (offset < path.size() && path[offset] != '/') {
            ++offset;
        }

        if (begin == offset) {
            continue;
        }

        const std::string segment = path.substr(begin, offset - begin);
        if (segment == ".") {
            continue;
        }
        if (segment == "..") {
            if (segments.empty()) {
                throw FastXlsxError("part name cannot escape the package root");
            }
            segments.pop_back();
            continue;
        }

        segments.push_back(segment);
    }

    if (segments.empty()) {
        throw FastXlsxError("part name cannot be the package root");
    }

    std::string normalized;
    for (const auto& segment : segments) {
        normalized.push_back('/');
        normalized += segment;
    }
    return normalized;
}

std::string part_extension(const std::string& part_name)
{
    const std::size_t slash = part_name.find_last_of('/');
    const std::size_t dot = part_name.find_last_of('.');
    if (dot == std::string::npos || dot <= slash + 1 || dot + 1 >= part_name.size()) {
        return {};
    }

    std::string extension = part_name.substr(dot + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(), to_ascii_lower);
    return extension;
}

constexpr std::string_view content_type_relationships =
    "application/vnd.openxmlformats-package.relationships+xml";
constexpr std::string_view content_type_xml = "application/xml";
constexpr std::string_view content_type_workbook =
    "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml";
constexpr std::string_view content_type_worksheet =
    "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml";
constexpr std::string_view content_type_shared_strings =
    "application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml";
constexpr std::string_view content_type_core_properties =
    "application/vnd.openxmlformats-package.core-properties+xml";
constexpr std::string_view content_type_extended_properties =
    "application/vnd.openxmlformats-officedocument.extended-properties+xml";
constexpr std::string_view relationship_type_office_document =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument";
constexpr std::string_view relationship_type_core_properties =
    "http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties";
constexpr std::string_view relationship_type_extended_properties =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties";
constexpr std::string_view relationship_type_worksheet =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet";
constexpr std::string_view relationship_type_shared_strings =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings";

} // namespace

PartName::PartName(std::string_view value)
    : value_(normalize_part_name(value))
{
}

const std::string& PartName::value() const noexcept
{
    return value_;
}

std::string PartName::zip_path() const
{
    return value_.substr(1);
}

std::string PartName::extension() const
{
    return part_extension(value_);
}

bool operator==(const PartName& left, const PartName& right) noexcept
{
    return left.value() == right.value();
}

bool operator<(const PartName& left, const PartName& right) noexcept
{
    return left.value() < right.value();
}

Relationship& RelationshipSet::add(Relationship relationship)
{
    validate_relationship(relationship);
    if (find_by_id(relationship.id) != nullptr) {
        throw FastXlsxError("relationship id must be unique within a relationship set");
    }

    relationships_.push_back(std::move(relationship));
    return relationships_.back();
}

Relationship& RelationshipSet::add(std::string id, std::string type, std::string target,
    Relationship::TargetMode target_mode)
{
    return add(Relationship {std::move(id), std::move(type), std::move(target), target_mode});
}

Relationship* RelationshipSet::find_by_id(std::string_view id) noexcept
{
    const auto item = std::find_if(relationships_.begin(), relationships_.end(),
        [id](const Relationship& relationship) { return relationship.id == id; });
    return item == relationships_.end() ? nullptr : &*item;
}

const Relationship* RelationshipSet::find_by_id(std::string_view id) const noexcept
{
    const auto item = std::find_if(relationships_.begin(), relationships_.end(),
        [id](const Relationship& relationship) { return relationship.id == id; });
    return item == relationships_.end() ? nullptr : &*item;
}

const std::vector<Relationship>& RelationshipSet::relationships() const noexcept
{
    return relationships_;
}

bool RelationshipSet::empty() const noexcept
{
    return relationships_.empty();
}

std::size_t RelationshipSet::size() const noexcept
{
    return relationships_.size();
}

const ContentTypeDefault& ContentTypesManifest::add_default(
    std::string extension, std::string content_type)
{
    extension = normalize_extension(extension);
    validate_content_type(content_type);

    const auto existing = std::find_if(defaults_.begin(), defaults_.end(),
        [&extension](const ContentTypeDefault& value) { return value.extension == extension; });
    if (existing != defaults_.end()) {
        if (existing->content_type != content_type) {
            throw FastXlsxError("content type default conflicts with an existing extension");
        }
        return *existing;
    }

    defaults_.push_back({std::move(extension), std::move(content_type)});
    return defaults_.back();
}

const ContentTypeOverride& ContentTypesManifest::add_override(
    PartName part_name, std::string content_type)
{
    validate_content_type(content_type);

    const auto existing = std::find_if(overrides_.begin(), overrides_.end(),
        [&part_name](const ContentTypeOverride& value) { return value.part_name == part_name; });
    if (existing != overrides_.end()) {
        if (existing->content_type != content_type) {
            throw FastXlsxError("content type override conflicts with an existing part");
        }
        return *existing;
    }

    overrides_.push_back({std::move(part_name), std::move(content_type)});
    return overrides_.back();
}

const std::string* ContentTypesManifest::content_type_for(const PartName& part_name) const noexcept
{
    if (const auto* override = override_for(part_name)) {
        return &override->content_type;
    }

    if (const auto extension = part_name.extension(); !extension.empty()) {
        if (const auto* item = default_for(extension)) {
            return &item->content_type;
        }
    }

    return nullptr;
}

const ContentTypeDefault* ContentTypesManifest::default_for(std::string_view extension) const noexcept
{
    while (!extension.empty() && extension.front() == '.') {
        extension.remove_prefix(1);
    }

    const auto item = std::find_if(defaults_.begin(), defaults_.end(),
        [extension](const ContentTypeDefault& value) {
            if (value.extension.size() != extension.size()) {
                return false;
            }

            for (std::size_t index = 0; index < extension.size(); ++index) {
                if (value.extension[index] != to_ascii_lower(extension[index])) {
                    return false;
                }
            }
            return true;
        });
    return item == defaults_.end() ? nullptr : &*item;
}

const ContentTypeOverride* ContentTypesManifest::override_for(
    const PartName& part_name) const noexcept
{
    const auto item = std::find_if(overrides_.begin(), overrides_.end(),
        [&part_name](const ContentTypeOverride& value) { return value.part_name == part_name; });
    return item == overrides_.end() ? nullptr : &*item;
}

const std::vector<ContentTypeDefault>& ContentTypesManifest::defaults() const noexcept
{
    return defaults_;
}

const std::vector<ContentTypeOverride>& ContentTypesManifest::overrides() const noexcept
{
    return overrides_;
}

const ContentTypeDefault& ContentTypeRegistry::add_default(
    std::string extension, std::string content_type)
{
    return manifest_.add_default(std::move(extension), std::move(content_type));
}

const ContentTypeOverride& ContentTypeRegistry::add_override(
    PartName part_name, std::string content_type)
{
    return manifest_.add_override(std::move(part_name), std::move(content_type));
}

const std::string* ContentTypeRegistry::content_type_for(
    const PartName& part_name) const noexcept
{
    return manifest_.content_type_for(part_name);
}

const ContentTypeDefault* ContentTypeRegistry::default_for(
    std::string_view extension) const noexcept
{
    return manifest_.default_for(extension);
}

const ContentTypeOverride* ContentTypeRegistry::override_for(
    const PartName& part_name) const noexcept
{
    return manifest_.override_for(part_name);
}

ContentTypesManifest& ContentTypeRegistry::manifest() noexcept
{
    return manifest_;
}

const ContentTypesManifest& ContentTypeRegistry::manifest() const noexcept
{
    return manifest_;
}

void PackagePart::set_write_mode(PartWriteMode mode) noexcept
{
    write_mode = mode;
    switch (mode) {
    case PartWriteMode::CopyOriginal:
        preserve_original = true;
        dirty = false;
        generated = false;
        break;
    case PartWriteMode::GenerateSmallXml:
        preserve_original = false;
        dirty = true;
        generated = true;
        break;
    case PartWriteMode::StreamRewrite:
    case PartWriteMode::LocalDomRewrite:
        preserve_original = false;
        dirty = true;
        generated = false;
        break;
    }
}

void PackagePart::mark_dirty() noexcept
{
    if (write_mode == PartWriteMode::CopyOriginal) {
        write_mode = PartWriteMode::LocalDomRewrite;
    }
    preserve_original = false;
    dirty = true;
}

void PackagePart::mark_generated() noexcept
{
    set_write_mode(PartWriteMode::GenerateSmallXml);
}

PackagePart& PartIndex::add_part(PartName part_name, std::string content_type)
{
    return ensure_part(std::move(part_name), std::move(content_type));
}

PackagePart& PartIndex::ensure_part(PartName part_name, std::string content_type)
{
    if (auto* existing = find_part(part_name)) {
        if (!content_type.empty()) {
            if (!existing->content_type.empty() && existing->content_type != content_type) {
                throw FastXlsxError("part index content type conflicts with an existing part");
            }
            content_types_.add_override(existing->name, content_type);
            existing->content_type = std::move(content_type);
        }
        return *existing;
    }

    PackagePart part {std::move(part_name), std::move(content_type), {}};
    if (!part.content_type.empty()) {
        content_types_.add_override(part.name, part.content_type);
    }

    parts_.push_back(std::move(part));
    return parts_.back();
}

PackagePart* PartIndex::find_part(const PartName& part_name) noexcept
{
    const auto item = std::find_if(parts_.begin(), parts_.end(),
        [&part_name](const PackagePart& part) { return part.name == part_name; });
    return item == parts_.end() ? nullptr : &*item;
}

const PackagePart* PartIndex::find_part(const PartName& part_name) const noexcept
{
    const auto item = std::find_if(parts_.begin(), parts_.end(),
        [&part_name](const PackagePart& part) { return part.name == part_name; });
    return item == parts_.end() ? nullptr : &*item;
}

ContentTypeRegistry& PartIndex::content_types() noexcept
{
    return content_types_;
}

const ContentTypeRegistry& PartIndex::content_types() const noexcept
{
    return content_types_;
}

const std::vector<PackagePart>& PartIndex::parts() const noexcept
{
    return parts_;
}

bool PartIndex::empty() const noexcept
{
    return parts_.empty();
}

std::size_t PartIndex::size() const noexcept
{
    return parts_.size();
}

RelationshipGraph::RelationshipGraph(const PartIndex& part_index)
    : part_index_(&part_index)
{
}

Relationship& RelationshipGraph::add_package_relationship(Relationship relationship)
{
    return package_relationships_.add(std::move(relationship));
}

Relationship& RelationshipGraph::add_package_relationship(std::string type, std::string target,
    Relationship::TargetMode target_mode)
{
    return add_package_relationship(Relationship {
        next_relationship_id(package_relationships_),
        std::move(type),
        std::move(target),
        target_mode,
    });
}

Relationship& RelationshipGraph::add_relationship(
    const PartName& source_part, Relationship relationship)
{
    return ensure_relationships_for(source_part).add(std::move(relationship));
}

Relationship& RelationshipGraph::add_relationship(const PartName& source_part, std::string type,
    std::string target, Relationship::TargetMode target_mode)
{
    RelationshipSet& relationships = ensure_relationships_for(source_part);
    return relationships.add(Relationship {
        next_relationship_id(relationships),
        std::move(type),
        std::move(target),
        target_mode,
    });
}

RelationshipSet& RelationshipGraph::package_relationships() noexcept
{
    return package_relationships_;
}

const RelationshipSet& RelationshipGraph::package_relationships() const noexcept
{
    return package_relationships_;
}

RelationshipSet* RelationshipGraph::relationships_for(const PartName& source_part) noexcept
{
    const auto item = std::find_if(part_relationships_.begin(), part_relationships_.end(),
        [&source_part](const PartRelationshipSet& value) { return value.owner == source_part; });
    return item == part_relationships_.end() ? nullptr : &item->relationships;
}

const RelationshipSet* RelationshipGraph::relationships_for(const PartName& source_part) const noexcept
{
    const auto item = std::find_if(part_relationships_.begin(), part_relationships_.end(),
        [&source_part](const PartRelationshipSet& value) { return value.owner == source_part; });
    return item == part_relationships_.end() ? nullptr : &item->relationships;
}

RelationshipSet& RelationshipGraph::ensure_relationships_for(const PartName& source_part)
{
    if (part_index_->find_part(source_part) == nullptr) {
        throw FastXlsxError("relationship source part is not registered in part index");
    }

    if (auto* relationships = relationships_for(source_part)) {
        return *relationships;
    }

    part_relationships_.push_back(PartRelationshipSet {source_part, {}});
    return part_relationships_.back().relationships;
}

PackagePart& PackageManifest::add_part(PartName part_name, std::string content_type)
{
    return ensure_part(std::move(part_name), std::move(content_type));
}

PackagePart& PackageManifest::ensure_part(PartName part_name, std::string content_type)
{
    if (auto* existing = find_part(part_name)) {
        if (!content_type.empty()) {
            if (!existing->content_type.empty() && existing->content_type != content_type) {
                throw FastXlsxError("package part content type conflicts with an existing part");
            }
            content_types_.add_override(existing->name, content_type);
            existing->content_type = std::move(content_type);
        }
        return *existing;
    }

    PackagePart part {std::move(part_name), std::move(content_type), {}};
    if (!part.content_type.empty()) {
        content_types_.add_override(part.name, part.content_type);
    }

    parts_.push_back(std::move(part));
    return parts_.back();
}

PackagePart& PackageManifest::set_part_write_mode(const PartName& part_name, PartWriteMode mode)
{
    auto* part = find_part(part_name);
    if (part == nullptr) {
        throw FastXlsxError("package edit part is not registered in package manifest");
    }

    part->set_write_mode(mode);
    return *part;
}

PackagePart& PackageManifest::mark_part_dirty(const PartName& part_name)
{
    auto* part = find_part(part_name);
    if (part == nullptr) {
        throw FastXlsxError("package edit part is not registered in package manifest");
    }

    part->mark_dirty();
    return *part;
}

PackagePart& PackageManifest::mark_part_generated(const PartName& part_name)
{
    auto* part = find_part(part_name);
    if (part == nullptr) {
        throw FastXlsxError("package edit part is not registered in package manifest");
    }

    part->mark_generated();
    return *part;
}

PackagePart* PackageManifest::find_part(const PartName& part_name) noexcept
{
    const auto item = std::find_if(parts_.begin(), parts_.end(),
        [&part_name](const PackagePart& part) { return part.name == part_name; });
    return item == parts_.end() ? nullptr : &*item;
}

const PackagePart* PackageManifest::find_part(const PartName& part_name) const noexcept
{
    const auto item = std::find_if(parts_.begin(), parts_.end(),
        [&part_name](const PackagePart& part) { return part.name == part_name; });
    return item == parts_.end() ? nullptr : &*item;
}

RelationshipSet* PackageManifest::relationships_for(const PartName& part_name) noexcept
{
    if (auto* part = find_part(part_name)) {
        return &part->relationships;
    }
    return nullptr;
}

const RelationshipSet* PackageManifest::relationships_for(const PartName& part_name) const noexcept
{
    if (const auto* part = find_part(part_name)) {
        return &part->relationships;
    }
    return nullptr;
}

Relationship& PackageManifest::add_relationship(
    const PartName& source_part, Relationship relationship)
{
    auto* part = find_part(source_part);
    if (part == nullptr) {
        throw FastXlsxError("relationship source part is not registered in package manifest");
    }
    return part->relationships.add(std::move(relationship));
}

Relationship& PackageManifest::add_package_relationship(Relationship relationship)
{
    return package_relationships_.add(std::move(relationship));
}

RelationshipSet& PackageManifest::package_relationships() noexcept
{
    return package_relationships_;
}

const RelationshipSet& PackageManifest::package_relationships() const noexcept
{
    return package_relationships_;
}

ContentTypesManifest& PackageManifest::content_types() noexcept
{
    return content_types_;
}

const ContentTypesManifest& PackageManifest::content_types() const noexcept
{
    return content_types_;
}

const std::vector<PackagePart>& PackageManifest::parts() const noexcept
{
    return parts_;
}

bool PackageManifest::empty() const noexcept
{
    return parts_.empty();
}

std::size_t PackageManifest::size() const noexcept
{
    return parts_.size();
}

PackageManifest make_minimal_workbook_manifest(
    std::size_t worksheet_count, bool include_shared_strings, bool include_document_properties)
{
    PackageManifest manifest;
    manifest.content_types().add_default("rels", std::string(content_type_relationships));
    manifest.content_types().add_default("xml", std::string(content_type_xml));

    const PartName workbook_part("/xl/workbook.xml");
    manifest.add_part(workbook_part, std::string(content_type_workbook)).mark_generated();

    manifest.add_package_relationship(Relationship {
        "rId1",
        std::string(relationship_type_office_document),
        "xl/workbook.xml",
    });

    if (include_document_properties) {
        manifest.add_part(PartName("/docProps/core.xml"), std::string(content_type_core_properties))
            .mark_generated();
        manifest.add_part(PartName("/docProps/app.xml"), std::string(content_type_extended_properties))
            .mark_generated();
        manifest.add_package_relationship(Relationship {
            "rId2",
            std::string(relationship_type_core_properties),
            "docProps/core.xml",
        });
        manifest.add_package_relationship(Relationship {
            "rId3",
            std::string(relationship_type_extended_properties),
            "docProps/app.xml",
        });
    }

    for (std::size_t index = 1; index <= worksheet_count; ++index) {
        const std::string sheet_name = "sheet" + std::to_string(index) + ".xml";
        manifest.add_part(PartName("/xl/worksheets/" + sheet_name), std::string(content_type_worksheet))
            .set_write_mode(PartWriteMode::StreamRewrite);
        manifest.add_relationship(workbook_part,
            Relationship {
                "rId" + std::to_string(index),
                std::string(relationship_type_worksheet),
                "worksheets/" + sheet_name,
            });
    }

    if (include_shared_strings) {
        manifest.add_part(PartName("/xl/sharedStrings.xml"), std::string(content_type_shared_strings))
            .mark_generated();
        manifest.add_relationship(workbook_part,
            Relationship {
                "rId" + std::to_string(worksheet_count + 1),
                std::string(relationship_type_shared_strings),
                "sharedStrings.xml",
            });
    }

    return manifest;
}

std::string build_core_properties(const DocumentProperties& properties)
{
    std::string xml;
    xml += R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)";
    xml += R"(<cp:coreProperties )";
    xml += R"(xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties" )";
    xml += R"(xmlns:dc="http://purl.org/dc/elements/1.1/" )";
    xml += R"(xmlns:dcterms="http://purl.org/dc/terms/" )";
    xml += R"(xmlns:dcmitype="http://purl.org/dc/dcmitype/" )";
    xml += R"(xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">)";
    append_optional_text_element(xml, "dc:creator", properties.creator);
    append_optional_text_element(xml, "cp:lastModifiedBy", properties.last_modified_by);
    append_optional_text_element(xml, "dc:title", properties.title);
    append_optional_text_element(xml, "dc:subject", properties.subject);
    append_optional_text_element(xml, "dc:description", properties.description);
    append_optional_text_element(xml, "cp:keywords", properties.keywords);
    append_optional_text_element(xml, "cp:category", properties.category);
    xml += "</cp:coreProperties>";
    return xml;
}

std::string build_extended_properties(const DocumentProperties& properties)
{
    std::string xml;
    xml += R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)";
    xml += R"(<Properties )";
    xml += R"(xmlns="http://schemas.openxmlformats.org/officeDocument/2006/extended-properties" )";
    xml += R"(xmlns:vt="http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes">)";
    append_optional_text_element(xml, "Application", properties.application);
    xml += "<DocSecurity>0</DocSecurity>";
    xml += "<ScaleCrop>false</ScaleCrop>";
    xml += "<LinksUpToDate>false</LinksUpToDate>";
    xml += "<SharedDoc>false</SharedDoc>";
    xml += "<HyperlinksChanged>false</HyperlinksChanged>";
    append_optional_text_element(xml, "AppVersion", properties.app_version);
    xml += "</Properties>";
    return xml;
}

std::string serialize_content_types(const ContentTypesManifest& content_types)
{
    std::string xml;
    xml += R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)";
    xml += R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)";

    for (const ContentTypeDefault& item : content_types.defaults()) {
        xml += R"(<Default Extension=")";
        xml += escape_xml_attribute(item.extension);
        xml += R"(" ContentType=")";
        xml += escape_xml_attribute(item.content_type);
        xml += R"("/>)";
    }

    for (const ContentTypeOverride& item : content_types.overrides()) {
        xml += R"(<Override PartName=")";
        xml += escape_xml_attribute(item.part_name.value());
        xml += R"(" ContentType=")";
        xml += escape_xml_attribute(item.content_type);
        xml += R"("/>)";
    }

    xml += "</Types>";
    return xml;
}

std::string serialize_relationships(const RelationshipSet& relationships)
{
    std::string xml;
    xml += R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)";
    xml += R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)";

    for (const Relationship& relationship : relationships.relationships()) {
        xml += R"(<Relationship Id=")";
        xml += escape_xml_attribute(relationship.id);
        xml += R"(" Type=")";
        xml += escape_xml_attribute(relationship.type);
        xml += R"(" Target=")";
        xml += escape_xml_attribute(relationship.target);
        xml += R"(")";
        if (relationship.target_mode == Relationship::TargetMode::External) {
            xml += R"( TargetMode="External")";
        }
        xml += R"(/>)";
    }

    xml += "</Relationships>";
    return xml;
}

} // namespace fastxlsx::detail
