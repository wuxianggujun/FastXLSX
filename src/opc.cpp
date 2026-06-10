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
    append_escaped_xml_text(xml, value);
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
constexpr std::string_view content_type_styles =
    "application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml";
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
constexpr std::string_view relationship_type_styles =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles";

void add_dependency_if_exists(DependencyAnalysis& analysis, const PackageManifest& manifest,
    std::string_view part_name, std::string reason)
{
    PartName normalized(part_name);
    if (manifest.find_part(normalized) == nullptr) {
        return;
    }

    const auto existing = std::find_if(analysis.parts.begin(), analysis.parts.end(),
        [&normalized](const PartDependency& dependency) {
            return dependency.part_name == normalized;
        });
    if (existing == analysis.parts.end()) {
        analysis.parts.push_back(PartDependency {std::move(normalized), std::move(reason)});
    }
}

bool add_dependency(DependencyAnalysis& analysis, PartName part_name, std::string reason)
{
    const auto existing = std::find_if(analysis.parts.begin(), analysis.parts.end(),
        [&part_name](const PartDependency& dependency) {
            return dependency.part_name == part_name;
        });
    if (existing != analysis.parts.end()) {
        return false;
    }

    analysis.parts.push_back(PartDependency {std::move(part_name), std::move(reason)});
    return true;
}

std::string resolve_internal_relationship_target_path(
    const PartName& source_part, const Relationship& relationship)
{
    if (relationship.target_mode == Relationship::TargetMode::External) {
        return {};
    }
    if (!relationship.target.empty() && relationship.target.front() == '/') {
        return relationship.target;
    }

    const std::string& source = source_part.value();
    const std::size_t slash = source.find_last_of('/');
    if (slash == std::string::npos || slash == 0) {
        return "/" + relationship.target;
    }
    return source.substr(0, slash) + "/" + relationship.target;
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

std::string relationship_dependency_reason(
    const PartName& source_part, const Relationship& relationship, const PartName& target_part)
{
    if (source_part.value().find("/xl/worksheets/") == 0) {
        return "worksheet relationship " + relationship.id
            + " type " + relationship.type + " targets known internal part "
            + target_part.value();
    }

    return "relationship from " + source_part.value() + " " + relationship.id
        + " type " + relationship.type + " targets known internal part "
        + target_part.value();
}

bool add_relationship_dependency(DependencyAnalysis& analysis, const PartName& source_part,
    const Relationship& relationship, PartName target_part)
{
    const auto existing = std::find_if(analysis.parts.begin(), analysis.parts.end(),
        [&target_part](const PartDependency& dependency) {
            return dependency.part_name == target_part;
        });
    if (existing != analysis.parts.end()) {
        return false;
    }

    analysis.parts.push_back(PartDependency {
        target_part,
        relationship_dependency_reason(source_part, relationship, target_part),
        source_part.value(),
        relationship.id,
        relationship.type,
        relationship.target,
    });
    return true;
}

std::string relationship_target_note_prefix(
    std::string_view prefix, const PartName& source_part, const Relationship& relationship)
{
    return std::string(prefix) + " owner " + source_part.value() + " relationship "
        + relationship.id + " type " + relationship.type + " target "
        + relationship.target;
}

void record_relationship_target_audit(DependencyAnalysis& analysis,
    std::string_view prefix, const PartName& source_part, const Relationship& relationship,
    std::string note_suffix = {}, std::string normalized_target = {})
{
    std::string note = relationship_target_note_prefix(prefix, source_part, relationship);
    if (!note_suffix.empty()) {
        note += " " + note_suffix;
    }
    analysis.relationship_target_audits.push_back(RelationshipTargetAudit {
        source_part,
        relationship.id,
        relationship.type,
        relationship.target,
        normalized_target,
        note,
    });
    analysis.relationship_target_notes.push_back(std::move(note));
}

bool is_same_relationship_target_audit(
    const RelationshipTargetAudit& left, const RelationshipTargetAudit& right) noexcept
{
    return left.owner_part == right.owner_part
        && left.relationship_id == right.relationship_id
        && left.relationship_type == right.relationship_type
        && left.target == right.target
        && left.normalized_target == right.normalized_target;
}

bool is_same_worksheet_relationship_reference_audit(
    const WorksheetRelationshipReferenceAudit& left,
    const WorksheetRelationshipReferenceAudit& right) noexcept
{
    return left.worksheet_part == right.worksheet_part
        && left.kind == right.kind
        && left.element == right.element
        && left.relationship_id == right.relationship_id
        && left.expected_relationship_type == right.expected_relationship_type
        && left.actual_relationship_type == right.actual_relationship_type;
}

bool is_same_worksheet_payload_dependency_audit(
    const WorksheetPayloadDependencyAudit& left,
    const WorksheetPayloadDependencyAudit& right) noexcept
{
    return left.worksheet_part == right.worksheet_part
        && left.kind == right.kind
        && left.scope == right.scope
        && left.element == right.element;
}

bool is_same_workbook_payload_dependency_audit(
    const WorkbookPayloadDependencyAudit& left,
    const WorkbookPayloadDependencyAudit& right) noexcept
{
    return left.workbook_part == right.workbook_part
        && left.kind == right.kind
        && left.scope == right.scope
        && left.element == right.element;
}

std::string normalized_package_relationship_target(const Relationship& relationship)
{
    if (relationship.target_mode == Relationship::TargetMode::External) {
        return {};
    }

    std::string target = relationship.target;
    const std::size_t uri_qualifier = target.find_first_of("?#");
    if (uri_qualifier != std::string::npos) {
        target.erase(uri_qualifier);
    }
    target = decode_percent_encoded_relationship_target(target);
    if (!target.empty() && target.front() == '/') {
        return target;
    }
    return "/" + target;
}

std::string normalized_source_relationship_target(
    const PartName& source_part, const Relationship& relationship)
{
    if (relationship.target_mode == Relationship::TargetMode::External) {
        return {};
    }

    Relationship target_relationship = relationship;
    const std::size_t uri_qualifier =
        target_relationship.target.find_first_of("?#");
    if (uri_qualifier != std::string::npos) {
        target_relationship.target.erase(uri_qualifier);
    }
    target_relationship.target =
        decode_percent_encoded_relationship_target(target_relationship.target);
    return resolve_internal_relationship_target_path(source_part, target_relationship);
}

bool relationship_targets_removed_part(std::string target_path, const PartName& removed_part)
{
    if (target_path.empty()) {
        return false;
    }

    try {
        return PartName(target_path) == removed_part;
    } catch (const FastXlsxError&) {
        return false;
    }
}

std::string relationship_entry_name_for_source_part(const PartName& source_part)
{
    const std::string source_path = source_part.zip_path();
    const std::size_t slash = source_path.find_last_of('/');
    if (slash == std::string::npos) {
        return "_rels/" + source_path + ".rels";
    }
    return source_path.substr(0, slash) + "/_rels/" + source_path.substr(slash + 1)
        + ".rels";
}

struct RemovedPartAuditPlan {
    std::string reason;
    std::vector<RemovedPartInboundRelationshipAudit> inbound_relationships;
    std::vector<std::string> notes;
};

void append_inbound_relationship_audit(RemovedPartAuditPlan& plan,
    std::string_view owner_reason, std::string owner_part, std::string owner_entry,
    const Relationship& relationship, const PartName& removed_part)
{
    plan.reason += "; inbound relationship preserved after explicit part removal: owner ";
    plan.reason += owner_reason;
    plan.reason += " relationship ";
    plan.reason += relationship.id;
    plan.reason += " type ";
    plan.reason += relationship.type;
    plan.reason += " target ";
    plan.reason += relationship.target;
    plan.reason += " still resolves to removed part ";
    plan.reason += removed_part.value();

    plan.inbound_relationships.push_back(RemovedPartInboundRelationshipAudit {
        std::move(owner_part),
        std::move(owner_entry),
        relationship.id,
        relationship.type,
        relationship.target,
        removed_part,
    });
}

void append_inbound_relationship_scan_note(RemovedPartAuditPlan& plan,
    std::string_view owner_reason, const Relationship& relationship,
    std::string_view error_message)
{
    std::string note =
        "invalid relationship target skipped during removed-part inbound audit: owner ";
    note += owner_reason;
    note += " relationship ";
    note += relationship.id;
    note += " type ";
    note += relationship.type;
    note += " target ";
    note += relationship.target;
    note += " error ";
    note += error_message;
    plan.notes.push_back(std::move(note));
}

RemovedPartAuditPlan append_inbound_relationship_removal_audit(
    const PackageManifest& manifest, const PartName& removed_part, std::string reason)
{
    RemovedPartAuditPlan plan {std::move(reason), {}, {}};

    for (const Relationship& relationship : manifest.package_relationships().relationships()) {
        std::string normalized_target;
        try {
            normalized_target = normalized_package_relationship_target(relationship);
        } catch (const FastXlsxError& error) {
            append_inbound_relationship_scan_note(
                plan, "package _rels/.rels", relationship, error.what());
            continue;
        }
        if (relationship_targets_removed_part(std::move(normalized_target), removed_part)) {
            append_inbound_relationship_audit(plan, "package _rels/.rels", {}, "_rels/.rels",
                relationship, removed_part);
        }
    }

    for (const PackagePart& part : manifest.parts()) {
        for (const Relationship& relationship : part.relationships.relationships()) {
            std::string normalized_target;
            try {
                normalized_target = normalized_source_relationship_target(part.name, relationship);
            } catch (const FastXlsxError& error) {
                append_inbound_relationship_scan_note(
                    plan, part.name.value(), relationship, error.what());
                continue;
            }
            if (relationship_targets_removed_part(std::move(normalized_target), removed_part)) {
                append_inbound_relationship_audit(plan, part.name.value(), part.name.value(),
                    relationship_entry_name_for_source_part(part.name), relationship,
                    removed_part);
            }
        }
    }

    return plan;
}

void add_known_relationship_target_dependencies(DependencyAnalysis& analysis,
    const PackageManifest& manifest, const PartName& source_part,
    const RelationshipSet& relationships)
{
    for (const Relationship& relationship : relationships.relationships()) {
        if (relationship.target_mode == Relationship::TargetMode::External) {
            analysis.has_external_relationship_targets = true;
            record_relationship_target_audit(analysis,
                "external relationship targets are preserved in owner .rels and are not package parts:",
                source_part, relationship);
            continue;
        }

        Relationship target_relationship = relationship;
        const std::size_t uri_qualifier =
            target_relationship.target.find_first_of("?#");
        const bool uri_qualified = uri_qualifier != std::string::npos;
        if (uri_qualified) {
            analysis.has_uri_qualified_internal_relationship_targets = true;
            target_relationship.target.erase(uri_qualifier);
        }

        try {
            target_relationship.target =
                decode_percent_encoded_relationship_target(target_relationship.target);
            const std::string target_path =
                resolve_internal_relationship_target_path(source_part, target_relationship);
            if (target_path.empty()) {
                continue;
            }

            PartName target_part(target_path);
            if (uri_qualified) {
                record_relationship_target_audit(analysis,
                    "URI-qualified internal relationship targets require package structure review:",
                    source_part, relationship, "has base part " + target_part.value(),
                    target_part.value());
            }
            if (manifest.find_part(target_part) == nullptr) {
                analysis.has_unresolved_internal_relationship_targets = true;
                record_relationship_target_audit(analysis,
                    "unresolved internal relationship targets require package structure review:",
                    source_part, relationship,
                    "resolves to unregistered part " + target_part.value(), target_part.value());
                continue;
            }

            const bool inserted =
                add_relationship_dependency(analysis, source_part, relationship, target_part);
            if (!inserted) {
                continue;
            }

            if (const RelationshipSet* target_relationships =
                    manifest.relationships_for(target_part)) {
                add_known_relationship_target_dependencies(
                    analysis, manifest, target_part, *target_relationships);
            }
        } catch (const FastXlsxError&) {
            analysis.has_invalid_internal_relationship_targets = true;
            record_relationship_target_audit(analysis,
                "invalid internal relationship targets require package structure review:",
                source_part, relationship, "cannot be normalized as a package part");
        }
    }
}

void annotate_copy_original_dependencies(EditPlan& plan, const DependencyAnalysis& analysis,
    const PartName& target_part)
{
    for (const PartDependency& dependency : analysis.parts) {
        if (dependency.part_name == target_part) {
            continue;
        }
        if (auto* entry = plan.find_part(dependency.part_name);
            entry != nullptr && entry->write_mode == PartWriteMode::CopyOriginal) {
            entry->reason = dependency.reason;
            entry->relationship_owner_part = dependency.relationship_owner_part;
            entry->relationship_id = dependency.relationship_id;
            entry->relationship_type = dependency.relationship_type;
            entry->relationship_target = dependency.relationship_target;
        }
    }
}

void validate_package_entry_audit_context(
    std::string_view entry_name, PackageEntryAuditKind audit_kind, std::string_view owner_part)
{
    switch (audit_kind) {
    case PackageEntryAuditKind::Generic:
        if (!owner_part.empty()) {
            throw FastXlsxError("generic package-entry audit cannot carry an owner part");
        }
        return;
    case PackageEntryAuditKind::ContentTypes:
        if (entry_name != "[Content_Types].xml") {
            throw FastXlsxError("content-types package-entry audit must target [Content_Types].xml");
        }
        if (!owner_part.empty()) {
            throw FastXlsxError("content-types package-entry audit cannot carry an owner part");
        }
        return;
    case PackageEntryAuditKind::PackageRelationships:
        if (entry_name != "_rels/.rels") {
            throw FastXlsxError("package-relationships package-entry audit must target _rels/.rels");
        }
        if (!owner_part.empty()) {
            throw FastXlsxError("package-relationships package-entry audit cannot carry an owner part");
        }
        return;
    case PackageEntryAuditKind::SourceRelationships:
        break;
    }

    if (owner_part.empty()) {
        throw FastXlsxError("source relationship package-entry audit requires an owner part");
    }

    const PartName owner(owner_part);
    const std::string source_path = owner.zip_path();
    const std::size_t slash = source_path.find_last_of('/');
    std::string expected_entry;
    if (slash == std::string::npos) {
        expected_entry = "_rels/" + source_path + ".rels";
    } else {
        expected_entry = source_path.substr(0, slash) + "/_rels/"
            + source_path.substr(slash + 1) + ".rels";
    }
    if (entry_name != expected_entry) {
        throw FastXlsxError("source relationship package-entry audit target does not match owner part");
    }
}

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

std::size_t RelationshipSet::remove_by_type(std::string_view type) noexcept
{
    const std::size_t old_size = relationships_.size();
    relationships_.erase(std::remove_if(relationships_.begin(), relationships_.end(),
                             [type](const Relationship& relationship) {
                                 return relationship.type == type;
                             }),
        relationships_.end());
    return old_size - relationships_.size();
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

bool ContentTypesManifest::remove_override(const PartName& part_name) noexcept
{
    const auto old_size = overrides_.size();
    overrides_.erase(std::remove_if(overrides_.begin(), overrides_.end(),
                         [&part_name](const ContentTypeOverride& value) {
                             return value.part_name == part_name;
                         }),
        overrides_.end());
    return old_size != overrides_.size();
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

EditPlanEntry& EditPlan::set_part(
    PartName part_name, PartWriteMode write_mode, std::string reason)
{
    removed_parts_.erase(std::remove_if(removed_parts_.begin(), removed_parts_.end(),
                             [&part_name](const EditPlanRemovedPart& entry) {
                                 return entry.part_name == part_name;
                             }),
        removed_parts_.end());

    if (auto* existing = find_part(part_name)) {
        existing->write_mode = write_mode;
        existing->reason = std::move(reason);
        existing->relationship_owner_part.clear();
        existing->relationship_id.clear();
        existing->relationship_type.clear();
        existing->relationship_target.clear();
        return *existing;
    }

    entries_.push_back(EditPlanEntry {std::move(part_name), write_mode, std::move(reason)});
    return entries_.back();
}

EditPlanEntry* EditPlan::find_part(const PartName& part_name) noexcept
{
    const auto item = std::find_if(entries_.begin(), entries_.end(),
        [&part_name](const EditPlanEntry& entry) { return entry.part_name == part_name; });
    return item == entries_.end() ? nullptr : &*item;
}

const EditPlanEntry* EditPlan::find_part(const PartName& part_name) const noexcept
{
    const auto item = std::find_if(entries_.begin(), entries_.end(),
        [&part_name](const EditPlanEntry& entry) { return entry.part_name == part_name; });
    return item == entries_.end() ? nullptr : &*item;
}

EditPlanRemovedPart& EditPlan::remove_part(PartName part_name, std::string reason)
{
    return remove_part(std::move(part_name), std::move(reason), {});
}

EditPlanRemovedPart& EditPlan::remove_part(PartName part_name, std::string reason,
    std::vector<RemovedPartInboundRelationshipAudit> inbound_relationships)
{
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                       [&part_name](const EditPlanEntry& entry) {
                           return entry.part_name == part_name;
                       }),
        entries_.end());

    const auto item = std::find_if(removed_parts_.begin(), removed_parts_.end(),
        [&part_name](const EditPlanRemovedPart& entry) { return entry.part_name == part_name; });
    if (item != removed_parts_.end()) {
        item->reason = std::move(reason);
        item->inbound_relationships = std::move(inbound_relationships);
        return *item;
    }

    removed_parts_.push_back(EditPlanRemovedPart {
        std::move(part_name),
        std::move(reason),
        std::move(inbound_relationships),
    });
    return removed_parts_.back();
}

const EditPlanRemovedPart* EditPlan::find_removed_part(
    const PartName& part_name) const noexcept
{
    const auto item = std::find_if(removed_parts_.begin(), removed_parts_.end(),
        [&part_name](const EditPlanRemovedPart& entry) { return entry.part_name == part_name; });
    return item == removed_parts_.end() ? nullptr : &*item;
}

EditPlanPackageEntry& EditPlan::set_package_entry(
    std::string entry_name, PartWriteMode write_mode, std::string reason,
    PackageEntryAuditKind audit_kind, std::string owner_part)
{
    if (entry_name.empty()) {
        throw FastXlsxError("edit plan package entry name cannot be empty");
    }
    validate_package_entry_audit_context(entry_name, audit_kind, owner_part);

    removed_package_entries_.erase(
        std::remove_if(removed_package_entries_.begin(), removed_package_entries_.end(),
            [&entry_name](const EditPlanRemovedPackageEntry& entry) {
                return entry.entry_name == entry_name;
            }),
        removed_package_entries_.end());

    if (auto* existing = find_package_entry(entry_name)) {
        existing->write_mode = write_mode;
        existing->reason = std::move(reason);
        existing->audit_kind = audit_kind;
        existing->owner_part = std::move(owner_part);
        return *existing;
    }

    package_entries_.push_back(EditPlanPackageEntry {
        std::move(entry_name),
        write_mode,
        std::move(reason),
        audit_kind,
        std::move(owner_part),
    });
    return package_entries_.back();
}

EditPlanPackageEntry* EditPlan::find_package_entry(std::string_view entry_name) noexcept
{
    const auto item = std::find_if(package_entries_.begin(), package_entries_.end(),
        [entry_name](const EditPlanPackageEntry& entry) {
            return entry.entry_name == entry_name;
        });
    return item == package_entries_.end() ? nullptr : &*item;
}

const EditPlanPackageEntry* EditPlan::find_package_entry(
    std::string_view entry_name) const noexcept
{
    const auto item = std::find_if(package_entries_.begin(), package_entries_.end(),
        [entry_name](const EditPlanPackageEntry& entry) {
            return entry.entry_name == entry_name;
        });
    return item == package_entries_.end() ? nullptr : &*item;
}

EditPlanRemovedPackageEntry& EditPlan::remove_package_entry(
    std::string entry_name, std::string reason, PackageEntryAuditKind audit_kind,
    std::string owner_part)
{
    if (entry_name.empty()) {
        throw FastXlsxError("removed edit plan package entry name cannot be empty");
    }
    validate_package_entry_audit_context(entry_name, audit_kind, owner_part);

    package_entries_.erase(std::remove_if(package_entries_.begin(), package_entries_.end(),
                               [&entry_name](const EditPlanPackageEntry& entry) {
                                   return entry.entry_name == entry_name;
                               }),
        package_entries_.end());

    const auto item = std::find_if(removed_package_entries_.begin(),
        removed_package_entries_.end(),
        [&entry_name](const EditPlanRemovedPackageEntry& entry) {
            return entry.entry_name == entry_name;
        });
    if (item != removed_package_entries_.end()) {
        item->reason = std::move(reason);
        item->audit_kind = audit_kind;
        item->owner_part = std::move(owner_part);
        return *item;
    }

    removed_package_entries_.push_back(EditPlanRemovedPackageEntry {
        std::move(entry_name),
        std::move(reason),
        audit_kind,
        std::move(owner_part),
    });
    return removed_package_entries_.back();
}

const EditPlanRemovedPackageEntry* EditPlan::find_removed_package_entry(
    std::string_view entry_name) const noexcept
{
    const auto item = std::find_if(removed_package_entries_.begin(),
        removed_package_entries_.end(),
        [entry_name](const EditPlanRemovedPackageEntry& entry) {
            return entry.entry_name == entry_name;
        });
    return item == removed_package_entries_.end() ? nullptr : &*item;
}

const std::vector<EditPlanEntry>& EditPlan::entries() const noexcept
{
    return entries_;
}

const std::vector<EditPlanRemovedPart>& EditPlan::removed_parts() const noexcept
{
    return removed_parts_;
}

const std::vector<EditPlanPackageEntry>& EditPlan::package_entries() const noexcept
{
    return package_entries_;
}

const std::vector<EditPlanRemovedPackageEntry>& EditPlan::removed_package_entries()
    const noexcept
{
    return removed_package_entries_;
}

bool EditPlan::empty() const noexcept
{
    return entries_.empty() && removed_parts_.empty() && package_entries_.empty()
        && removed_package_entries_.empty();
}

std::size_t EditPlan::size() const noexcept
{
    return entries_.size();
}

void EditPlan::request_full_calculation(CalcChainAction calc_chain_action) noexcept
{
    full_calculation_on_load_ = true;
    calc_chain_action_ = calc_chain_action;
}

bool EditPlan::full_calculation_on_load() const noexcept
{
    return full_calculation_on_load_;
}

CalcChainAction EditPlan::calc_chain_action() const noexcept
{
    return calc_chain_action_;
}

void EditPlan::add_note(std::string note)
{
    if (note.empty()) {
        return;
    }
    if (std::find(notes_.begin(), notes_.end(), note) != notes_.end()) {
        return;
    }

    notes_.push_back(std::move(note));
}

const std::vector<std::string>& EditPlan::notes() const noexcept
{
    return notes_;
}

void EditPlan::add_relationship_target_audit(RelationshipTargetAudit audit)
{
    const auto existing = std::find_if(relationship_target_audits_.begin(),
        relationship_target_audits_.end(),
        [&audit](const RelationshipTargetAudit& entry) {
            return is_same_relationship_target_audit(entry, audit);
        });
    if (existing != relationship_target_audits_.end()) {
        existing->note = std::move(audit.note);
        return;
    }

    relationship_target_audits_.push_back(std::move(audit));
}

const std::vector<RelationshipTargetAudit>& EditPlan::relationship_target_audits()
    const noexcept
{
    return relationship_target_audits_;
}

void EditPlan::add_worksheet_relationship_reference_audit(
    WorksheetRelationshipReferenceAudit audit)
{
    const auto existing = std::find_if(worksheet_relationship_reference_audits_.begin(),
        worksheet_relationship_reference_audits_.end(),
        [&audit](const WorksheetRelationshipReferenceAudit& entry) {
            return is_same_worksheet_relationship_reference_audit(entry, audit);
        });
    if (existing != worksheet_relationship_reference_audits_.end()) {
        existing->note = std::move(audit.note);
        return;
    }

    worksheet_relationship_reference_audits_.push_back(std::move(audit));
}

const std::vector<WorksheetRelationshipReferenceAudit>&
EditPlan::worksheet_relationship_reference_audits() const noexcept
{
    return worksheet_relationship_reference_audits_;
}

void EditPlan::add_worksheet_payload_dependency_audit(
    WorksheetPayloadDependencyAudit audit)
{
    const auto existing = std::find_if(worksheet_payload_dependency_audits_.begin(),
        worksheet_payload_dependency_audits_.end(),
        [&audit](const WorksheetPayloadDependencyAudit& entry) {
            return is_same_worksheet_payload_dependency_audit(entry, audit);
        });
    if (existing != worksheet_payload_dependency_audits_.end()) {
        existing->note = std::move(audit.note);
        return;
    }

    worksheet_payload_dependency_audits_.push_back(std::move(audit));
}

const std::vector<WorksheetPayloadDependencyAudit>&
EditPlan::worksheet_payload_dependency_audits() const noexcept
{
    return worksheet_payload_dependency_audits_;
}

void EditPlan::add_workbook_payload_dependency_audit(
    WorkbookPayloadDependencyAudit audit)
{
    const auto existing = std::find_if(workbook_payload_dependency_audits_.begin(),
        workbook_payload_dependency_audits_.end(),
        [&audit](const WorkbookPayloadDependencyAudit& entry) {
            return is_same_workbook_payload_dependency_audit(entry, audit);
        });
    if (existing != workbook_payload_dependency_audits_.end()) {
        existing->note = std::move(audit.note);
        return;
    }

    workbook_payload_dependency_audits_.push_back(std::move(audit));
}

const std::vector<WorkbookPayloadDependencyAudit>&
EditPlan::workbook_payload_dependency_audits() const noexcept
{
    return workbook_payload_dependency_audits_;
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

bool PackageManifest::remove_part(const PartName& part_name) noexcept
{
    const auto old_size = parts_.size();
    parts_.erase(std::remove_if(parts_.begin(), parts_.end(),
                     [&part_name](const PackagePart& part) { return part.name == part_name; }),
        parts_.end());
    const bool removed_part = old_size != parts_.size();
    const bool removed_content_type = content_types_.remove_override(part_name);
    return removed_part || removed_content_type;
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

DependencyAnalyzer::DependencyAnalyzer(const PackageManifest& manifest) noexcept
    : manifest_(&manifest)
{
}

DependencyAnalysis DependencyAnalyzer::analyze_worksheet_stream_rewrite(
    const PartName& worksheet_part) const
{
    const auto* target = manifest_->find_part(worksheet_part);
    if (target == nullptr) {
        throw FastXlsxError("worksheet rewrite target is not registered in package manifest");
    }

    DependencyAnalysis analysis;
    analysis.parts.push_back(
        PartDependency {worksheet_part, "target worksheet part will be stream-rewritten"});

    if (const auto* relationships = manifest_->relationships_for(worksheet_part)) {
        analysis.has_worksheet_relationships = !relationships->empty();
        add_known_relationship_target_dependencies(
            analysis, *manifest_, worksheet_part, *relationships);
    }

    const PartName workbook_part("/xl/workbook.xml");
    if (manifest_->find_part(workbook_part) != nullptr) {
        add_dependency_if_exists(
            analysis, *manifest_, workbook_part.value(),
            "workbook metadata may need calcPr or definedNames review");
        analysis.workbook_payload_dependency_audits.push_back(
            WorkbookPayloadDependencyAudit {
                workbook_part,
                WorkbookPayloadDependencyAuditKind::CalcMetadata,
                WorkbookPayloadDependencyAuditScope::WorksheetRewrite,
                "calcPr",
                "worksheet rewrite may require workbook calcPr fullCalcOnLoad review",
            });
        analysis.workbook_payload_dependency_audits.push_back(
            WorkbookPayloadDependencyAudit {
                workbook_part,
                WorkbookPayloadDependencyAuditKind::DefinedNames,
                WorkbookPayloadDependencyAuditScope::WorksheetRewrite,
                "definedNames",
                "worksheet rewrite may affect workbook definedNames and requires caller review",
            });
    }
    add_dependency_if_exists(
        analysis, *manifest_, "/xl/sharedStrings.xml", "worksheet cells may reference shared strings");
    add_dependency_if_exists(
        analysis, *manifest_, "/xl/styles.xml", "worksheet cells may reference style ids");
    add_dependency_if_exists(
        analysis, *manifest_, "/xl/calcChain.xml", "worksheet rewrite may stale calcChain.xml");
    analysis.has_calc_chain =
        manifest_->find_part(PartName("/xl/calcChain.xml")) != nullptr;

    return analysis;
}

PartRewritePlanner::PartRewritePlanner(const PackageManifest& manifest) noexcept
    : manifest_(&manifest)
{
}

EditPlan PartRewritePlanner::plan_copy_original() const
{
    EditPlan plan;
    for (const PackagePart& part : manifest_->parts()) {
        plan.set_part(part.name, PartWriteMode::CopyOriginal, "preserve original package part");
    }
    return plan;
}

EditPlan PartRewritePlanner::plan_part_rewrite(
    const PartName& part_name, PartWriteMode write_mode, std::string reason) const
{
    if (write_mode == PartWriteMode::CopyOriginal) {
        throw FastXlsxError("part rewrite plan cannot use copy-original mode for the target");
    }
    if (manifest_->find_part(part_name) == nullptr) {
        throw FastXlsxError("part rewrite target is not registered in package manifest");
    }

    EditPlan plan = plan_copy_original();
    plan.set_part(part_name, write_mode, std::move(reason));
    return plan;
}

EditPlan PartRewritePlanner::plan_part_removal(
    const PartName& part_name, std::string reason) const
{
    if (manifest_->find_part(part_name) == nullptr) {
        throw FastXlsxError("part removal target is not registered in package manifest");
    }

    EditPlan plan = plan_copy_original();
    RemovedPartAuditPlan removal_audit =
        append_inbound_relationship_removal_audit(*manifest_, part_name, std::move(reason));
    plan.remove_part(part_name, std::move(removal_audit.reason),
        std::move(removal_audit.inbound_relationships));
    for (std::string& note : removal_audit.notes) {
        plan.add_note(std::move(note));
    }
    return plan;
}

EditPlan PartRewritePlanner::plan_worksheet_stream_rewrite(
    const PartName& worksheet_part, const ReferencePolicy& policy) const
{
    const DependencyAnalysis analysis =
        DependencyAnalyzer(*manifest_).analyze_worksheet_stream_rewrite(worksheet_part);

    if (analysis.has_worksheet_relationships
        && policy.unsupported_linked_part_action == ReferencePolicyAction::Fail) {
        throw FastXlsxError("worksheet rewrite has linked parts blocked by reference policy");
    }

    EditPlan plan = plan_part_rewrite(
        worksheet_part, PartWriteMode::StreamRewrite, "target worksheet part stream rewrite");
    annotate_copy_original_dependencies(plan, analysis, worksheet_part);

    if (analysis.has_worksheet_relationships) {
        plan.add_note(
            "worksheet relationships are preserved; linked object references require policy review");
        if (analysis.has_external_relationship_targets) {
            plan.add_note(
                "external relationship targets are preserved in owner .rels and are not package parts");
        }
        if (analysis.has_uri_qualified_internal_relationship_targets) {
            plan.add_note(
                "URI-qualified internal relationship targets require package structure review");
        }
        if (analysis.has_invalid_internal_relationship_targets) {
            plan.add_note(
                "invalid internal relationship targets require package structure review");
        }
        if (analysis.has_unresolved_internal_relationship_targets) {
            plan.add_note(
                "unresolved internal relationship targets require package structure review");
        }
        for (const std::string& note : analysis.relationship_target_notes) {
            plan.add_note(note);
        }
        for (const RelationshipTargetAudit& audit : analysis.relationship_target_audits) {
            plan.add_relationship_target_audit(audit);
        }
        if (policy.unsupported_linked_part_action
            == ReferencePolicyAction::RequestRecalculation) {
            plan.request_full_calculation(policy.calc_chain_action);
        }
    }

    if (policy.request_full_calculation_on_sheet_rewrite) {
        plan.request_full_calculation(policy.calc_chain_action);
    }
    if (analysis.has_calc_chain && policy.calc_chain_action == CalcChainAction::Remove) {
        plan.remove_part(PartName("/xl/calcChain.xml"),
            "calcChain.xml removed because worksheet rewrite can stale calculation order");
    }
    if (analysis.has_calc_chain && policy.calc_chain_action != CalcChainAction::Preserve) {
        plan.add_note("calcChain.xml must follow the edit plan calc-chain action");
    }
    for (const WorkbookPayloadDependencyAudit& audit :
        analysis.workbook_payload_dependency_audits) {
        plan.add_workbook_payload_dependency_audit(audit);
    }

    return plan;
}

PackageManifest make_minimal_workbook_manifest(
    std::size_t worksheet_count, bool include_shared_strings, bool include_document_properties,
    bool include_styles)
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

    if (include_styles) {
        manifest.add_part(PartName("/xl/styles.xml"), std::string(content_type_styles))
            .mark_generated();
        manifest.add_relationship(workbook_part,
            Relationship {
                "rId" + std::to_string(worksheet_count + 1 + (include_shared_strings ? 1 : 0)),
                std::string(relationship_type_styles),
                "styles.xml",
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
        append_escaped_xml_attribute(xml, item.extension);
        xml += R"(" ContentType=")";
        append_escaped_xml_attribute(xml, item.content_type);
        xml += R"("/>)";
    }

    for (const ContentTypeOverride& item : content_types.overrides()) {
        xml += R"(<Override PartName=")";
        append_escaped_xml_attribute(xml, item.part_name.value());
        xml += R"(" ContentType=")";
        append_escaped_xml_attribute(xml, item.content_type);
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
        append_escaped_xml_attribute(xml, relationship.id);
        xml += R"(" Type=")";
        append_escaped_xml_attribute(xml, relationship.type);
        xml += R"(" Target=")";
        append_escaped_xml_attribute(xml, relationship.target);
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
