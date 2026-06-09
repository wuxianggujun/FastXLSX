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
    [[nodiscard]] std::size_t remove_by_type(std::string_view type) noexcept;
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

/// Structured role for non-part package-entry audit records.
///
/// These entries describe ZIP metadata entries in the Patch plan. They are not
/// workbook parts and do not imply a general relationship/content-type editor.
enum class PackageEntryAuditKind {
    Generic,
    ContentTypes,
    PackageRelationships,
    SourceRelationships,
};

/// Planned calc-chain handling for a future Patch save.
///
/// This records edit-plan intent. Current internal PackageEditor helpers can
/// omit a stale calcChain part in narrow Patch paths, but they do not rebuild
/// calcChain.xml or evaluate formulas.
enum class CalcChainAction {
    Preserve,
    Remove,
    Rebuild,
};

/// Conservative action for linked worksheet objects, replacement worksheet
/// relationship references, and audit-only replacement payload dependencies
/// that are not yet natively editable.
enum class ReferencePolicyAction {
    Preserve,
    RequestRecalculation,
    Fail,
};

/// Internal policy used while turning a user edit into part-level decisions.
struct ReferencePolicy {
    ReferencePolicyAction unsupported_linked_part_action = ReferencePolicyAction::Preserve;
    bool request_full_calculation_on_sheet_rewrite = true;
    CalcChainAction calc_chain_action = CalcChainAction::Remove;
};

/// One part-level output decision in a future Patch edit plan.
struct EditPlanEntry {
    PartName part_name;
    PartWriteMode write_mode = PartWriteMode::CopyOriginal;
    std::string reason;
    // Relationship-derived copy-original audit metadata. Static dependencies
    // and rewritten/generated entries leave these fields empty.
    std::string relationship_owner_part;
    std::string relationship_id;
    std::string relationship_type;
    std::string relationship_target;
};

struct RelationshipTargetAudit {
    PartName owner_part;
    std::string relationship_id;
    std::string relationship_type;
    std::string target;
    std::string normalized_target;
    std::string note;
};

/// Structured audit kind for worksheet XML `r:id` references checked against
/// the preserved worksheet-owned `.rels` entry.
enum class WorksheetRelationshipReferenceAuditKind {
    MissingRelationships,
    MissingRelationshipId,
    UnreferencedRelationshipId,
    TypeMismatch,
};

/// Audit-only record for relationship ids mentioned by replacement worksheet
/// XML or preserved worksheet relationships.
///
/// These records support Patch traceability. They do not imply namespace
/// validation, relationship repair, pruning, or linked-part regeneration.
struct WorksheetRelationshipReferenceAudit {
    PartName worksheet_part;
    WorksheetRelationshipReferenceAuditKind kind =
        WorksheetRelationshipReferenceAuditKind::MissingRelationshipId;
    std::string element;
    std::string relationship_id;
    std::string expected_relationship_type;
    std::string actual_relationship_type;
    std::string note;
};

/// Structured audit kind for worksheet payload dependencies that are currently
/// review-only in the Patch path.
enum class WorksheetPayloadDependencyAuditKind {
    SharedStrings,
    Styles,
    Formula,
    RangeMetadata,
    RelationshipMetadata,
};

/// Source operation that exposed a worksheet payload dependency.
enum class WorksheetPayloadDependencyAuditScope {
    WorksheetReplacement,
    SheetDataReplacement,
    PreservedWorksheetMetadata,
};

/// Audit-only record for worksheet payload references that can affect linked
/// OpenXML parts or worksheet-local metadata.
///
/// These records support Patch traceability. They do not imply sharedStrings
/// migration, style merging, formula evaluation, calcChain rebuild,
/// relationship repair, or range metadata synchronization.
struct WorksheetPayloadDependencyAudit {
    PartName worksheet_part;
    WorksheetPayloadDependencyAuditKind kind =
        WorksheetPayloadDependencyAuditKind::RangeMetadata;
    WorksheetPayloadDependencyAuditScope scope =
        WorksheetPayloadDependencyAuditScope::WorksheetReplacement;
    std::string element;
    std::string note;
};

/// Structured audit kind for workbook payload dependencies that are currently
/// review-only in the Patch path.
enum class WorkbookPayloadDependencyAuditKind {
    CalcMetadata,
    DefinedNames,
    SheetCatalog,
};

/// Source operation that exposed a workbook payload dependency.
enum class WorkbookPayloadDependencyAuditScope {
    WorksheetRewrite,
    SheetCatalogRename,
    WorkbookCalcMetadataRewrite,
};

/// Audit-only record for workbook payload references that can affect sheet
/// edits or workbook-level calculation metadata.
///
/// These records support Patch traceability. They do not imply defined-name
/// synchronization, formula rewriting, sheet-catalog repair, calcChain rebuild,
/// or workbook metadata DOM support.
struct WorkbookPayloadDependencyAudit {
    PartName workbook_part;
    WorkbookPayloadDependencyAuditKind kind =
        WorkbookPayloadDependencyAuditKind::CalcMetadata;
    WorkbookPayloadDependencyAuditScope scope =
        WorkbookPayloadDependencyAuditScope::WorksheetRewrite;
    std::string element;
    std::string note;
};

/// One package part intentionally omitted by a Patch output plan.
struct RemovedPartInboundRelationshipAudit {
    // Empty for package `_rels/.rels`; otherwise the source owner part name.
    std::string owner_part;
    std::string owner_entry;
    std::string relationship_id;
    std::string relationship_type;
    std::string relationship_target;
    PartName target_part;
};

/// One package part intentionally omitted by a Patch output plan.
struct EditPlanRemovedPart {
    PartName part_name;
    std::string reason;
    std::vector<RemovedPartInboundRelationshipAudit> inbound_relationships;
};

/// One non-part package entry output decision in a Patch edit plan.
///
/// This covers metadata entries such as `[Content_Types].xml` and `.rels`
/// files. They are ZIP package entries, but not workbook parts. The write mode
/// can be `CopyOriginal` when the entry is intentionally preserved for audit
/// visibility.
struct EditPlanPackageEntry {
    std::string entry_name;
    PartWriteMode write_mode = PartWriteMode::CopyOriginal;
    std::string reason;
    PackageEntryAuditKind audit_kind = PackageEntryAuditKind::Generic;
    std::string owner_part;
};

/// One non-part package entry intentionally omitted by a Patch output plan.
struct EditPlanRemovedPackageEntry {
    std::string entry_name;
    std::string reason;
    PackageEntryAuditKind audit_kind = PackageEntryAuditKind::Generic;
    std::string owner_part;
};

/// Internal part-level and metadata-entry edit plan.
///
/// This is planning metadata for the Patch path. It does not read ZIP entries,
/// write packages, mutate relationships, or implement public existing-file
/// editing by itself.
class EditPlan {
public:
    EditPlanEntry& set_part(PartName part_name, PartWriteMode write_mode,
        std::string reason = {});

    [[nodiscard]] EditPlanEntry* find_part(const PartName& part_name) noexcept;
    [[nodiscard]] const EditPlanEntry* find_part(const PartName& part_name) const noexcept;
    EditPlanRemovedPart& remove_part(PartName part_name, std::string reason = {});
    EditPlanRemovedPart& remove_part(PartName part_name, std::string reason,
        std::vector<RemovedPartInboundRelationshipAudit> inbound_relationships);
    [[nodiscard]] const EditPlanRemovedPart* find_removed_part(
        const PartName& part_name) const noexcept;
    EditPlanPackageEntry& set_package_entry(std::string entry_name,
        PartWriteMode write_mode, std::string reason = {},
        PackageEntryAuditKind audit_kind = PackageEntryAuditKind::Generic,
        std::string owner_part = {});
    [[nodiscard]] EditPlanPackageEntry* find_package_entry(
        std::string_view entry_name) noexcept;
    [[nodiscard]] const EditPlanPackageEntry* find_package_entry(
        std::string_view entry_name) const noexcept;
    EditPlanRemovedPackageEntry& remove_package_entry(
        std::string entry_name, std::string reason = {},
        PackageEntryAuditKind audit_kind = PackageEntryAuditKind::Generic,
        std::string owner_part = {});
    [[nodiscard]] const EditPlanRemovedPackageEntry* find_removed_package_entry(
        std::string_view entry_name) const noexcept;
    [[nodiscard]] const std::vector<EditPlanEntry>& entries() const noexcept;
    [[nodiscard]] const std::vector<EditPlanRemovedPart>& removed_parts() const noexcept;
    [[nodiscard]] const std::vector<EditPlanPackageEntry>& package_entries() const noexcept;
    [[nodiscard]] const std::vector<EditPlanRemovedPackageEntry>& removed_package_entries()
        const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

    void request_full_calculation(CalcChainAction calc_chain_action) noexcept;
    [[nodiscard]] bool full_calculation_on_load() const noexcept;
    [[nodiscard]] CalcChainAction calc_chain_action() const noexcept;

    void add_note(std::string note);
    [[nodiscard]] const std::vector<std::string>& notes() const noexcept;
    void add_relationship_target_audit(RelationshipTargetAudit audit);
    [[nodiscard]] const std::vector<RelationshipTargetAudit>& relationship_target_audits()
        const noexcept;
    void add_worksheet_relationship_reference_audit(
        WorksheetRelationshipReferenceAudit audit);
    [[nodiscard]] const std::vector<WorksheetRelationshipReferenceAudit>&
    worksheet_relationship_reference_audits() const noexcept;
    void add_worksheet_payload_dependency_audit(WorksheetPayloadDependencyAudit audit);
    [[nodiscard]] const std::vector<WorksheetPayloadDependencyAudit>&
    worksheet_payload_dependency_audits() const noexcept;
    void add_workbook_payload_dependency_audit(WorkbookPayloadDependencyAudit audit);
    [[nodiscard]] const std::vector<WorkbookPayloadDependencyAudit>&
    workbook_payload_dependency_audits() const noexcept;

private:
    std::vector<EditPlanEntry> entries_;
    std::vector<EditPlanRemovedPart> removed_parts_;
    std::vector<EditPlanPackageEntry> package_entries_;
    std::vector<EditPlanRemovedPackageEntry> removed_package_entries_;
    bool full_calculation_on_load_ = false;
    CalcChainAction calc_chain_action_ = CalcChainAction::Preserve;
    std::vector<std::string> notes_;
    std::vector<RelationshipTargetAudit> relationship_target_audits_;
    std::vector<WorksheetRelationshipReferenceAudit> worksheet_relationship_reference_audits_;
    std::vector<WorksheetPayloadDependencyAudit> worksheet_payload_dependency_audits_;
    std::vector<WorkbookPayloadDependencyAudit> workbook_payload_dependency_audits_;
};

/// `[Content_Types].xml` defaults and overrides.
///
/// Overrides win over extension defaults during lookup. Duplicate defaults or
/// overrides are ignored when identical and rejected when they conflict.
class ContentTypesManifest {
public:
    const ContentTypeDefault& add_default(std::string extension, std::string content_type);
    const ContentTypeOverride& add_override(PartName part_name, std::string content_type);
    [[nodiscard]] bool remove_override(const PartName& part_name) noexcept;

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
    [[nodiscard]] bool remove_part(const PartName& part_name) noexcept;
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

struct PartDependency {
    PartName part_name;
    std::string reason;
    std::string relationship_owner_part;
    std::string relationship_id;
    std::string relationship_type;
    std::string relationship_target;
};

/// Internal dependency summary for a worksheet-level Patch operation.
///
/// The analyzer is intentionally conservative and part-level. It does not parse
/// large worksheet XML into a DOM or promise complete object support. Known
/// internal worksheet relationship targets may be listed as dependencies;
/// external relationship targets are not package parts, but can be flagged for
/// edit-plan audit notes. URI-qualified internal relationship targets are
/// flagged for review; their base target can still be listed as a dependency
/// when it resolves to a registered package part. Internal relationship targets
/// that are not registered in the current manifest are flagged for review
/// without inventing package parts. Invalid internal targets, such as relative
/// paths that escape the package root, are recorded as package-structure audit
/// state instead of aborting dependency planning. Relationship-driven audit text
/// records owner context, relationship id, relationship type, and target path
/// for traceability; it is not relationship validation or repair.
struct DependencyAnalysis {
    std::vector<PartDependency> parts;
    std::vector<RelationshipTargetAudit> relationship_target_audits;
    std::vector<WorkbookPayloadDependencyAudit> workbook_payload_dependency_audits;
    std::vector<std::string> relationship_target_notes;
    bool has_worksheet_relationships = false;
    bool has_external_relationship_targets = false;
    bool has_uri_qualified_internal_relationship_targets = false;
    bool has_invalid_internal_relationship_targets = false;
    bool has_unresolved_internal_relationship_targets = false;
    bool has_calc_chain = false;
};

class DependencyAnalyzer {
public:
    explicit DependencyAnalyzer(const PackageManifest& manifest) noexcept;

    [[nodiscard]] DependencyAnalysis analyze_worksheet_stream_rewrite(
        const PartName& worksheet_part) const;

private:
    const PackageManifest* manifest_;
};

/// Converts package metadata and dependency policy into part rewrite decisions.
///
/// This is the internal Patch planning boundary for copy-original defaults,
/// targeted rewrites, and explicit part removals. It does not prune
/// relationships, repair targets, or imply public existing-file editing.
class PartRewritePlanner {
public:
    explicit PartRewritePlanner(const PackageManifest& manifest) noexcept;

    [[nodiscard]] EditPlan plan_copy_original() const;
    [[nodiscard]] EditPlan plan_part_rewrite(const PartName& part_name, PartWriteMode write_mode,
        std::string reason = {}) const;
    [[nodiscard]] EditPlan plan_part_removal(const PartName& part_name,
        std::string reason = {}) const;
    [[nodiscard]] EditPlan plan_worksheet_stream_rewrite(const PartName& worksheet_part,
        const ReferencePolicy& policy = {}) const;

private:
    const PackageManifest* manifest_;
};

[[nodiscard]] PackageManifest make_minimal_workbook_manifest(
    std::size_t worksheet_count, bool include_shared_strings = false,
    bool include_document_properties = true, bool include_styles = false);
[[nodiscard]] std::string build_core_properties(const DocumentProperties& properties = {});
[[nodiscard]] std::string build_extended_properties(const DocumentProperties& properties = {});
[[nodiscard]] std::string serialize_content_types(const ContentTypesManifest& content_types);
[[nodiscard]] std::string serialize_relationships(const RelationshipSet& relationships);

} // namespace fastxlsx::detail
