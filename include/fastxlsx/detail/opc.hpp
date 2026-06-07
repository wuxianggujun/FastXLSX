#pragma once

#include <fastxlsx/document_properties.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace fastxlsx::detail {

/// Normalized OPC part name.
///
/// Internal helper only. The stored value is an absolute package part name such
/// as `/xl/workbook.xml`. It does not imply ZIP read/write support or existing
/// XLSX editing by itself.
class PartName {
public:
    explicit PartName(std::string_view value);

    [[nodiscard]] const std::string& value() const noexcept;
    [[nodiscard]] std::string zip_path() const;
    [[nodiscard]] std::string extension() const;

private:
    std::string value_;
};

[[nodiscard]] bool operator==(const PartName& left, const PartName& right) noexcept;
[[nodiscard]] bool operator<(const PartName& left, const PartName& right) noexcept;

/// OPC relationship metadata as stored in a `.rels` part.
struct Relationship {
    enum class TargetMode {
        Internal,
        External,
    };

    std::string id;
    std::string type;
    std::string target;
    TargetMode target_mode = TargetMode::Internal;
};

/// Small ordered relationship collection with relationship-id uniqueness.
class RelationshipSet {
public:
    Relationship& add(Relationship relationship);
    Relationship& add(std::string id, std::string type, std::string target,
        Relationship::TargetMode target_mode = Relationship::TargetMode::Internal);

    [[nodiscard]] Relationship* find_by_id(std::string_view id) noexcept;
    [[nodiscard]] const Relationship* find_by_id(std::string_view id) const noexcept;
    [[nodiscard]] const std::vector<Relationship>& relationships() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::vector<Relationship> relationships_;
};

struct ContentTypeDefault {
    std::string extension;
    std::string content_type;
};

struct ContentTypeOverride {
    PartName part_name;
    std::string content_type;
};

/// Planned write strategy for a package part in a future edit pass.
///
/// This is edit-planning metadata only. It does not imply ZIP read/write support
/// or existing-XLSX editing behavior.
enum class PartWriteMode {
    CopyOriginal,
    GenerateSmallXml,
    StreamRewrite,
    LocalDomRewrite,
};

/// `[Content_Types].xml` defaults and overrides.
///
/// Overrides win over extension defaults during lookup. Duplicate defaults or
/// overrides are ignored when identical and rejected when they conflict.
class ContentTypesManifest {
public:
    const ContentTypeDefault& add_default(std::string extension, std::string content_type);
    const ContentTypeOverride& add_override(PartName part_name, std::string content_type);

    [[nodiscard]] const std::string* content_type_for(const PartName& part_name) const noexcept;
    [[nodiscard]] const ContentTypeDefault* default_for(std::string_view extension) const noexcept;
    [[nodiscard]] const ContentTypeOverride* override_for(const PartName& part_name) const noexcept;
    [[nodiscard]] const std::vector<ContentTypeDefault>& defaults() const noexcept;
    [[nodiscard]] const std::vector<ContentTypeOverride>& overrides() const noexcept;

private:
    std::vector<ContentTypeDefault> defaults_;
    std::vector<ContentTypeOverride> overrides_;
};

/// Internal helper for registering and resolving OPC content types.
///
/// This is a small convenience wrapper over `ContentTypesManifest`; it does not
/// imply package read/write support.
class ContentTypeRegistry {
public:
    const ContentTypeDefault& add_default(std::string extension, std::string content_type);
    const ContentTypeOverride& add_override(PartName part_name, std::string content_type);

    [[nodiscard]] const std::string* content_type_for(const PartName& part_name) const noexcept;
    [[nodiscard]] const ContentTypeDefault* default_for(std::string_view extension) const noexcept;
    [[nodiscard]] const ContentTypeOverride* override_for(const PartName& part_name) const noexcept;
    [[nodiscard]] ContentTypesManifest& manifest() noexcept;
    [[nodiscard]] const ContentTypesManifest& manifest() const noexcept;

private:
    ContentTypesManifest manifest_;
};

/// One known package part and relationships sourced from that part.
struct PackagePart {
    PartName name;
    std::string content_type;
    RelationshipSet relationships;
    PartWriteMode write_mode = PartWriteMode::CopyOriginal;
    bool preserve_original = true;
    bool dirty = false;
    bool generated = false;

    void set_write_mode(PartWriteMode mode) noexcept;
    void mark_dirty() noexcept;
    void mark_generated() noexcept;
};

/// Internal index of known package parts.
///
/// This only tracks part metadata and content types. It has no ZIP reader,
/// writer, or existing-package editing behavior.
class PartIndex {
public:
    PackagePart& add_part(PartName part_name, std::string content_type = {});
    PackagePart& ensure_part(PartName part_name, std::string content_type = {});

    [[nodiscard]] PackagePart* find_part(const PartName& part_name) noexcept;
    [[nodiscard]] const PackagePart* find_part(const PartName& part_name) const noexcept;
    [[nodiscard]] ContentTypeRegistry& content_types() noexcept;
    [[nodiscard]] const ContentTypeRegistry& content_types() const noexcept;
    [[nodiscard]] const std::vector<PackagePart>& parts() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::vector<PackagePart> parts_;
    ContentTypeRegistry content_types_;
};

/// Internal relationship graph keyed by package owner or source part.
///
/// Relationship ids are unique within each owner only, matching OPC `.rels`
/// files. Source-part relationships require the source part to exist in the
/// supplied `PartIndex`.
class RelationshipGraph {
public:
    explicit RelationshipGraph(const PartIndex& part_index);

    Relationship& add_package_relationship(Relationship relationship);
    Relationship& add_package_relationship(std::string type, std::string target,
        Relationship::TargetMode target_mode = Relationship::TargetMode::Internal);

    Relationship& add_relationship(const PartName& source_part, Relationship relationship);
    Relationship& add_relationship(const PartName& source_part, std::string type,
        std::string target,
        Relationship::TargetMode target_mode = Relationship::TargetMode::Internal);

    [[nodiscard]] RelationshipSet& package_relationships() noexcept;
    [[nodiscard]] const RelationshipSet& package_relationships() const noexcept;
    [[nodiscard]] RelationshipSet* relationships_for(const PartName& source_part) noexcept;
    [[nodiscard]] const RelationshipSet* relationships_for(
        const PartName& source_part) const noexcept;

private:
    struct PartRelationshipSet {
        PartName owner;
        RelationshipSet relationships;
    };

    [[nodiscard]] RelationshipSet& ensure_relationships_for(const PartName& source_part);

    const PartIndex* part_index_;
    RelationshipSet package_relationships_;
    std::vector<PartRelationshipSet> part_relationships_;
};

/// Lightweight internal package manifest.
///
/// This is only an index for known parts, content types, and relationships.
/// It intentionally contains no ZIP reader/writer and no full existing-XLSX
/// editing behavior.
class PackageManifest {
public:
    PackagePart& add_part(PartName part_name, std::string content_type);
    PackagePart& ensure_part(PartName part_name, std::string content_type = {});
    PackagePart& set_part_write_mode(const PartName& part_name, PartWriteMode mode);
    PackagePart& mark_part_dirty(const PartName& part_name);
    PackagePart& mark_part_generated(const PartName& part_name);

    [[nodiscard]] PackagePart* find_part(const PartName& part_name) noexcept;
    [[nodiscard]] const PackagePart* find_part(const PartName& part_name) const noexcept;
    [[nodiscard]] RelationshipSet* relationships_for(const PartName& part_name) noexcept;
    [[nodiscard]] const RelationshipSet* relationships_for(const PartName& part_name) const noexcept;

    Relationship& add_relationship(const PartName& source_part, Relationship relationship);
    Relationship& add_package_relationship(Relationship relationship);

    [[nodiscard]] RelationshipSet& package_relationships() noexcept;
    [[nodiscard]] const RelationshipSet& package_relationships() const noexcept;
    [[nodiscard]] ContentTypesManifest& content_types() noexcept;
    [[nodiscard]] const ContentTypesManifest& content_types() const noexcept;
    [[nodiscard]] const std::vector<PackagePart>& parts() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::vector<PackagePart> parts_;
    ContentTypesManifest content_types_;
    RelationshipSet package_relationships_;
};

[[nodiscard]] PackageManifest make_minimal_workbook_manifest(
    std::size_t worksheet_count, bool include_shared_strings = false,
    bool include_document_properties = true, bool include_styles = false);
[[nodiscard]] std::string build_core_properties(const DocumentProperties& properties = {});
[[nodiscard]] std::string build_extended_properties(const DocumentProperties& properties = {});
[[nodiscard]] std::string serialize_content_types(const ContentTypesManifest& content_types);
[[nodiscard]] std::string serialize_relationships(const RelationshipSet& relationships);

} // namespace fastxlsx::detail
