#include "../src/package_editor.hpp"
#include "../src/package_writer.hpp"
#include "zip_test_utils.hpp"

#include <fastxlsx/detail/cell_store.hpp>
#include <fastxlsx/streaming_writer.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
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

void check_contains(std::string_view haystack, std::string_view needle, const char* message)
{
    check(haystack.find(needle) != std::string_view::npos, message);
}

void check_staged_file_chunk_read_progress(std::string_view error,
    std::size_t read_attempt,
    std::uint64_t bytes_read,
    const char* message)
{
    check_contains(error,
        std::string("staged package-entry file chunk read attempt ")
            + std::to_string(read_attempt),
        message);
    check_contains(error,
        std::string("after reading ") + std::to_string(bytes_read) + " bytes",
        message);
}

void check_staged_chunk_replay_progress(std::string_view error,
    std::size_t read_attempt,
    std::size_t emitted_chunks,
    std::uint64_t emitted_bytes,
    std::uint64_t last_chunk_bytes,
    const char* message)
{
    check_contains(error,
        std::string("staged package-entry chunk replay read attempt ")
            + std::to_string(read_attempt),
        message);
    check_contains(error,
        std::string("after emitting ") + std::to_string(emitted_chunks) + " chunk",
        message);
    check_contains(error,
        std::to_string(emitted_bytes) + " bytes",
        message);
    if (emitted_chunks > 0) {
        check_contains(error,
            std::string("last chunk ") + std::to_string(last_chunk_bytes) + " bytes",
            message);
    }
}

void check_staged_chunk_expected_total(std::string_view error,
    std::uint64_t expected_bytes,
    const char* message)
{
    check_contains(error,
        std::string("expected staged payload total ")
            + std::to_string(expected_bytes) + " bytes",
        message);
}

void check_staged_chunk_expected_remaining(std::string_view error,
    std::uint64_t expected_bytes,
    const char* message)
{
    check_contains(error,
        std::string("expected staged payload remaining ")
            + std::to_string(expected_bytes) + " bytes",
        message);
}

void check_staged_chunk_expected_size(std::string_view error,
    std::uint64_t expected_bytes,
    const char* message)
{
    check_contains(error,
        std::string("expected chunk ") + std::to_string(expected_bytes) + " bytes",
        message);
}

void check_staged_chunk_replay_cursor(std::string_view error,
    std::size_t chunk_index,
    std::size_t chunk_count,
    const char* message)
{
    check_contains(error,
        std::string("staged package-entry chunk index ")
            + std::to_string(chunk_index) + " of " + std::to_string(chunk_count),
        message);
}

void check_not_contains(std::string_view haystack, std::string_view needle, const char* message)
{
    check(haystack.find(needle) == std::string_view::npos, message);
}

fastxlsx::detail::WorksheetCellReplacement worksheet_cell_replacement(
    std::string_view cell_reference, std::string_view materialized_xml)
{
    return fastxlsx::detail::WorksheetCellReplacement {
        cell_reference,
        fastxlsx::detail::WorksheetCellReplacementPayload::from_materialized_xml(materialized_xml),
    };
}

fastxlsx::detail::WorksheetCellReplacement chunked_worksheet_cell_replacement(
    std::string_view cell_reference, std::span<const std::string_view> chunks)
{
    return fastxlsx::detail::WorksheetCellReplacement {
        cell_reference,
        fastxlsx::detail::WorksheetCellReplacementPayload::from_chunks(chunks),
    };
}

void check_entry_bytes(const fastxlsx::detail::PackageReader& reader,
    std::string_view entry_name,
    const std::string& expected)
{
    const std::string actual = reader.read_entry(entry_name);
    if (actual != expected) {
        throw TestFailure("package entry bytes changed: " + std::string(entry_name));
    }
}

void check_preserved_source_entries(const fastxlsx::detail::PackageReader& source_reader,
    const fastxlsx::detail::PackageReader& output_reader,
    std::string_view rewritten_entry_name = {})
{
    const std::vector<fastxlsx::detail::PackageReaderEntry>& source_entries =
        source_reader.entries();
    const std::vector<fastxlsx::detail::PackageReaderEntry>& output_entries =
        output_reader.entries();
    check(output_entries.size() == source_entries.size(),
        "PackageEditor output should keep the source entry count");

    for (std::size_t index = 0; index < source_entries.size(); ++index) {
        const fastxlsx::detail::PackageReaderEntry& source_entry = source_entries[index];
        const fastxlsx::detail::PackageReaderEntry& output_entry = output_entries[index];
        if (output_entry.name != source_entry.name) {
            throw TestFailure("PackageEditor output entry order changed: " + source_entry.name);
        }
        check(output_reader.find_entry(source_entry.name) != nullptr,
            "PackageEditor output should keep every source entry");

        if (source_entry.name == rewritten_entry_name) {
            continue;
        }
        if (output_entry.compression_method != source_entry.compression_method) {
            throw TestFailure(
                "PackageEditor changed copied entry compression method: " + source_entry.name);
        }
        if (output_entry.crc32 != source_entry.crc32) {
            throw TestFailure("PackageEditor changed copied entry CRC: " + source_entry.name);
        }
        if (output_entry.uncompressed_size != source_entry.uncompressed_size) {
            throw TestFailure(
                "PackageEditor changed copied entry uncompressed size: " + source_entry.name);
        }
        check_entry_bytes(output_reader, source_entry.name, source_reader.read_entry(source_entry.name));
    }
}

void rewrite_package_entry_as_stored(
    const std::filesystem::path& path, std::string_view entry_name, std::string replacement)
{
    std::map<std::string, std::string> source_entries =
        fastxlsx::test::read_zip_entries(path);
    auto entry = source_entries.find(std::string(entry_name));
    if (entry == source_entries.end()) {
        throw TestFailure("test package entry to rewrite was not found");
    }
    entry->second = std::move(replacement);

    std::vector<fastxlsx::detail::PackageEntry> rewritten_entries;
    rewritten_entries.reserve(source_entries.size());
    for (auto& [name, body] : source_entries) {
        rewritten_entries.emplace_back(name, std::move(body));
    }

    fastxlsx::detail::PackageWriterOptions options;
    options.backend = fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap;
    fastxlsx::detail::write_package(path, rewritten_entries, options);
}

template <typename Notes>
bool has_note_containing(const Notes& notes, std::initializer_list<std::string_view> needles)
{
    for (const std::string& note : notes) {
        bool matched = true;
        for (std::string_view needle : needles) {
            if (note.find(needle) == std::string::npos) {
                matched = false;
                break;
            }
        }
        if (matched) {
            return true;
        }
    }
    return false;
}

template <typename Audits>
bool has_payload_audit(const Audits& audits,
    const fastxlsx::detail::PartName& worksheet_part,
    fastxlsx::detail::WorksheetPayloadDependencyAuditKind kind,
    fastxlsx::detail::WorksheetPayloadDependencyAuditScope scope,
    std::string_view element,
    std::initializer_list<std::string_view> note_needles = {})
{
    for (const fastxlsx::detail::WorksheetPayloadDependencyAudit& audit : audits) {
        if (audit.worksheet_part != worksheet_part || audit.kind != kind
            || audit.scope != scope || audit.element != element) {
            continue;
        }

        bool matched = true;
        for (std::string_view needle : note_needles) {
            if (audit.note.find(needle) == std::string::npos) {
                matched = false;
                break;
            }
        }
        if (matched) {
            return true;
        }
    }
    return false;
}

template <typename Audits>
bool has_workbook_payload_audit(const Audits& audits,
    const fastxlsx::detail::PartName& workbook_part,
    fastxlsx::detail::WorkbookPayloadDependencyAuditKind kind,
    fastxlsx::detail::WorkbookPayloadDependencyAuditScope scope,
    std::string_view element,
    std::initializer_list<std::string_view> note_needles = {})
{
    for (const fastxlsx::detail::WorkbookPayloadDependencyAudit& audit : audits) {
        if (audit.workbook_part != workbook_part || audit.kind != kind
            || audit.scope != scope || audit.element != element) {
            continue;
        }

        bool matched = true;
        for (std::string_view needle : note_needles) {
            if (audit.note.find(needle) == std::string::npos) {
                matched = false;
                break;
            }
        }
        if (matched) {
            return true;
        }
    }
    return false;
}

void check_manifest_write_mode(const fastxlsx::detail::PackageEditor& editor,
    const fastxlsx::detail::PartName& part_name,
    fastxlsx::detail::PartWriteMode write_mode,
    const char* message)
{
    const auto* part = editor.manifest().find_part(part_name);
    check(part != nullptr && part->write_mode == write_mode, message);
}

const fastxlsx::detail::PackageEditorOutputEntryPlan* find_output_entry_plan(
    const std::vector<fastxlsx::detail::PackageEditorOutputEntryPlan>& plans,
    std::string_view entry_name)
{
    const auto item = std::find_if(plans.begin(), plans.end(),
        [entry_name](const fastxlsx::detail::PackageEditorOutputEntryPlan& plan) {
            return plan.entry_name == entry_name;
        });
    return item == plans.end() ? nullptr : &*item;
}

void check_output_entry_plan(
    const std::vector<fastxlsx::detail::PackageEditorOutputEntryPlan>& plans,
    std::string_view entry_name,
    fastxlsx::detail::PartWriteMode write_mode,
    bool source_entry,
    bool generated,
    bool copied_from_source,
    bool omitted,
    const char* message)
{
    const fastxlsx::detail::PackageEditorOutputEntryPlan* plan =
        find_output_entry_plan(plans, entry_name);
    if (plan == nullptr) {
        throw TestFailure(std::string(message) + ": missing " + std::string(entry_name));
    }
    check(plan->write_mode == write_mode, message);
    check(plan->source_entry == source_entry, message);
    check(plan->generated == generated, message);
    check(plan->copied_from_source == copied_from_source, message);
    check(plan->omitted == omitted, message);
}

void check_output_entry_part_context(
    const std::vector<fastxlsx::detail::PackageEditorOutputEntryPlan>& plans,
    std::string_view entry_name,
    bool package_part,
    std::string_view part_name,
    const char* message)
{
    const fastxlsx::detail::PackageEditorOutputEntryPlan* plan =
        find_output_entry_plan(plans, entry_name);
    if (plan == nullptr) {
        throw TestFailure(std::string(message) + ": missing " + std::string(entry_name));
    }
    check(plan->package_part == package_part, message);
    check(plan->part_name == part_name, message);
}

void check_output_entry_staged_replacement_chunks(
    const std::vector<fastxlsx::detail::PackageEditorOutputEntryPlan>& plans,
    std::string_view entry_name,
    bool expected,
    const char* message)
{
    const fastxlsx::detail::PackageEditorOutputEntryPlan* plan =
        find_output_entry_plan(plans, entry_name);
    if (plan == nullptr) {
        throw TestFailure(std::string(message) + ": missing " + std::string(entry_name));
    }
    check(plan->staged_replacement_chunks == expected, message);
}

void check_output_entry_materialized_replacement(
    const std::vector<fastxlsx::detail::PackageEditorOutputEntryPlan>& plans,
    std::string_view entry_name,
    bool expected,
    const char* message)
{
    const fastxlsx::detail::PackageEditorOutputEntryPlan* plan =
        find_output_entry_plan(plans, entry_name);
    if (plan == nullptr) {
        throw TestFailure(std::string(message) + ": missing " + std::string(entry_name));
    }
    check(plan->materialized_replacement == expected, message);
    if (expected) {
        check_contains(plan->materialized_replacement_reason,
            "materialized payload", message);
    } else {
        check(plan->materialized_replacement_reason.empty(), message);
    }
}

void check_output_entry_materialized_replacement_reason(
    const std::vector<fastxlsx::detail::PackageEditorOutputEntryPlan>& plans,
    std::string_view entry_name,
    std::string_view expected_reason_fragment,
    const char* message)
{
    const fastxlsx::detail::PackageEditorOutputEntryPlan* plan =
        find_output_entry_plan(plans, entry_name);
    if (plan == nullptr) {
        throw TestFailure(std::string(message) + ": missing " + std::string(entry_name));
    }
    check(plan->materialized_replacement, message);
    check_contains(plan->materialized_replacement_reason,
        expected_reason_fragment, message);
}

void check_source_package_parts_are_file_backed(
    const fastxlsx::detail::PackageReader& reader,
    const std::vector<fastxlsx::detail::PackageEditorOutputEntryPlan>& plans,
    const char* message)
{
    for (const fastxlsx::detail::PackagePart& part : reader.part_index().parts()) {
        const std::string entry_name = part.name.zip_path();
        const fastxlsx::detail::PackageEditorOutputEntryPlan* plan =
            find_output_entry_plan(plans, entry_name);
        if (plan == nullptr) {
            throw TestFailure(std::string(message) + ": missing " + entry_name);
        }
        check(plan->source_entry, message);
        check(plan->copied_from_source, message);
        check(!plan->omitted, message);
        check(plan->package_part, message);
        check(plan->part_name == part.name.value(), message);
        check(plan->file_backed_source_copy, message);
        check(!plan->file_backed_source_copy_reason.empty(), message);
        check(!plan->staged_replacement_chunks, message);
        check(!plan->materialized_replacement, message);
        check(plan->materialized_replacement_reason.empty(), message);
    }
}

void check_source_metadata_entry_is_file_backed(
    const std::vector<fastxlsx::detail::PackageEditorOutputEntryPlan>& plans,
    std::string_view entry_name,
    std::string_view expected_reason_fragment,
    const char* message)
{
    const fastxlsx::detail::PackageEditorOutputEntryPlan* plan =
        find_output_entry_plan(plans, entry_name);
    if (plan == nullptr) {
        throw TestFailure(std::string(message) + ": missing " + std::string(entry_name));
    }
    check(plan->source_entry, message);
    check(plan->copied_from_source, message);
    check(!plan->omitted, message);
    check(!plan->package_part, message);
    check(plan->file_backed_source_copy, message);
    check(!plan->file_backed_source_copy_reason.empty(), message);
    check(!plan->staged_replacement_chunks, message);
    check(!plan->materialized_replacement, message);
    check(plan->materialized_replacement_reason.empty(), message);
    check_contains(plan->file_backed_source_copy_reason,
        expected_reason_fragment, message);
}

void check_output_plan_preserves_source_copy_original(
    const fastxlsx::detail::PackageEditor& editor,
    const fastxlsx::detail::PackageEditorOutputPlan& output_plan,
    const char* message)
{
    check(output_plan.entries.size() == editor.reader().entries().size(), message);
    check(editor.planned_output_entries().size() == output_plan.entries.size(), message);
    for (const fastxlsx::detail::PackageReaderEntry& entry : editor.reader().entries()) {
        check_output_entry_plan(output_plan.entries, entry.name,
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            message);
        const fastxlsx::detail::PackageEditorOutputEntryPlan* plan =
            find_output_entry_plan(output_plan.entries, entry.name);
        check(plan != nullptr && plan->file_backed_source_copy, message);
        check(plan != nullptr && !plan->file_backed_source_copy_reason.empty(), message);
    }
    check_source_package_parts_are_file_backed(editor.reader(), output_plan.entries, message);
}

void check_output_entry_relationship_context(
    const std::vector<fastxlsx::detail::PackageEditorOutputEntryPlan>& plans,
    std::string_view entry_name,
    std::string_view owner_part,
    std::string_view relationship_id,
    std::string_view relationship_type,
    std::string_view relationship_target,
    const char* message)
{
    const fastxlsx::detail::PackageEditorOutputEntryPlan* plan =
        find_output_entry_plan(plans, entry_name);
    if (plan == nullptr) {
        throw TestFailure(std::string(message) + ": missing " + std::string(entry_name));
    }
    check(plan->relationship_owner_part == owner_part, message);
    check(plan->relationship_id == relationship_id, message);
    check(plan->relationship_type == relationship_type, message);
    check(plan->relationship_target == relationship_target, message);
}

void check_output_entry_has_inbound_relationship(
    const std::vector<fastxlsx::detail::PackageEditorOutputEntryPlan>& plans,
    std::string_view entry_name,
    std::string_view owner_part,
    std::string_view owner_entry,
    std::string_view relationship_id,
    std::string_view relationship_type,
    std::string_view relationship_target,
    const fastxlsx::detail::PartName& target_part,
    const char* message)
{
    const fastxlsx::detail::PackageEditorOutputEntryPlan* plan =
        find_output_entry_plan(plans, entry_name);
    if (plan == nullptr) {
        throw TestFailure(std::string(message) + ": missing " + std::string(entry_name));
    }
    const auto item = std::find_if(plan->inbound_relationships.begin(),
        plan->inbound_relationships.end(),
        [owner_part, owner_entry, relationship_id, relationship_type, relationship_target,
            &target_part](const fastxlsx::detail::RemovedPartInboundRelationshipAudit& audit) {
            return audit.owner_part == owner_part && audit.owner_entry == owner_entry
                && audit.relationship_id == relationship_id
                && audit.relationship_type == relationship_type
                && audit.relationship_target == relationship_target
                && audit.target_part == target_part;
        });
    check(item != plan->inbound_relationships.end(), message);
}

std::filesystem::path output_path(std::string_view name)
{
    return fastxlsx::test::artifact_path(name);
}

bool is_package_editor_shard(std::string_view shard)
{
    return shard == "all" || shard == "cellstore-core"
        || shard == "cellstore-chunks" || shard == "cellstore-source"
        || shard == "cellstore-failures" || shard == "cellstore-catalog";
}

std::string_view package_editor_shard_from_args(int argc, char* argv[])
{
    if (argc <= 1) {
        return "all";
    }
    if (argc != 2) {
        throw TestFailure("usage: fastxlsx_package_editor_tests [--shard=<name>]");
    }

    std::string_view shard = argv[1];
    constexpr std::string_view prefix = "--shard=";
    if (shard.starts_with(prefix)) {
        shard.remove_prefix(prefix.size());
    }
    if (!is_package_editor_shard(shard)) {
        throw TestFailure("unknown package_editor shard: " + std::string(shard));
    }
    return shard;
}

bool should_run_package_editor_shard(std::string_view selected, std::string_view shard)
{
    return selected == "all" || selected == shard;
}

std::vector<std::filesystem::path> package_editor_temp_files()
{
    std::vector<std::filesystem::path> paths;
    std::error_code error;
    const std::filesystem::path temp_dir = std::filesystem::temp_directory_path(error);
    if (error) {
        return paths;
    }

    std::filesystem::directory_iterator iterator(
        temp_dir, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::directory_iterator end;
    while (!error && iterator != end) {
        const std::filesystem::path path = iterator->path();
        const std::string filename = path.filename().string();
        if (filename.rfind("fastxlsx-package-editor-", 0) == 0
            && path.extension() == ".xml") {
            paths.push_back(path);
        }
        iterator.increment(error);
    }
    return paths;
}

std::vector<std::filesystem::path> package_editor_output_sibling_temp_files(
    const std::filesystem::path& output)
{
    std::vector<std::filesystem::path> paths;
    const std::filesystem::path parent = output.parent_path();
    if (parent.empty()) {
        return paths;
    }

    std::error_code error;
    std::filesystem::directory_iterator iterator(
        parent, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::directory_iterator end;
    const std::string output_prefix = output.filename().string() + ".fastxlsx-output-";
    const std::string backup_prefix = output.filename().string() + ".fastxlsx-backup-";
    while (!error && iterator != end) {
        const std::filesystem::path path = iterator->path();
        const std::string filename = path.filename().string();
        if (filename.rfind(output_prefix, 0) == 0
            || filename.rfind(backup_prefix, 0) == 0) {
            paths.push_back(path);
        }
        iterator.increment(error);
    }
    return paths;
}

bool contains_path(
    const std::vector<std::filesystem::path>& paths, const std::filesystem::path& target)
{
    return std::any_of(paths.begin(), paths.end(),
        [&target](const std::filesystem::path& path) { return path == target; });
}

void check_no_new_package_editor_temp_files(
    const std::vector<std::filesystem::path>& before, const char* message)
{
    const std::vector<std::filesystem::path> after = package_editor_temp_files();
    for (const std::filesystem::path& path : after) {
        if (!contains_path(before, path)) {
            throw TestFailure(std::string(message) + ": " + path.string());
        }
    }
}

void check_no_new_package_editor_output_sibling_temp_files(
    const std::vector<std::filesystem::path>& before,
    const std::filesystem::path& output,
    const char* message)
{
    const std::vector<std::filesystem::path> after =
        package_editor_output_sibling_temp_files(output);
    for (const std::filesystem::path& path : after) {
        if (!contains_path(before, path)) {
            throw TestFailure(std::string(message) + ": " + path.string());
        }
    }
}

void write_binary_file(const std::filesystem::path& path, std::string_view data)
{
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        throw TestFailure("failed to open test file for writing");
    }
    if (!data.empty()) {
        stream.write(data.data(), static_cast<std::streamsize>(data.size()));
    }
    if (!stream) {
        throw TestFailure("failed to write test file");
    }
}

std::string same_size_different_payload(std::string_view original)
{
    const char replacement = original.find_first_not_of('X') == std::string_view::npos
        ? 'Y'
        : 'X';
    return std::string(original.size(), replacement);
}

class ScopedPackageEditorSourceCopyTempFilesHook {
public:
    explicit ScopedPackageEditorSourceCopyTempFilesHook(
        fastxlsx::detail::PackageEditorSourceCopyTempFilesHook hook)
    {
        fastxlsx::detail::testing_set_package_editor_source_copy_temp_files_hook(hook);
    }

    ~ScopedPackageEditorSourceCopyTempFilesHook()
    {
        fastxlsx::detail::testing_set_package_editor_source_copy_temp_files_hook(nullptr);
    }

    ScopedPackageEditorSourceCopyTempFilesHook(
        const ScopedPackageEditorSourceCopyTempFilesHook&) = delete;
    ScopedPackageEditorSourceCopyTempFilesHook& operator=(
        const ScopedPackageEditorSourceCopyTempFilesHook&) = delete;
};

void truncate_package_editor_source_copy_temp_files(
    std::span<const std::filesystem::path> temporary_source_files)
{
    for (const std::filesystem::path& temporary_source_file : temporary_source_files) {
        write_binary_file(temporary_source_file, {});
    }
}

void rewrite_package_editor_source_copy_temp_files_same_size(
    std::span<const std::filesystem::path> temporary_source_files)
{
    for (const std::filesystem::path& temporary_source_file : temporary_source_files) {
        const std::string original = fastxlsx::test::read_file(temporary_source_file);
        if (original.empty()) {
            continue;
        }
        write_binary_file(temporary_source_file, same_size_different_payload(original));
    }
}

void delete_package_editor_source_copy_temp_files(
    std::span<const std::filesystem::path> temporary_source_files)
{
    for (const std::filesystem::path& temporary_source_file : temporary_source_files) {
        std::error_code error;
        std::filesystem::remove(temporary_source_file, error);
        if (error) {
            throw TestFailure("failed to delete source-copy temporary file");
        }
    }
}

fastxlsx::detail::WorksheetInputChunkCallback make_test_chunk_source(
    std::vector<std::string> chunks)
{
    return [chunks = std::move(chunks), index = std::size_t {0}](
               std::string& output_chunk) mutable {
        if (index >= chunks.size()) {
            return false;
        }
        output_chunk = std::move(chunks[index]);
        ++index;
        return true;
    };
}

void replace_worksheet_part_from_single_chunk_source(fastxlsx::detail::PackageEditor& editor,
    fastxlsx::detail::PartName worksheet_part, std::string worksheet_xml,
    const fastxlsx::detail::ReferencePolicy& policy = fastxlsx::detail::ReferencePolicy {},
    std::string reason = {})
{
    editor.replace_worksheet_part_from_chunk_source(
        std::move(worksheet_part),
        make_test_chunk_source({std::move(worksheet_xml)}),
        policy,
        std::move(reason));
}

void replace_worksheet_part_by_name_from_single_chunk_source(fastxlsx::detail::PackageEditor& editor,
    std::string_view sheet_name, std::string worksheet_xml,
    const fastxlsx::detail::ReferencePolicy& policy = fastxlsx::detail::ReferencePolicy {},
    std::string reason = {})
{
    editor.replace_worksheet_part_from_chunk_source_by_name(
        sheet_name,
        make_test_chunk_source({std::move(worksheet_xml)}),
        policy,
        std::move(reason));
}

void replace_worksheet_sheet_data_from_single_chunk_source(fastxlsx::detail::PackageEditor& editor,
    fastxlsx::detail::PartName worksheet_part, std::string sheet_data_xml,
    const fastxlsx::detail::ReferencePolicy& policy = fastxlsx::detail::ReferencePolicy {})
{
    editor.replace_worksheet_sheet_data_from_chunk_source(
        std::move(worksheet_part),
        make_test_chunk_source({std::move(sheet_data_xml)}),
        policy);
}

void replace_worksheet_sheet_data_by_name_from_single_chunk_source(
    fastxlsx::detail::PackageEditor& editor,
    std::string_view sheet_name,
    std::string sheet_data_xml,
    const fastxlsx::detail::ReferencePolicy& policy = fastxlsx::detail::ReferencePolicy {})
{
    editor.replace_worksheet_sheet_data_from_chunk_source_by_name(
        sheet_name,
        make_test_chunk_source({std::move(sheet_data_xml)}),
        policy);
}

void replace_part_with_memory_chunks(fastxlsx::detail::PackageEditor& editor,
    fastxlsx::detail::PartName part_name, std::string chunk_payload,
    std::string reason)
{
    editor.replace_part_chunks(std::move(part_name),
        {fastxlsx::detail::PackageEntryChunk::memory(std::move(chunk_payload))},
        std::move(reason));
}

void append_bounded_xml_comment_padding(
    std::string& xml, std::size_t max_size, char fill)
{
    const std::string large_comment = std::string("<!--")
        + std::string(512U * 1024U, fill) + "-->";
    while (xml.size() + large_comment.size() <= max_size) {
        xml += large_comment;
    }

    const std::string small_comment = std::string("<!--")
        + std::string(32U, fill) + "-->";
    while (xml.size() + small_comment.size() <= max_size) {
        xml += small_comment;
    }
}

void corrupt_first_occurrence(std::string& data, std::string_view needle)
{
    const std::string marker(needle);
    const std::size_t offset = data.find(marker);
    if (offset == std::string::npos) {
        throw TestFailure("test payload marker not found");
    }
    data[offset] = data[offset] == 'X' ? 'Y' : 'X';
}

void expect_replace_failure(fastxlsx::detail::PackageEditor& editor,
    fastxlsx::detail::PartName part_name,
    fastxlsx::detail::PartWriteMode write_mode,
    const char* message)
{
    bool failed = false;
    try {
        editor.replace_part(std::move(part_name), "<ignored/>", write_mode);
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, message);
}

void expect_replace_failure_containing(fastxlsx::detail::PackageEditor& editor,
    fastxlsx::detail::PartName part_name,
    fastxlsx::detail::PartWriteMode write_mode,
    std::string_view expected_error,
    const char* message)
{
    bool failed = false;
    try {
        editor.replace_part(std::move(part_name), "<ignored/>", write_mode);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), expected_error, message);
    }
    check(failed, message);
}

struct SourcePackage {
    std::filesystem::path path;
    std::string content_types;
    std::string package_relationships;
    std::string workbook_relationships;
    std::string workbook;
    std::string core_properties;
    std::string worksheet;
    std::string unknown;
};

struct CalcSourcePackage : SourcePackage {
    std::string calc_chain;
};

struct CommentsSourcePackage : SourcePackage {
    std::string worksheet_relationships;
    std::string comments;
};

struct ThreadedCommentsSourcePackage : CommentsSourcePackage {
    std::string threaded_comments;
    std::string persons;
};

struct PivotSourcePackage : SourcePackage {
    std::string worksheet_relationships;
    std::string pivot_table;
    std::string pivot_table_relationships;
    std::string pivot_cache_definition;
    std::string pivot_cache_definition_relationships;
    std::string pivot_cache_records;
};

struct ExternalLinksSourcePackage : SourcePackage {
    std::string external_link;
    std::string external_link_relationships;
};

struct CustomXmlSourcePackage : SourcePackage {
    std::string custom_xml;
    std::string custom_xml_relationships;
    std::string custom_xml_properties;
};

struct LinkedObjectSourcePackage : CalcSourcePackage {
    std::string worksheet_relationships;
    std::string drawing;
    std::string drawing_relationships;
    std::string chart;
    std::string media;
    std::string table;
    std::string vml_drawing;
    std::string percent_encoded_drawing;
    std::string shared_strings;
    std::string shared_strings_relationships;
    std::string styles;
    std::string vba_project;
    std::string opaque_extension;
    std::string opaque_extension_relationships;
};

SourcePackage write_source_package(std::string_view name)
{
    SourcePackage source;
    source.path = output_path(name);
    source.content_types = R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/docProps/core.xml" ContentType="application/vnd.openxmlformats-package.core-properties+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(</Types>)";
    source.package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/>)"
        R"(</Relationships>)";
    source.workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(</Relationships>)";
    source.workbook = "<workbook><sheets><sheet name=\"Sheet1\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>";
    source.core_properties = "<cp:coreProperties><dc:creator>Original</dc:creator></cp:coreProperties>";
    source.worksheet = "<worksheet><sheetData><row r=\"1\"><c r=\"A1\"><v>1</v></c></row></sheetData></worksheet>";
    source.unknown = std::string("opaque\0bytes", 12);

    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"docProps/core.xml", source.core_properties},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"custom/opaque.bin", source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    return source;
}

SourcePackage write_missing_worksheet_entry_source_package(std::string_view name)
{
    SourcePackage source = write_source_package(name);
    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"docProps/core.xml", source.core_properties},
            {"custom/opaque.bin", source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    return source;
}

CommentsSourcePackage write_comments_source_package(std::string_view name)
{
    CommentsSourcePackage source;
    source.path = output_path(name);
    source.content_types = R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/comments/comment1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.comments+xml"/>)"
        R"(</Types>)";
    source.package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    source.workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(</Relationships>)";
    source.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    source.worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData>)"
        R"(</worksheet>)";
    source.worksheet_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments" Target="../comments/comment1.xml"/>)"
        R"(</Relationships>)";
    source.comments =
        R"(<comments xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<authors><author>FastXLSX</author></authors>)"
        R"(<commentList><comment ref="A1" authorId="0"><text><t>Keep me</t></text></comment></commentList>)"
        R"(</comments>)";
    source.unknown = std::string("comments\0opaque", 15);

    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"xl/worksheets/_rels/sheet1.xml.rels", source.worksheet_relationships},
            {"xl/comments/comment1.xml", source.comments},
            {"custom/opaque.bin", source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    return source;
}

ThreadedCommentsSourcePackage write_threaded_comments_source_package(std::string_view name)
{
    ThreadedCommentsSourcePackage source;
    source.path = output_path(name);
    source.content_types = R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/comments/comment1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.comments+xml"/>)"
        R"(<Override PartName="/xl/threadedComments/threadedComment1.xml" ContentType="application/vnd.ms-excel.threadedcomments+xml"/>)"
        R"(<Override PartName="/xl/persons/person.xml" ContentType="application/vnd.ms-excel.person+xml"/>)"
        R"(</Types>)";
    source.package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    source.workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(<Relationship Id="rIdPerson" Type="http://schemas.microsoft.com/office/2017/10/relationships/person" Target="persons/person.xml"/>)"
        R"(</Relationships>)";
    source.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    source.worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData>)"
        R"(</worksheet>)";
    source.worksheet_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdLegacy" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments" Target="../comments/comment1.xml"/>)"
        R"(<Relationship Id="rIdThreaded" Type="http://schemas.microsoft.com/office/2017/10/relationships/threadedComment" Target="../threadedComments/threadedComment1.xml"/>)"
        R"(</Relationships>)";
    source.comments =
        R"(<comments xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<authors><author>FastXLSX</author></authors>)"
        R"(<commentList><comment ref="A1" authorId="0"><text><t>Keep legacy comment</t></text></comment></commentList>)"
        R"(</comments>)";
    source.threaded_comments =
        R"(<ThreadedComments xmlns="http://schemas.microsoft.com/office/spreadsheetml/2018/threadedcomments">)"
        R"(<threadedComment ref="A1" id="{11111111-1111-1111-1111-111111111111}" personId="{22222222-2222-2222-2222-222222222222}" dT="2026-06-08T00:00:00Z">)"
        R"(<text>Keep threaded comment</text>)"
        R"(</threadedComment>)"
        R"(</ThreadedComments>)";
    source.persons =
        R"(<personList xmlns="http://schemas.microsoft.com/office/spreadsheetml/2018/threadedcomments">)"
        R"(<person displayName="FastXLSX" id="{22222222-2222-2222-2222-222222222222}" providerId="None" userId="fastxlsx@example.invalid"/>)"
        R"(</personList>)";
    source.unknown = std::string("threaded-comments\0opaque", 24);

    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"xl/worksheets/_rels/sheet1.xml.rels", source.worksheet_relationships},
            {"xl/comments/comment1.xml", source.comments},
            {"xl/threadedComments/threadedComment1.xml", source.threaded_comments},
            {"xl/persons/person.xml", source.persons},
            {"custom/opaque.bin", source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    return source;
}

PivotSourcePackage write_pivot_source_package(std::string_view name)
{
    PivotSourcePackage source;
    source.path = output_path(name);
    source.content_types = R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/pivotTables/pivotTable1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.pivotTable+xml"/>)"
        R"(<Override PartName="/xl/pivotCache/pivotCacheDefinition1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.pivotCacheDefinition+xml"/>)"
        R"(<Override PartName="/xl/pivotCache/pivotCacheRecords1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.pivotCacheRecords+xml"/>)"
        R"(</Types>)";
    source.package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    source.workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(<Relationship Id="rIdPivotCache" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotCacheDefinition" Target="pivotCache/pivotCacheDefinition1.xml"/>)"
        R"(</Relationships>)";
    source.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<pivotCaches><pivotCache cacheId="1" r:id="rIdPivotCache"/></pivotCaches>)"
        R"(</workbook>)";
    source.worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>1</v></c><c r="B1"><v>2</v></c></row></sheetData>)"
        R"(<pivotTableDefinition r:id="rIdPivotTable"/>)"
        R"(</worksheet>)";
    source.worksheet_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdPivotTable" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotTable" Target="../pivotTables/pivotTable1.xml"/>)"
        R"(</Relationships>)";
    source.pivot_table =
        R"(<pivotTableDefinition xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" name="PivotTable1" cacheId="1" dataCaption="Values" grandTotalCaption="Grand Total">)"
        R"(<location ref="D3:E8" firstHeaderRow="1" firstDataRow="2" firstDataCol="1"/>)"
        R"(<pivotFields count="1"><pivotField axis="axisRow"/></pivotFields>)"
        R"(</pivotTableDefinition>)";
    source.pivot_table_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdPivotCacheDef" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotCacheDefinition" Target="../pivotCache/pivotCacheDefinition1.xml"/>)"
        R"(</Relationships>)";
    source.pivot_cache_definition =
        R"(<pivotCacheDefinition xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" refreshOnLoad="1" refreshedVersion="8">)"
        R"(<cacheSource type="worksheet"><worksheetSource ref="A1:B3" sheet="Sheet1"/></cacheSource>)"
        R"(<cacheFields count="1"><cacheField name="Value" numFmtId="0"><sharedItems containsNumber="1"/></cacheField></cacheFields>)"
        R"(<cacheRecords r:id="rIdPivotRecords"/>)"
        R"(</pivotCacheDefinition>)";
    source.pivot_cache_definition_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdPivotRecords" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotCacheRecords" Target="pivotCacheRecords1.xml"/>)"
        R"(</Relationships>)";
    source.pivot_cache_records =
        R"(<pivotCacheRecords xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1">)"
        R"(<r><n v="42"/></r>)"
        R"(</pivotCacheRecords>)";
    source.unknown = std::string("pivot\0opaque", 12);

    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"xl/worksheets/_rels/sheet1.xml.rels", source.worksheet_relationships},
            {"xl/pivotTables/pivotTable1.xml", source.pivot_table},
            {"xl/pivotTables/_rels/pivotTable1.xml.rels", source.pivot_table_relationships},
            {"xl/pivotCache/pivotCacheDefinition1.xml", source.pivot_cache_definition},
            {"xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels",
                source.pivot_cache_definition_relationships},
            {"xl/pivotCache/pivotCacheRecords1.xml", source.pivot_cache_records},
            {"custom/opaque.bin", source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    return source;
}

ExternalLinksSourcePackage write_external_links_source_package(std::string_view name)
{
    ExternalLinksSourcePackage source;
    source.path = output_path(name);
    source.content_types = R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/externalLinks/externalLink1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.externalLink+xml"/>)"
        R"(</Types>)";
    source.package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    source.workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(<Relationship Id="rIdExternalLink" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/externalLink" Target="externalLinks/externalLink1.xml"/>)"
        R"(</Relationships>)";
    source.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<externalReferences><externalReference r:id="rIdExternalLink"/></externalReferences>)"
        R"(</workbook>)";
    source.worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData>)"
        R"(</worksheet>)";
    source.external_link =
        R"(<externalLink xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" )"
        R"(xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<externalBook r:id="rIdExternalWorkbook">)"
        R"(<sheetNames><sheetName val="ExternalSheet"/></sheetNames>)"
        R"(</externalBook>)"
        R"(</externalLink>)";
    source.external_link_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdExternalWorkbook" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/externalLinkPath" Target="file:///C:/fastxlsx/source.xlsx" TargetMode="External"/>)"
        R"(</Relationships>)";
    source.unknown = std::string("external-links\0opaque", 22);

    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"xl/externalLinks/externalLink1.xml", source.external_link},
            {"xl/externalLinks/_rels/externalLink1.xml.rels",
                source.external_link_relationships},
            {"custom/opaque.bin", source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    return source;
}

CustomXmlSourcePackage write_custom_xml_source_package(std::string_view name)
{
    CustomXmlSourcePackage source;
    source.path = output_path(name);
    source.content_types = R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/customXml/itemProps1.xml" ContentType="application/vnd.openxmlformats-officedocument.customXmlProperties+xml"/>)"
        R"(</Types>)";
    source.package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(<Relationship Id="rIdCustomXml" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXml" Target="customXml/item1.xml"/>)"
        R"(</Relationships>)";
    source.workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(</Relationships>)";
    source.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    source.worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData>)"
        R"(</worksheet>)";
    source.custom_xml =
        R"(<fx:payload xmlns:fx="urn:fastxlsx:test-custom-xml">)"
        R"(<fx:value>Keep custom XML payload</fx:value>)"
        R"(</fx:payload>)";
    source.custom_xml_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdCustomXmlProps" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXmlProps" Target="itemProps1.xml"/>)"
        R"(</Relationships>)";
    source.custom_xml_properties =
        R"(<ds:datastoreItem ds:itemID="{11111111-2222-3333-4444-555555555555}" xmlns:ds="http://schemas.openxmlformats.org/officeDocument/2006/customXml">)"
        R"(<ds:schemaRefs/>)"
        R"(</ds:datastoreItem>)";
    source.unknown = std::string("custom-xml\0opaque", 18);

    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"customXml/item1.xml", source.custom_xml},
            {"customXml/_rels/item1.xml.rels", source.custom_xml_relationships},
            {"customXml/itemProps1.xml", source.custom_xml_properties},
            {"custom/opaque.bin", source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    return source;
}

SourcePackage write_missing_sheet_data_source_package(std::string_view name)
{
    SourcePackage source = write_source_package(name);
    source.worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<dimension ref="A1"/>)"
        R"(</worksheet>)";

    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"docProps/core.xml", source.core_properties},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"custom/opaque.bin", source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    return source;
}

CalcSourcePackage write_calc_source_package(std::string_view name,
    fastxlsx::detail::PackageWriterBackend backend =
        fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap)
{
    CalcSourcePackage source;
    source.path = output_path(name);
    source.content_types = R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/calcChain.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.calcChain+xml"/>)"
        R"(</Types>)";
    source.package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    source.workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="calcChain.xml"/>)"
        R"(</Relationships>)";
    source.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<calcPr calcId="1" fullCalcOnLoad="0"/>)"
        R"(</workbook>)";
    source.worksheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><f>SUM(B1:C1)</f><v>3</v></c></row></sheetData></worksheet>)";
    source.calc_chain = R"(<calcChain><c r="A1" i="1"/></calcChain>)";
    source.unknown = std::string("opaque\0calc\0bytes", 18);

    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"xl/calcChain.xml", source.calc_chain},
            {"custom/opaque.bin", source.unknown},
        },
        {backend});

    return source;
}

void rewrite_calc_source_package(const CalcSourcePackage& source,
    fastxlsx::detail::PackageWriterBackend backend =
        fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap)
{
    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"xl/calcChain.xml", source.calc_chain},
            {"custom/opaque.bin", source.unknown},
        },
        {backend});
}

LinkedObjectSourcePackage write_linked_object_source_package(std::string_view name,
    fastxlsx::detail::PackageWriterBackend backend =
        fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap)
{
    LinkedObjectSourcePackage source;
    source.path = output_path(name);
    source.content_types = R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="png" ContentType="image/png"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/drawings/drawing1.xml" ContentType="application/vnd.openxmlformats-officedocument.drawing+xml"/>)"
        R"(<Override PartName="/xl/charts/chart1.xml" ContentType="application/vnd.openxmlformats-officedocument.drawingml.chart+xml"/>)"
        R"(<Override PartName="/xl/tables/table1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.table+xml"/>)"
        R"(<Override PartName="/xl/drawings/vmlDrawing1.vml" ContentType="application/vnd.openxmlformats-officedocument.vmlDrawing"/>)"
        R"(<Override PartName="/xl/drawings/drawing space.xml" ContentType="application/vnd.openxmlformats-officedocument.drawing+xml"/>)"
        R"(<Override PartName="/xl/sharedStrings.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml"/>)"
        R"(<Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/>)"
        R"(<Override PartName="/xl/vbaProject.bin" ContentType="application/vnd.ms-office.vbaProject"/>)"
        R"(<Override PartName="/xl/calcChain.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.calcChain+xml"/>)"
        R"(</Types>)";
    source.package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    source.workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/vbaProject" Target="vbaProject.bin"/>)"
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>)"
        R"(<Relationship Id="rId4" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>)"
        R"(<Relationship Id="rId5" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="calcChain.xml"/>)"
        R"(</Relationships>)";
    source.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<definedNames><definedName name="ReportRange">Sheet1!$A$1:$B$2</definedName></definedNames>)"
        R"(</workbook>)";
    source.worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData>)"
        R"(<drawing r:id="rId1"/>)"
        R"(<tableParts count="1"><tablePart r:id="rId3"/></tableParts>)"
        R"(</worksheet>)";
    source.worksheet_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing" Target="../drawings/drawing1.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://example.invalid/link" TargetMode="External"/>)"
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/table" Target="../tables/table1.xml"/>)"
        R"(<Relationship Id="rId4" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments" Target="../comments/comment1.xml"/>)"
        R"(<Relationship Id="rId5" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing" Target="../drawings/drawing1.xml#shape1"/>)"
        R"(<Relationship Id="rId6" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments" Target="../../../outside.bin"/>)"
        R"(<Relationship Id="rId7" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing" Target="../drawings/vmlDrawing1.vml#shape1"/>)"
        R"(<Relationship Id="rId8" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing" Target="../drawings/drawing%20space.xml"/>)"
        R"(<Relationship Id="rId9" Type="https://fastxlsx.invalid/relationships/opaque-extension" Target="../../custom/opaque-extension.bin"/>)"
        R"(</Relationships>)";
    source.drawing =
        R"(<xdr:wsDr xmlns:xdr="http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing" )"
        R"(xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" )"
        R"(xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<xdr:twoCellAnchor><xdr:pic><xdr:blipFill><a:blip r:embed="rId1"/></xdr:blipFill></xdr:pic>)"
        R"(<xdr:graphicFrame><a:graphic><a:graphicData><c:chart xmlns:c="http://schemas.openxmlformats.org/drawingml/2006/chart" r:id="rId2"/></a:graphicData></a:graphic></xdr:graphicFrame>)"
        R"(</xdr:twoCellAnchor></xdr:wsDr>)";
    source.drawing_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="../media/image1.png"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart" Target="../charts/chart1.xml"/>)"
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://drawing.example.invalid/link" TargetMode="External"/>)"
        R"(<Relationship Id="rId4" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart" Target="../charts/chart1.xml#plotArea"/>)"
        R"(<Relationship Id="rId5" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/oleObject" Target="../embeddings/oleObject1.bin"/>)"
        R"(<Relationship Id="rId6" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/oleObject" Target="../../../outside-from-drawing.bin"/>)"
        R"(</Relationships>)";
    source.chart =
        R"(<c:chartSpace xmlns:c="http://schemas.openxmlformats.org/drawingml/2006/chart">)"
        R"(<c:chart><c:title><c:tx><c:rich/></c:tx></c:title></c:chart>)"
        R"(</c:chartSpace>)";
    source.media = std::string("\x89PNG\r\n\x1A\nopaque-image-bytes", 26);
    source.table =
        R"(<table xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" )"
        R"(id="1" name="Table1" displayName="Table1" ref="A1:B2">)"
        R"(<autoFilter ref="A1:B2"/>)"
        R"(<tableColumns count="2"><tableColumn id="1" name="A"/><tableColumn id="2" name="B"/></tableColumns>)"
        R"(</table>)";
    source.vml_drawing = R"(<xml><v:shape id="shape1"/></xml>)";
    source.percent_encoded_drawing =
        R"(<xdr:wsDr xmlns:xdr="http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing"/>)";
    source.shared_strings =
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="2" uniqueCount="2">)"
        R"(<si><t>Alpha</t></si><si><t>Beta</t></si>)"
        R"(</sst>)";
    source.shared_strings_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdSharedExternal" Type="https://fastxlsx.invalid/relationships/sharedStrings-audit" Target="https://example.invalid/sharedStrings-audit" TargetMode="External"/>)"
        R"(</Relationships>)";
    source.styles =
        R"(<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<fonts count="1"><font/></fonts>)"
        R"(<fills count="2"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill></fills>)"
        R"(<borders count="1"><border/></borders>)"
        R"(<cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs>)"
        R"(<cellXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/></cellXfs>)"
        R"(</styleSheet>)";
    source.vba_project = std::string("VBA\0PROJECT\0opaque", 18);
    source.calc_chain = R"(<calcChain><c r="A1" i="1"/></calcChain>)";
    source.opaque_extension = std::string("extension\0payload", 17);
    source.opaque_extension_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdOpaqueExternal" Type="https://fastxlsx.invalid/relationships/opaque-extension-audit" Target="https://example.invalid/opaque-extension-audit" TargetMode="External"/>)"
        R"(</Relationships>)";

    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"xl/worksheets/_rels/sheet1.xml.rels", source.worksheet_relationships},
            {"xl/drawings/drawing1.xml", source.drawing},
            {"xl/drawings/_rels/drawing1.xml.rels", source.drawing_relationships},
            {"xl/charts/chart1.xml", source.chart},
            {"xl/media/image1.png", source.media},
            {"xl/tables/table1.xml", source.table},
            {"xl/drawings/vmlDrawing1.vml", source.vml_drawing},
            {"xl/drawings/drawing space.xml", source.percent_encoded_drawing},
            {"xl/sharedStrings.xml", source.shared_strings},
            {"xl/_rels/sharedStrings.xml.rels", source.shared_strings_relationships},
            {"xl/styles.xml", source.styles},
            {"xl/vbaProject.bin", source.vba_project},
            {"xl/calcChain.xml", source.calc_chain},
            {"custom/opaque-extension.bin", source.opaque_extension},
            {"custom/_rels/opaque-extension.bin.rels", source.opaque_extension_relationships},
        },
        {backend});

    return source;
}

LinkedObjectSourcePackage write_sheet_data_patch_source_package(std::string_view name)
{
    LinkedObjectSourcePackage source = write_linked_object_source_package(name);
    source.worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetPr filterMode="1"/>)"
        R"(<sheetCalcPr fullCalcOnLoad="1"/>)"
        R"(<dimension ref="A1:B2"/>)"
        R"(<sheetViews><sheetView workbookViewId="0"/></sheetViews>)"
        R"(<customSheetViews><customSheetView guid="{11111111-2222-3333-4444-555555555555}"/></customSheetViews>)"
        R"(<sheetFormatPr defaultRowHeight="15"/>)"
        R"(<cols><col min="1" max="1" width="12" customWidth="1"/></cols>)"
        R"(<sheetData><row r="1"><c r="A1"><v>1</v></c><c r="B1" t="s"><v>0</v></c></row></sheetData>)"
        R"(<sheetProtection sheet="1" objects="1" scenarios="1"/>)"
        R"(<protectedRanges><protectedRange name="Inputs" sqref="A1:B2"/></protectedRanges>)"
        R"(<sortState ref="A1:B2"><sortCondition ref="A1:A2"/></sortState>)"
        R"(<autoFilter ref="A1:B2"/>)"
        R"(<mergeCells count="1"><mergeCell ref="A1:B1"/></mergeCells>)"
        R"(<scenarios><scenario name="Base" user="FastXLSX"/></scenarios>)"
        R"(<dataConsolidate function="sum" ref3D="1"><dataRefs count="1"><dataRef ref="A1:B2" sheet="Sheet1"/></dataRefs></dataConsolidate>)"
        R"(<customProperties><customPr name="FastXLSX"/></customProperties>)"
        R"(<cellWatches><cellWatch r="A1"/></cellWatches>)"
        R"(<smartTags><cellSmartTags r="A1"><cellSmartTag type="urn:fastxlsx:smart"/></cellSmartTags></smartTags>)"
        R"(<webPublishItems count="1"><webPublishItem id="1" divId="FastXLSX" sourceType="sheet" sourceRef="Sheet1!A1:B2"/></webPublishItems>)"
        R"(<conditionalFormatting sqref="A1:B2"><cfRule type="expression" priority="1"><formula>$A$1&gt;0</formula></cfRule></conditionalFormatting>)"
        R"(<dataValidations count="1"><dataValidation type="whole" sqref="A1:B2"><formula1>1</formula1></dataValidation></dataValidations>)"
        R"(<hyperlinks><hyperlink ref="A1" r:id="rId2"/></hyperlinks>)"
        R"(<printOptions horizontalCentered="1"/>)"
        R"(<pageMargins left="0.7" right="0.7" top="0.75" bottom="0.75" header="0.3" footer="0.3"/>)"
        R"(<pageSetup orientation="landscape"/>)"
        R"(<headerFooter><oddHeader>&amp;LFastXLSX</oddHeader></headerFooter>)"
        R"(<rowBreaks count="1" manualBreakCount="1"><brk id="2" max="16383" man="1"/></rowBreaks>)"
        R"(<colBreaks count="1" manualBreakCount="1"><brk id="1" max="1048575" man="1"/></colBreaks>)"
        R"(<phoneticPr fontId="1" type="noConversion"/>)"
        R"(<ignoredErrors><ignoredError sqref="A1:B2" numberStoredAsText="1"/></ignoredErrors>)"
        R"(<drawing r:id="rId1"/>)"
        R"(<legacyDrawing r:id="rId7"/>)"
        R"(<picture r:id="rId1"/>)"
        R"(<legacyDrawingHF r:id="rId7"/>)"
        R"(<oleObjects><oleObject progId="Forms.CommandButton.1" r:id="rId1"/></oleObjects>)"
        R"(<controls><control shapeId="1" r:id="rId1"/></controls>)"
        R"(<tableParts count="1"><tablePart r:id="rId3"/></tableParts>)"
        R"(<extLst><ext uri="{fastxlsx-test}"><fx:opaque xmlns:fx="urn:fastxlsx:test"/></ext></extLst>)"
        R"(</worksheet>)";

    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"xl/worksheets/_rels/sheet1.xml.rels", source.worksheet_relationships},
            {"xl/drawings/drawing1.xml", source.drawing},
            {"xl/drawings/_rels/drawing1.xml.rels", source.drawing_relationships},
            {"xl/charts/chart1.xml", source.chart},
            {"xl/media/image1.png", source.media},
            {"xl/tables/table1.xml", source.table},
            {"xl/drawings/vmlDrawing1.vml", source.vml_drawing},
            {"xl/drawings/drawing space.xml", source.percent_encoded_drawing},
            {"xl/sharedStrings.xml", source.shared_strings},
            {"xl/_rels/sharedStrings.xml.rels", source.shared_strings_relationships},
            {"xl/styles.xml", source.styles},
            {"xl/vbaProject.bin", source.vba_project},
            {"xl/calcChain.xml", source.calc_chain},
            {"custom/opaque-extension.bin", source.opaque_extension},
            {"custom/_rels/opaque-extension.bin.rels", source.opaque_extension_relationships},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    return source;
}

void rewrite_linked_object_source_package(const LinkedObjectSourcePackage& source)
{
    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"xl/worksheets/_rels/sheet1.xml.rels", source.worksheet_relationships},
            {"xl/drawings/drawing1.xml", source.drawing},
            {"xl/drawings/_rels/drawing1.xml.rels", source.drawing_relationships},
            {"xl/charts/chart1.xml", source.chart},
            {"xl/media/image1.png", source.media},
            {"xl/tables/table1.xml", source.table},
            {"xl/drawings/vmlDrawing1.vml", source.vml_drawing},
            {"xl/drawings/drawing space.xml", source.percent_encoded_drawing},
            {"xl/sharedStrings.xml", source.shared_strings},
            {"xl/_rels/sharedStrings.xml.rels", source.shared_strings_relationships},
            {"xl/styles.xml", source.styles},
            {"xl/vbaProject.bin", source.vba_project},
            {"xl/calcChain.xml", source.calc_chain},
            {"custom/opaque-extension.bin", source.opaque_extension},
            {"custom/_rels/opaque-extension.bin.rels", source.opaque_extension_relationships},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
}

void test_package_editor_patches_cell_store_sheet_data_by_sheet_name()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-cellstore-sheetdata-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-cellstore-sheetdata-output.xlsx");

    fastxlsx::detail::CellStore store;
    store.set_cell(1, 1, fastxlsx::CellValue::number(42.25));
    store.set_cell(1, 2, fastxlsx::CellValue::text(" <cell & value> "));
    store.set_cell(2, 1, fastxlsx::CellValue::formula("SUM(A1:A1)&\"<done>\""));
    store.set_cell(3, 3, fastxlsx::CellValue::boolean(false));
    store.set_cell(4, 1, fastxlsx::CellValue::blank());
    const fastxlsx::detail::WorksheetInputChunkCallback replacement_sheet_data_source =
        fastxlsx::detail::cell_store_sheet_data_chunk_source(store);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    editor.replace_worksheet_sheet_data_from_chunk_source_by_name(
        "Sheet1", replacement_sheet_data_source);

    using PayloadAuditKind = fastxlsx::detail::WorksheetPayloadDependencyAuditKind;
    using PayloadAuditScope = fastxlsx::detail::WorksheetPayloadDependencyAuditScope;

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "CellStore sheetData handoff should resolve the target worksheet part");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "CellStore sheetData handoff should use bounded local worksheet rewrite");
    check(worksheet_plan->reason.find("bounded local sheetData replacement")
            != std::string::npos,
        "CellStore sheetData handoff should disclose bounded local rewrite reason");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "CellStore sheetData handoff should mirror worksheet rewrite in the manifest");

    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "CellStore sheetData handoff should plan workbook calc metadata rewrite");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "CellStore sheetData handoff should rewrite workbook calc metadata");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "CellStore sheetData handoff should remove stale calcChain by default");
    check(editor.edit_plan().full_calculation_on_load(),
        "CellStore sheetData handoff should request full calculation");
    check(has_note_containing(editor.edit_plan().notes(),
              {"bounded local worksheet XML rewrite", "not the large-file streaming"}),
        "CellStore sheetData handoff should retain bounded local rewrite audit note");
    check(has_note_containing(editor.edit_plan().notes(),
              {"by-name sheetData chunk-source replacement", "without routing through",
                  "materialized sheetData string"}),
        "CellStore sheetData handoff should not route through a materialized sheetData string");
    check(has_payload_audit(editor.edit_plan().worksheet_payload_dependency_audits(),
              worksheet_part, PayloadAuditKind::Formula,
              PayloadAuditScope::SheetDataReplacement, "f",
              {"formulas", "calcChain policy"}),
        "CellStore sheetData handoff should audit formula payload dependencies");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load,
        "CellStore sheetData output plan should expose full calculation request");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "CellStore sheetData output plan should expose workbook metadata rewrite");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "CellStore sheetData output plan should expose worksheet local-DOM rewrite");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "CellStore sheetData output plan should omit stale calcChain");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "CellStore sheetData output plan should preserve unknown bytes");
    check(has_payload_audit(output_plan.worksheet_payload_dependency_audits,
              worksheet_part, PayloadAuditKind::Formula,
              PayloadAuditScope::SheetDataReplacement, "f",
              {"formulas", "calcChain policy"}),
        "CellStore sheetData output plan should keep formula dependency audit");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "CellStore sheetData output should omit stale calcChain");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("Sheet1") == worksheet_part,
        "CellStore sheetData output should keep sheet lookup readable");

    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<sheetData><row r="1"><c r="A1"><v>42.25</v></c>)",
        "CellStore sheetData output should write generated first row payload");
    check_contains(worksheet_xml, R"(</row><row r="2"><c r="A2"><f>)",
        "CellStore sheetData output should group generated sparse rows");
    check_contains(worksheet_xml,
        R"(<t xml:space="preserve"> &lt;cell &amp; value&gt; </t>)",
        "CellStore sheetData output should preserve and escape inline text");
    check_contains(worksheet_xml, R"(<f>SUM(A1:A1)&amp;"&lt;done&gt;"</f>)",
        "CellStore sheetData output should escape formula text");
    check_contains(worksheet_xml, R"(<c r="C3" t="b"><v>0</v></c>)",
        "CellStore sheetData output should write boolean records");
    check_contains(worksheet_xml, R"(<c r="A4"/>)",
        "CellStore sheetData output should write explicit blank records");
    check_not_contains(worksheet_xml, "SUM(B1:C1)",
        "CellStore sheetData output should remove old source formula rows");

    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "CellStore sheetData output should request workbook recalculation");
    check_not_contains(output_reader.read_entry("[Content_Types].xml"), "calcChain+xml",
        "CellStore sheetData output should remove calcChain content type");
    check_not_contains(output_reader.read_entry("xl/_rels/workbook.xml.rels"),
        "relationships/calcChain",
        "CellStore sheetData output should remove calcChain relationship");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "CellStore sheetData output should preserve unknown bytes");
}

void test_package_editor_patches_cell_store_sheet_data_with_writer_style()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-editor-cellstore-styled-sheetdata-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-cellstore-styled-sheetdata-output.xlsx");

    fastxlsx::StyleId text_style;
    {
        auto workbook = fastxlsx::WorkbookWriter::create(source_path);
        text_style = workbook.add_style(fastxlsx::CellStyle {"@"});
        auto sheet = workbook.add_worksheet("Styled");
        sheet.append_row({fastxlsx::CellView::text("old plain")});
        workbook.close();
    }

    const auto source_entries = fastxlsx::test::read_zip_entries(source_path);
    check(source_entries.find("xl/styles.xml") != source_entries.end(),
        "styled CellStore source should contain styles.xml");
    const std::string styles_before = source_entries.at("xl/styles.xml");

    fastxlsx::detail::CellStore store;
    store.set_cell(1, 1, fastxlsx::CellValue::text("styled replacement").with_style(text_style));
    store.set_cell(1, 2,
        fastxlsx::CellValue::text("explicit default").with_style(fastxlsx::StyleId {}));
    const fastxlsx::detail::WorksheetInputChunkCallback replacement_sheet_data_source =
        fastxlsx::detail::cell_store_sheet_data_chunk_source(store);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source_path);
    editor.replace_worksheet_sheet_data_from_chunk_source_by_name(
        "Styled", replacement_sheet_data_source);

    using PayloadAuditKind = fastxlsx::detail::WorksheetPayloadDependencyAuditKind;
    using PayloadAuditScope = fastxlsx::detail::WorksheetPayloadDependencyAuditScope;

    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    check(has_payload_audit(editor.edit_plan().worksheet_payload_dependency_audits(),
              worksheet_part, PayloadAuditKind::Styles,
              PayloadAuditScope::SheetDataReplacement, "c",
              {"style id references", "xl/styles.xml"}),
        "CellStore styled sheetData handoff should audit style id references");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "CellStore styled sheetData output plan should preserve styles");
    check(has_payload_audit(output_plan.worksheet_payload_dependency_audits,
              worksheet_part, PayloadAuditKind::Styles,
              PayloadAuditScope::SheetDataReplacement, "c",
              {"style id references", "xl/styles.xml"}),
        "CellStore styled sheetData output plan should keep style dependency audit");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="A1" s="1" t="inlineStr"><is><t>styled replacement</t></is></c>)",
        "CellStore styled sheetData output should write caller-supplied StyleId");
    check_contains(worksheet_xml,
        R"(<c r="B1" t="inlineStr"><is><t>explicit default</t></is></c>)",
        "CellStore styled sheetData output should omit explicit default StyleId");
    check_not_contains(worksheet_xml, R"(s="0")",
        "CellStore styled sheetData output should not write default style attributes");
    check(output_reader.read_entry("xl/styles.xml") == styles_before,
        "CellStore styled sheetData output should preserve styles.xml bytes");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "CellStore styled sheetData output should preserve styles content type");
}

void test_package_editor_patches_source_loaded_cell_store_sheet_data_by_sheet_name()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-source-cellstore-sheetdata-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-source-cellstore-sheetdata-output.xlsx");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    fastxlsx::detail::CellStore store =
        fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Sheet1");
    check(store.cell_count() == 1,
        "source-backed CellStore handoff should load the source worksheet cells");
    const fastxlsx::detail::CellRecord* source_formula = store.try_cell(1, 1);
    check(source_formula != nullptr && source_formula->kind == fastxlsx::CellValueKind::Formula,
        "source-backed CellStore handoff should load source formula cells");
    check(source_formula->text_value == "SUM(B1:C1)",
        "source-backed CellStore handoff formula payload mismatch");

    store.erase_cell(1, 1);
    store.set_cell(1, 2, fastxlsx::CellValue::text("loaded & patched"));
    store.set_cell(1, 3, fastxlsx::CellValue::blank());
    check(store.try_cell(1, 1) == nullptr,
        "source-backed CellStore mutation should remove erased records");
    check(store.try_cell(1, 3) != nullptr
            && store.try_cell(1, 3)->kind == fastxlsx::CellValueKind::Blank,
        "source-backed CellStore mutation should keep explicit blank records");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::WorksheetInputChunkCallback replacement_sheet_data_source =
        fastxlsx::detail::cell_store_sheet_data_chunk_source(store);
    editor.replace_worksheet_sheet_data_from_chunk_source_by_name(
        "Sheet1", replacement_sheet_data_source);

    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "source-backed CellStore handoff should plan the worksheet rewrite");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "source-backed CellStore handoff should use bounded sheetData rewrite");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "source-backed CellStore handoff should remove stale calcChain by default");
    check(editor.edit_plan().full_calculation_on_load(),
        "source-backed CellStore handoff should request workbook recalculation");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "source-backed CellStore output should omit stale calcChain");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("Sheet1") == worksheet_part,
        "source-backed CellStore output should keep sheet lookup readable");

    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<sheetData><row r="1"><c r="B1" t="inlineStr"><is><t>loaded &amp; patched</t></is></c><c r="C1"/></row></sheetData>)",
        "source-backed CellStore output should write mutated sparse sheetData");
    check_not_contains(worksheet_xml, R"(r="A1")",
        "source-backed CellStore output should omit erased source records");
    check_not_contains(worksheet_xml, "SUM(B1:C1)",
        "source-backed CellStore output should remove old source formula payload");

    check_contains(output_reader.read_entry("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "source-backed CellStore output should request workbook recalculation");
    check_not_contains(output_reader.read_entry("[Content_Types].xml"), "calcChain+xml",
        "source-backed CellStore output should remove calcChain content type");
    check_not_contains(output_reader.read_entry("xl/_rels/workbook.xml.rels"),
        "relationships/calcChain",
        "source-backed CellStore output should remove calcChain relationship");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "source-backed CellStore output should preserve unknown bytes");
}

void test_package_editor_source_loaded_cell_store_uses_planned_name_after_rename()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-source-cellstore-planned-rename-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-source-cellstore-planned-rename-output.xlsx");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    fastxlsx::detail::CellStore store =
        fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Sheet1");
    check(store.try_cell(1, 1) != nullptr
            && store.try_cell(1, 1)->kind == fastxlsx::CellValueKind::Formula,
        "source-backed CellStore planned-name test should load source formula");
    store.set_cell(2, 2, fastxlsx::CellValue::text("planned-name handoff"));

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    editor.rename_sheet_catalog_entry("Sheet1", "Loaded Data");

    const std::size_t renamed_plan_size = editor.edit_plan().size();
    const std::size_t renamed_note_count = editor.edit_plan().notes().size();
    const fastxlsx::detail::CalcChainAction renamed_calc_chain_action =
        editor.edit_plan().calc_chain_action();

    std::size_t sheet_data_chunk_reads = 0;
    fastxlsx::detail::WorksheetInputChunkCallback sheet_data_source =
        fastxlsx::detail::cell_store_sheet_data_chunk_source(store);
    fastxlsx::detail::WorksheetInputChunkCallback counted_sheet_data_source =
        [&sheet_data_source, &sheet_data_chunk_reads](std::string& output_chunk) {
            ++sheet_data_chunk_reads;
            return sheet_data_source(output_chunk);
        };

    bool failed_old_name = false;
    try {
        editor.replace_worksheet_sheet_data_from_chunk_source_by_name(
            "Sheet1", counted_sheet_data_source);
    } catch (const fastxlsx::FastXlsxError& error) {
        failed_old_name = true;
        check_contains(error.what(), "workbook sheet name is not present",
            "source-backed CellStore old-name handoff should use planned catalog");
    }
    check(failed_old_name,
        "PackageEditor should reject source name after queued rename for CellStore handoff");

    check(editor.edit_plan().size() == renamed_plan_size,
        "source-backed CellStore old-name failure should preserve queued rename plan size");
    check(editor.edit_plan().notes().size() == renamed_note_count,
        "source-backed CellStore old-name failure should not append notes");
    check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
        "source-backed CellStore old-name failure should not queue calcChain removal");
    check(!editor.edit_plan().full_calculation_on_load(),
        "source-backed CellStore old-name failure should not request recalculation");
    check(editor.edit_plan().calc_chain_action() == renamed_calc_chain_action,
        "source-backed CellStore old-name failure should not change calcChain policy");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "source-backed CellStore old-name failure should preserve workbook rename rewrite");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-backed CellStore old-name failure should leave worksheet copy-original");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-backed CellStore old-name failure should leave calcChain copy-original");
    check(sheet_data_chunk_reads == 0,
        "source-backed CellStore old-name failure should not consume sheetData chunks");

    editor.replace_worksheet_sheet_data_from_chunk_source_by_name(
        "Loaded Data", counted_sheet_data_source);
    check(sheet_data_chunk_reads > 0,
        "source-backed CellStore planned-name handoff should consume sheetData chunks");

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr
            && worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "source-backed CellStore planned-name handoff should rewrite the worksheet");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "source-backed CellStore planned-name handoff should remove stale calcChain");
    check(editor.edit_plan().full_calculation_on_load(),
        "source-backed CellStore planned-name handoff should request recalculation");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "source-backed CellStore planned-name output should omit stale calcChain");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("Loaded Data") == worksheet_part,
        "source-backed CellStore planned-name output should expose renamed sheet");

    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Loaded Data")",
        "source-backed CellStore planned-name output should preserve renamed catalog");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "source-backed CellStore planned-name output should request workbook recalculation");

    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<f>SUM(B1:C1)</f>)",
        "source-backed CellStore planned-name output should retain loaded source formula");
    check_contains(worksheet_xml,
        R"(<c r="B2" t="inlineStr"><is><t>planned-name handoff</t></is></c>)",
        "source-backed CellStore planned-name output should write the mutated cell");
    check_not_contains(worksheet_xml, "<v>3</v>",
        "source-backed CellStore planned-name output should drop old cached formula value");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "source-backed CellStore planned-name output should preserve unknown bytes");
}

void test_package_editor_source_loaded_cell_store_patches_queued_worksheet_replacement()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-source-cellstore-queued-worksheet-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-source-cellstore-queued-worksheet-output.xlsx");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    fastxlsx::detail::CellStore store =
        fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Sheet1");
    check(store.try_cell(1, 1) != nullptr
            && store.try_cell(1, 1)->kind == fastxlsx::CellValueKind::Formula,
        "source-backed CellStore queued-worksheet test should load source formula");
    store.erase_cell(1, 1);
    store.set_cell(2, 2, fastxlsx::CellValue::text("queued CellStore patch"));
    store.set_cell(4, 1, fastxlsx::CellValue::blank());

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::string queued_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetViews><sheetView workbookViewId="24"/></sheetViews>)"
        R"(<sheetData><row r="9"><c r="C9"><v>999</v></c></row></sheetData>)"
        R"(<autoFilter ref="B2:C4"/>)"
        R"(<extLst><ext uri="{cellstore-queued-wrapper}"/></extLst>)"
        R"(</worksheet>)";
    replace_worksheet_part_by_name_from_single_chunk_source(editor, "Sheet1", queued_worksheet);

    const fastxlsx::detail::WorksheetInputChunkCallback replacement_sheet_data_source =
        fastxlsx::detail::cell_store_sheet_data_chunk_source(store);
    editor.replace_worksheet_sheet_data_from_chunk_source_by_name(
        "Sheet1", replacement_sheet_data_source);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "source-backed CellStore queued worksheet patch should keep worksheet in the edit plan");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "source-backed CellStore queued worksheet patch should finish as local-DOM rewrite");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr
            && workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "source-backed CellStore queued worksheet patch should keep workbook calc rewrite");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "source-backed CellStore queued worksheet patch should keep stale calcChain removed");
    check(editor.edit_plan().full_calculation_on_load(),
        "source-backed CellStore queued worksheet patch should keep full calculation request");
    check(has_note_containing(editor.edit_plan().notes(),
              {"pull-based chunk source", "file-backed staged chunk",
                  "follow-up planned-input transforms"}),
        "source-backed CellStore queued worksheet patch should preserve staged-input evidence");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "view metadata", "caller review"}),
        "source-backed CellStore queued worksheet patch should audit queued view metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "autoFilter metadata", "caller review"}),
        "source-backed CellStore queued worksheet patch should audit queued autoFilter metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "extension metadata", "caller review"}),
        "source-backed CellStore queued worksheet patch should audit queued extension metadata");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<sheetViews><sheetView workbookViewId="24"/></sheetViews>)",
        "source-backed CellStore queued worksheet output should preserve queued sheetViews");
    check_contains(worksheet_xml,
        R"(<sheetData><row r="2"><c r="B2" t="inlineStr"><is><t>queued CellStore patch</t></is></c></row><row r="4"><c r="A4"/></row></sheetData>)",
        "source-backed CellStore queued worksheet output should write CellStore sheetData");
    check_contains(worksheet_xml, R"(<autoFilter ref="B2:C4"/>)",
        "source-backed CellStore queued worksheet output should preserve queued autoFilter");
    check_contains(worksheet_xml, R"(<extLst><ext uri="{cellstore-queued-wrapper}"/></extLst>)",
        "source-backed CellStore queued worksheet output should preserve queued extLst");
    check_not_contains(worksheet_xml, R"(<v>999</v>)",
        "source-backed CellStore queued worksheet output should remove queued old rows");
    check_not_contains(worksheet_xml, "SUM(B1:C1)",
        "source-backed CellStore queued worksheet output should not resurrect erased source formula");
    check(output_reader.find_entry("xl/calcChain.xml") == nullptr,
        "source-backed CellStore queued worksheet output should keep stale calcChain omitted");
    check_contains(output_reader.read_entry("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "source-backed CellStore queued worksheet output should keep workbook recalculation request");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "source-backed CellStore queued worksheet output should preserve unknown bytes");
}

void test_package_editor_source_loaded_cell_store_uses_planned_name_after_queued_rename_and_worksheet()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-source-cellstore-rename-queued-worksheet-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-source-cellstore-rename-queued-worksheet-output.xlsx");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    fastxlsx::detail::CellStore store =
        fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Sheet1");
    store.erase_cell(1, 1);
    store.set_cell(2, 2, fastxlsx::CellValue::text("renamed queued CellStore patch"));
    store.set_cell(3, 3, fastxlsx::CellValue::blank());

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    editor.rename_sheet_catalog_entry("Sheet1", "Renamed Queued");
    const std::string queued_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetViews><sheetView workbookViewId="52"/></sheetViews>)"
        R"(<sheetData><row r="8"><c r="D8"><v>888</v></c></row></sheetData>)"
        R"(<autoFilter ref="B2:D8"/>)"
        R"(<extLst><ext uri="{renamed-queued-wrapper}"/></extLst>)"
        R"(</worksheet>)";
    replace_worksheet_part_by_name_from_single_chunk_source(
        editor, "Renamed Queued", queued_worksheet);

    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    const fastxlsx::detail::CalcChainAction queued_calc_chain_action =
        editor.edit_plan().calc_chain_action();

    std::size_t sheet_data_chunk_reads = 0;
    fastxlsx::detail::WorksheetInputChunkCallback sheet_data_source =
        fastxlsx::detail::cell_store_sheet_data_chunk_source(store);
    fastxlsx::detail::WorksheetInputChunkCallback counted_sheet_data_source =
        [&sheet_data_source, &sheet_data_chunk_reads](std::string& output_chunk) {
            ++sheet_data_chunk_reads;
            return sheet_data_source(output_chunk);
        };

    bool failed_old_name = false;
    try {
        editor.replace_worksheet_sheet_data_from_chunk_source_by_name(
            "Sheet1", counted_sheet_data_source);
    } catch (const fastxlsx::FastXlsxError& error) {
        failed_old_name = true;
        check_contains(error.what(), "workbook sheet name is not present",
            "source-backed CellStore rename+queued worksheet old-name failure should use planned catalog");
    }
    check(failed_old_name,
        "PackageEditor should reject source name after queued rename and worksheet replacement");

    check(editor.edit_plan().size() == queued_plan_size,
        "source-backed CellStore rename+queued worksheet old-name failure should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "source-backed CellStore rename+queued worksheet old-name failure should not append notes");
    check(editor.edit_plan().calc_chain_action() == queued_calc_chain_action,
        "source-backed CellStore rename+queued worksheet old-name failure should preserve calc policy");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "source-backed CellStore rename+queued worksheet old-name failure should keep stale calcChain removed");
    check(editor.edit_plan().full_calculation_on_load(),
        "source-backed CellStore rename+queued worksheet old-name failure should keep recalculation request");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "source-backed CellStore rename+queued worksheet old-name failure should keep workbook rewrite");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "source-backed CellStore rename+queued worksheet old-name failure should keep queued worksheet rewrite");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "source-backed CellStore rename+queued worksheet old-name failure should keep calcChain omitted");
    check(sheet_data_chunk_reads == 0,
        "source-backed CellStore rename+queued worksheet old-name failure should not consume chunks");

    editor.replace_worksheet_sheet_data_from_chunk_source_by_name(
        "Renamed Queued", counted_sheet_data_source);
    check(sheet_data_chunk_reads > 0,
        "source-backed CellStore rename+queued worksheet planned-name handoff should consume chunks");

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr
            && worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "source-backed CellStore rename+queued worksheet planned-name handoff should local-DOM-rewrite worksheet");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "source-backed CellStore rename+queued worksheet planned-name handoff should keep stale calcChain removed");
    check(editor.edit_plan().full_calculation_on_load(),
        "source-backed CellStore rename+queued worksheet planned-name handoff should keep recalculation request");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("Renamed Queued") == worksheet_part,
        "source-backed CellStore rename+queued worksheet output should expose planned sheet name");
    bool old_name_failed = false;
    try {
        (void)output_reader.worksheet_part_by_sheet_name("Sheet1");
    } catch (const std::exception&) {
        old_name_failed = true;
    }
    check(old_name_failed,
        "source-backed CellStore rename+queued worksheet output should not expose old sheet name");

    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Renamed Queued")",
        "source-backed CellStore rename+queued worksheet output should preserve renamed catalog");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "source-backed CellStore rename+queued worksheet output should keep recalculation request");

    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<sheetViews><sheetView workbookViewId="52"/></sheetViews>)",
        "source-backed CellStore rename+queued worksheet output should preserve queued sheetViews");
    check_contains(worksheet_xml,
        R"(<sheetData><row r="2"><c r="B2" t="inlineStr"><is><t>renamed queued CellStore patch</t></is></c></row><row r="3"><c r="C3"/></row></sheetData>)",
        "source-backed CellStore rename+queued worksheet output should write planned-name CellStore sheetData");
    check_contains(worksheet_xml, R"(<autoFilter ref="B2:D8"/>)",
        "source-backed CellStore rename+queued worksheet output should preserve queued autoFilter");
    check_contains(worksheet_xml, R"(<extLst><ext uri="{renamed-queued-wrapper}"/></extLst>)",
        "source-backed CellStore rename+queued worksheet output should preserve queued extLst");
    check_not_contains(worksheet_xml, R"(<v>888</v>)",
        "source-backed CellStore rename+queued worksheet output should remove queued old rows");
    check_not_contains(worksheet_xml, "SUM(B1:C1)",
        "source-backed CellStore rename+queued worksheet output should not resurrect erased source formula");
    check(output_reader.find_entry("xl/calcChain.xml") == nullptr,
        "source-backed CellStore rename+queued worksheet output should keep stale calcChain omitted");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "source-backed CellStore rename+queued worksheet output should preserve unknown bytes");
}

void test_package_editor_source_loaded_cell_store_worksheet_chunks_use_planned_name_after_queued_rename_and_worksheet()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-source-cellstore-rename-queued-worksheet-chunks-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-source-cellstore-rename-queued-worksheet-chunks-output.xlsx");
    const std::filesystem::path second_output =
        output_path("fastxlsx-package-editor-source-cellstore-rename-queued-worksheet-chunks-output-2.xlsx");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    fastxlsx::detail::CellStore store =
        fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Sheet1");
    store.set_cell(6, 6, fastxlsx::CellValue::text("renamed queued full projection"));
    store.set_cell(7, 7, fastxlsx::CellValue::blank());

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    editor.rename_sheet_catalog_entry("Sheet1", "Renamed Projection");
    const std::string queued_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetViews><sheetView workbookViewId="53"/></sheetViews>)"
        R"(<sheetData><row r="9"><c r="E9"><v>999</v></c></row></sheetData>)"
        R"(<autoFilter ref="E9:F9"/>)"
        R"(<extLst><ext uri="{renamed-queued-before-full-projection}"/></extLst>)"
        R"(</worksheet>)";
    replace_worksheet_part_by_name_from_single_chunk_source(
        editor, "Renamed Projection", queued_worksheet);

    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    const fastxlsx::detail::CalcChainAction queued_calc_chain_action =
        editor.edit_plan().calc_chain_action();

    std::size_t worksheet_chunk_reads = 0;
    fastxlsx::detail::WorksheetInputChunkCallback worksheet_source =
        fastxlsx::detail::cell_store_worksheet_chunk_source(store);
    fastxlsx::detail::WorksheetInputChunkCallback counted_worksheet_source =
        [&worksheet_source, &worksheet_chunk_reads](std::string& output_chunk) {
            ++worksheet_chunk_reads;
            return worksheet_source(output_chunk);
        };

    bool failed_old_name = false;
    try {
        editor.replace_worksheet_part_from_chunk_source_by_name(
            "Sheet1", counted_worksheet_source);
    } catch (const fastxlsx::FastXlsxError& error) {
        failed_old_name = true;
        check_contains(error.what(), "workbook sheet name is not present",
            "source-backed CellStore rename+queued worksheet full-projection old-name failure should use planned catalog");
    }
    check(failed_old_name,
        "PackageEditor should reject source name before full projection after queued rename and worksheet replacement");

    check(editor.edit_plan().size() == queued_plan_size,
        "source-backed CellStore rename+queued worksheet full-projection old-name failure should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "source-backed CellStore rename+queued worksheet full-projection old-name failure should not append notes");
    check(editor.edit_plan().calc_chain_action() == queued_calc_chain_action,
        "source-backed CellStore rename+queued worksheet full-projection old-name failure should preserve calc policy");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "source-backed CellStore rename+queued worksheet full-projection old-name failure should keep stale calcChain removed");
    check(editor.edit_plan().full_calculation_on_load(),
        "source-backed CellStore rename+queued worksheet full-projection old-name failure should keep recalculation request");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "source-backed CellStore rename+queued worksheet full-projection old-name failure should keep workbook rewrite");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "source-backed CellStore rename+queued worksheet full-projection old-name failure should keep queued worksheet rewrite");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "source-backed CellStore rename+queued worksheet full-projection old-name failure should keep calcChain omitted");
    check(worksheet_chunk_reads == 0,
        "source-backed CellStore rename+queued worksheet full-projection old-name failure should not consume chunks");

    editor.replace_worksheet_part_from_chunk_source_by_name(
        "Renamed Projection", counted_worksheet_source);
    check(worksheet_chunk_reads > 0,
        "source-backed CellStore rename+queued worksheet full-projection planned-name handoff should consume chunks");

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr
            && worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "source-backed CellStore rename+queued worksheet full-projection planned-name handoff should stage stream rewrite");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "source-backed CellStore rename+queued worksheet full-projection planned-name handoff should keep stale calcChain removed");
    check(editor.edit_plan().full_calculation_on_load(),
        "source-backed CellStore rename+queued worksheet full-projection planned-name handoff should keep recalculation request");
    check(has_note_containing(editor.edit_plan().notes(),
              {"pull-based chunk source", "file-backed staged chunk"}),
        "source-backed CellStore rename+queued worksheet full-projection should expose staged chunk-source evidence");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, worksheet_part.zip_path(),
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "source-backed CellStore rename+queued worksheet full-projection output plan should stream-rewrite worksheet");
    check_output_entry_staged_replacement_chunks(output_plan.entries,
        worksheet_part.zip_path(), true,
        "source-backed CellStore rename+queued worksheet full-projection output plan should expose staged chunks");

    const std::size_t planned_plan_size = editor.edit_plan().size();
    const std::size_t planned_note_count = editor.edit_plan().notes().size();
    const std::size_t planned_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t planned_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const fastxlsx::detail::CalcChainAction planned_calc_chain_action =
        editor.edit_plan().calc_chain_action();

    bool failed_source_overwrite = false;
    try {
        editor.save_as(source.path);
    } catch (const fastxlsx::FastXlsxError& error) {
        failed_source_overwrite = true;
        check_contains(error.what(), "cannot save over the source package",
            "source-backed CellStore rename+queued worksheet full-projection source-overwrite failure should explain the output path guard");
    }
    check(failed_source_overwrite,
        "source-backed CellStore rename+queued worksheet full-projection should reject saving over the source package");

    check(editor.edit_plan().size() == planned_plan_size,
        "source-backed CellStore rename+queued worksheet full-projection source-overwrite failure should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == planned_note_count,
        "source-backed CellStore rename+queued worksheet full-projection source-overwrite failure should not append notes");
    check(editor.edit_plan().package_entries().size() == planned_package_entry_count,
        "source-backed CellStore rename+queued worksheet full-projection source-overwrite failure should preserve package-entry audits");
    check(editor.edit_plan().removed_parts().size() == planned_removed_part_count,
        "source-backed CellStore rename+queued worksheet full-projection source-overwrite failure should preserve removed-part audits");
    check(editor.edit_plan().calc_chain_action() == planned_calc_chain_action,
        "source-backed CellStore rename+queued worksheet full-projection source-overwrite failure should preserve calc policy");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "source-backed CellStore rename+queued worksheet full-projection source-overwrite failure should keep workbook rewrite");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "source-backed CellStore rename+queued worksheet full-projection source-overwrite failure should keep worksheet staged rewrite");
    const fastxlsx::detail::PackageEditorOutputPlan after_failed_save_output_plan =
        editor.planned_output();
    check_output_entry_staged_replacement_chunks(after_failed_save_output_plan.entries,
        worksheet_part.zip_path(), true,
        "source-backed CellStore rename+queued worksheet full-projection source-overwrite failure should preserve staged chunks");

    const std::filesystem::path equivalent_source_path =
        source.path.parent_path() / "." / source.path.filename();
    bool failed_equivalent_source_overwrite = false;
    try {
        editor.save_as(equivalent_source_path);
    } catch (const fastxlsx::FastXlsxError& error) {
        failed_equivalent_source_overwrite = true;
        check_contains(error.what(), "cannot save over the source package",
            "source-backed CellStore rename+queued worksheet full-projection path-equivalent source-overwrite failure should explain the output path guard");
    }
    check(failed_equivalent_source_overwrite,
        "source-backed CellStore rename+queued worksheet full-projection should reject path-equivalent source overwrite");

    check(editor.edit_plan().size() == planned_plan_size,
        "source-backed CellStore rename+queued worksheet full-projection path-equivalent source-overwrite failure should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == planned_note_count,
        "source-backed CellStore rename+queued worksheet full-projection path-equivalent source-overwrite failure should not append notes");
    check(editor.edit_plan().calc_chain_action() == planned_calc_chain_action,
        "source-backed CellStore rename+queued worksheet full-projection path-equivalent source-overwrite failure should preserve calc policy");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "source-backed CellStore rename+queued worksheet full-projection path-equivalent source-overwrite failure should keep worksheet staged rewrite");
    const fastxlsx::detail::PackageEditorOutputPlan after_equivalent_failed_save_output_plan =
        editor.planned_output();
    check_output_entry_staged_replacement_chunks(after_equivalent_failed_save_output_plan.entries,
        worksheet_part.zip_path(), true,
        "source-backed CellStore rename+queued worksheet full-projection path-equivalent source-overwrite failure should preserve staged chunks");

    bool failed_empty_output_path = false;
    try {
        editor.save_as(std::filesystem::path());
    } catch (const fastxlsx::FastXlsxError& error) {
        failed_empty_output_path = true;
        check_contains(error.what(), "output path cannot be empty",
            "source-backed CellStore rename+queued worksheet full-projection empty-output failure should explain the output path guard");
    }
    check(failed_empty_output_path,
        "source-backed CellStore rename+queued worksheet full-projection should reject empty output path");

    check(editor.edit_plan().size() == planned_plan_size,
        "source-backed CellStore rename+queued worksheet full-projection empty-output failure should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == planned_note_count,
        "source-backed CellStore rename+queued worksheet full-projection empty-output failure should not append notes");
    check(editor.edit_plan().calc_chain_action() == planned_calc_chain_action,
        "source-backed CellStore rename+queued worksheet full-projection empty-output failure should preserve calc policy");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "source-backed CellStore rename+queued worksheet full-projection empty-output failure should keep worksheet staged rewrite");
    const fastxlsx::detail::PackageEditorOutputPlan after_empty_failed_save_output_plan =
        editor.planned_output();
    check_output_entry_staged_replacement_chunks(after_empty_failed_save_output_plan.entries,
        worksheet_part.zip_path(), true,
        "source-backed CellStore rename+queued worksheet full-projection empty-output failure should preserve staged chunks");

    const std::filesystem::path missing_parent_output =
        output_path("fastxlsx-package-editor-source-cellstore-combined-missing-parent-output") / "out.xlsx";
    bool failed_missing_parent_output = false;
    try {
        editor.save_as(missing_parent_output);
    } catch (const fastxlsx::FastXlsxError& error) {
        failed_missing_parent_output = true;
        check_contains(error.what(), "output parent path is not an existing directory",
            "source-backed CellStore rename+queued worksheet full-projection missing-parent failure should explain the output path guard");
    }
    check(failed_missing_parent_output,
        "source-backed CellStore rename+queued worksheet full-projection should reject missing output parent");

    check(editor.edit_plan().size() == planned_plan_size,
        "source-backed CellStore rename+queued worksheet full-projection missing-parent failure should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == planned_note_count,
        "source-backed CellStore rename+queued worksheet full-projection missing-parent failure should not append notes");
    check(editor.edit_plan().calc_chain_action() == planned_calc_chain_action,
        "source-backed CellStore rename+queued worksheet full-projection missing-parent failure should preserve calc policy");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "source-backed CellStore rename+queued worksheet full-projection missing-parent failure should keep worksheet staged rewrite");
    const fastxlsx::detail::PackageEditorOutputPlan after_missing_parent_failed_save_output_plan =
        editor.planned_output();
    check_output_entry_staged_replacement_chunks(after_missing_parent_failed_save_output_plan.entries,
        worksheet_part.zip_path(), true,
        "source-backed CellStore rename+queued worksheet full-projection missing-parent failure should preserve staged chunks");

    const std::filesystem::path parent_file_output =
        output_path("fastxlsx-package-editor-source-cellstore-combined-parent-file-output");
    {
        std::ofstream parent_file(parent_file_output, std::ios::binary);
        parent_file << "not a directory";
        check(parent_file.good(),
            "source-backed CellStore save-as guard test should create non-directory output parent");
    }
    bool failed_non_directory_parent_output = false;
    try {
        editor.save_as(parent_file_output / "out.xlsx");
    } catch (const fastxlsx::FastXlsxError& error) {
        failed_non_directory_parent_output = true;
        check_contains(error.what(), "output parent path is not an existing directory",
            "source-backed CellStore rename+queued worksheet full-projection non-directory-parent failure should explain the output path guard");
    }
    check(failed_non_directory_parent_output,
        "source-backed CellStore rename+queued worksheet full-projection should reject non-directory output parent");

    check(editor.edit_plan().size() == planned_plan_size,
        "source-backed CellStore rename+queued worksheet full-projection non-directory-parent failure should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == planned_note_count,
        "source-backed CellStore rename+queued worksheet full-projection non-directory-parent failure should not append notes");
    check(editor.edit_plan().calc_chain_action() == planned_calc_chain_action,
        "source-backed CellStore rename+queued worksheet full-projection non-directory-parent failure should preserve calc policy");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "source-backed CellStore rename+queued worksheet full-projection non-directory-parent failure should keep worksheet staged rewrite");
    const fastxlsx::detail::PackageEditorOutputPlan after_non_directory_parent_failed_save_output_plan =
        editor.planned_output();
    check_output_entry_staged_replacement_chunks(after_non_directory_parent_failed_save_output_plan.entries,
        worksheet_part.zip_path(), true,
        "source-backed CellStore rename+queued worksheet full-projection non-directory-parent failure should preserve staged chunks");

    const std::filesystem::path existing_directory_output =
        output_path("fastxlsx-package-editor-source-cellstore-combined-existing-directory-output");
    std::error_code create_directory_error;
    const bool created_directory =
        std::filesystem::create_directories(existing_directory_output, create_directory_error);
    check(!create_directory_error
            && (created_directory || std::filesystem::is_directory(existing_directory_output)),
        "source-backed CellStore save-as guard test should create existing output directory");

    bool failed_existing_directory_output = false;
    try {
        editor.save_as(existing_directory_output);
    } catch (const fastxlsx::FastXlsxError& error) {
        failed_existing_directory_output = true;
        check_contains(error.what(), "output path is an existing directory",
            "source-backed CellStore rename+queued worksheet full-projection existing-directory failure should explain the output path guard");
    }
    check(failed_existing_directory_output,
        "source-backed CellStore rename+queued worksheet full-projection should reject existing directory output path");

    check(editor.edit_plan().size() == planned_plan_size,
        "source-backed CellStore rename+queued worksheet full-projection existing-directory failure should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == planned_note_count,
        "source-backed CellStore rename+queued worksheet full-projection existing-directory failure should not append notes");
    check(editor.edit_plan().calc_chain_action() == planned_calc_chain_action,
        "source-backed CellStore rename+queued worksheet full-projection existing-directory failure should preserve calc policy");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "source-backed CellStore rename+queued worksheet full-projection existing-directory failure should keep worksheet staged rewrite");
    const fastxlsx::detail::PackageEditorOutputPlan after_existing_directory_failed_save_output_plan =
        editor.planned_output();
    check_output_entry_staged_replacement_chunks(after_existing_directory_failed_save_output_plan.entries,
        worksheet_part.zip_path(), true,
        "source-backed CellStore rename+queued worksheet full-projection existing-directory failure should preserve staged chunks");

    const std::vector<std::filesystem::path> temp_files_before_source_copy_failure =
        package_editor_temp_files();
    const std::string source_copy_failure_output_sentinel =
        "do not overwrite this combined CellStore source-copy temp failure output";
    write_binary_file(output, source_copy_failure_output_sentinel);
    const std::vector<std::filesystem::path> output_temp_files_before_source_copy_failure =
        package_editor_output_sibling_temp_files(output);

    bool failed_source_copy_temp_size = false;
    {
        ScopedPackageEditorSourceCopyTempFilesHook hook(
            truncate_package_editor_source_copy_temp_files);
        try {
            editor.save_as(output);
        } catch (const fastxlsx::FastXlsxError& error) {
            failed_source_copy_temp_size = true;
            check_contains(error.what(), "failed to write PackageEditor output package",
                "source-backed CellStore rename+queued worksheet full-projection source-copy temp failure should include PackageEditor context");
            check_contains(error.what(), "ZIP entry chunk size changed after staging",
                "source-backed CellStore rename+queued worksheet full-projection source-copy temp failure should enforce size metadata");
        }
    }
    check(failed_source_copy_temp_size,
        "source-backed CellStore rename+queued worksheet full-projection should reject changed source-copy temp size");

    check(editor.edit_plan().size() == planned_plan_size,
        "source-backed CellStore rename+queued worksheet full-projection source-copy temp failure should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == planned_note_count,
        "source-backed CellStore rename+queued worksheet full-projection source-copy temp failure should not append notes");
    check(editor.edit_plan().calc_chain_action() == planned_calc_chain_action,
        "source-backed CellStore rename+queued worksheet full-projection source-copy temp failure should preserve calc policy");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "source-backed CellStore rename+queued worksheet full-projection source-copy temp failure should keep worksheet staged rewrite");
    const fastxlsx::detail::PackageEditorOutputPlan after_source_copy_failed_save_output_plan =
        editor.planned_output();
    check_output_entry_staged_replacement_chunks(after_source_copy_failed_save_output_plan.entries,
        worksheet_part.zip_path(), true,
        "source-backed CellStore rename+queued worksheet full-projection source-copy temp failure should preserve staged chunks");
    check(fastxlsx::test::read_file(output) == source_copy_failure_output_sentinel,
        "source-backed CellStore rename+queued worksheet full-projection source-copy temp failure should not overwrite existing output");
    check_no_new_package_editor_temp_files(temp_files_before_source_copy_failure,
        "source-backed CellStore rename+queued worksheet full-projection source-copy temp failure should clean source-copy temp files");
    check_no_new_package_editor_output_sibling_temp_files(
        output_temp_files_before_source_copy_failure, output,
        "source-backed CellStore rename+queued worksheet full-projection source-copy temp failure should clean output sibling temp files");

    const std::vector<std::filesystem::path> temp_files_before_missing_source_copy =
        package_editor_temp_files();
    const std::string missing_source_copy_output_sentinel =
        "do not overwrite this combined CellStore missing source-copy temp failure output";
    write_binary_file(output, missing_source_copy_output_sentinel);
    const std::vector<std::filesystem::path> output_temp_files_before_missing_source_copy =
        package_editor_output_sibling_temp_files(output);

    bool failed_missing_source_copy_temp = false;
    {
        ScopedPackageEditorSourceCopyTempFilesHook hook(
            delete_package_editor_source_copy_temp_files);
        try {
            editor.save_as(output);
        } catch (const fastxlsx::FastXlsxError& error) {
            failed_missing_source_copy_temp = true;
            check_contains(error.what(), "failed to write PackageEditor output package",
                "source-backed CellStore rename+queued worksheet full-projection missing source-copy temp failure should include PackageEditor context");
            check_contains(error.what(), "failed to stat file-backed ZIP entry chunk",
                "source-backed CellStore rename+queued worksheet full-projection missing source-copy temp failure should report stat failure");
        }
    }
    check(failed_missing_source_copy_temp,
        "source-backed CellStore rename+queued worksheet full-projection should reject missing source-copy temp file");

    check(editor.edit_plan().size() == planned_plan_size,
        "source-backed CellStore rename+queued worksheet full-projection missing source-copy temp failure should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == planned_note_count,
        "source-backed CellStore rename+queued worksheet full-projection missing source-copy temp failure should not append notes");
    check(editor.edit_plan().calc_chain_action() == planned_calc_chain_action,
        "source-backed CellStore rename+queued worksheet full-projection missing source-copy temp failure should preserve calc policy");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "source-backed CellStore rename+queued worksheet full-projection missing source-copy temp failure should keep worksheet staged rewrite");
    const fastxlsx::detail::PackageEditorOutputPlan after_missing_source_copy_failed_save_output_plan =
        editor.planned_output();
    check_output_entry_staged_replacement_chunks(
        after_missing_source_copy_failed_save_output_plan.entries,
        worksheet_part.zip_path(), true,
        "source-backed CellStore rename+queued worksheet full-projection missing source-copy temp failure should preserve staged chunks");
    check(fastxlsx::test::read_file(output) == missing_source_copy_output_sentinel,
        "source-backed CellStore rename+queued worksheet full-projection missing source-copy temp failure should not overwrite existing output");
    check_no_new_package_editor_temp_files(temp_files_before_missing_source_copy,
        "source-backed CellStore rename+queued worksheet full-projection missing source-copy temp failure should clean source-copy temp files");
    check_no_new_package_editor_output_sibling_temp_files(
        output_temp_files_before_missing_source_copy, output,
        "source-backed CellStore rename+queued worksheet full-projection missing source-copy temp failure should clean output sibling temp files");

    const std::vector<std::filesystem::path> temp_files_before_source_copy_crc_failure =
        package_editor_temp_files();
    const std::string source_copy_crc_failure_output_sentinel =
        "do not overwrite this combined CellStore source-copy CRC failure output";
    write_binary_file(output, source_copy_crc_failure_output_sentinel);
    const std::vector<std::filesystem::path> output_temp_files_before_source_copy_crc_failure =
        package_editor_output_sibling_temp_files(output);

    bool failed_source_copy_temp_crc = false;
    {
        ScopedPackageEditorSourceCopyTempFilesHook hook(
            rewrite_package_editor_source_copy_temp_files_same_size);
        try {
            editor.save_as(output);
        } catch (const fastxlsx::FastXlsxError& error) {
            failed_source_copy_temp_crc = true;
            check_contains(error.what(), "failed to write PackageEditor output package",
                "source-backed CellStore rename+queued worksheet full-projection source-copy temp CRC failure should include PackageEditor context");
            check_contains(error.what(), "ZIP entry chunk CRC32 changed after staging",
                "source-backed CellStore rename+queued worksheet full-projection source-copy temp CRC failure should enforce CRC metadata");
        }
    }
    check(failed_source_copy_temp_crc,
        "source-backed CellStore rename+queued worksheet full-projection should reject changed source-copy temp CRC");

    check(editor.edit_plan().size() == planned_plan_size,
        "source-backed CellStore rename+queued worksheet full-projection source-copy temp CRC failure should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == planned_note_count,
        "source-backed CellStore rename+queued worksheet full-projection source-copy temp CRC failure should not append notes");
    check(editor.edit_plan().calc_chain_action() == planned_calc_chain_action,
        "source-backed CellStore rename+queued worksheet full-projection source-copy temp CRC failure should preserve calc policy");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "source-backed CellStore rename+queued worksheet full-projection source-copy temp CRC failure should keep worksheet staged rewrite");
    const fastxlsx::detail::PackageEditorOutputPlan after_source_copy_crc_failed_save_output_plan =
        editor.planned_output();
    check_output_entry_staged_replacement_chunks(
        after_source_copy_crc_failed_save_output_plan.entries,
        worksheet_part.zip_path(), true,
        "source-backed CellStore rename+queued worksheet full-projection source-copy temp CRC failure should preserve staged chunks");
    check(fastxlsx::test::read_file(output) == source_copy_crc_failure_output_sentinel,
        "source-backed CellStore rename+queued worksheet full-projection source-copy temp CRC failure should not overwrite existing output");
    check_no_new_package_editor_temp_files(temp_files_before_source_copy_crc_failure,
        "source-backed CellStore rename+queued worksheet full-projection source-copy temp CRC failure should clean source-copy temp files");
    check_no_new_package_editor_output_sibling_temp_files(
        output_temp_files_before_source_copy_crc_failure, output,
        "source-backed CellStore rename+queued worksheet full-projection source-copy temp CRC failure should clean output sibling temp files");

    const std::vector<std::filesystem::path> temp_files_before_writer_failure =
        package_editor_temp_files();
    const std::string writer_failure_output_sentinel =
        "do not overwrite this combined CellStore writer failure output";
    write_binary_file(output, writer_failure_output_sentinel);
    const std::vector<std::filesystem::path> output_temp_files_before_writer_failure =
        package_editor_output_sibling_temp_files(output);

    fastxlsx::detail::PackageWriterOptions writer_failure_options;
    writer_failure_options.backend =
        static_cast<fastxlsx::detail::PackageWriterBackend>(999);

    bool failed_writer_output = false;
    try {
        editor.save_as(output, writer_failure_options);
    } catch (const fastxlsx::FastXlsxError& error) {
        failed_writer_output = true;
        check_contains(error.what(), "failed to write PackageEditor output package",
            "source-backed CellStore rename+queued worksheet full-projection writer failure should include PackageEditor context");
        check_contains(error.what(), "unsupported package writer backend",
            "source-backed CellStore rename+queued worksheet full-projection writer failure should preserve backend reason");
    }
    check(failed_writer_output,
        "source-backed CellStore rename+queued worksheet full-projection should reject writer backend failure");

    check(editor.edit_plan().size() == planned_plan_size,
        "source-backed CellStore rename+queued worksheet full-projection writer failure should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == planned_note_count,
        "source-backed CellStore rename+queued worksheet full-projection writer failure should not append notes");
    check(editor.edit_plan().calc_chain_action() == planned_calc_chain_action,
        "source-backed CellStore rename+queued worksheet full-projection writer failure should preserve calc policy");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "source-backed CellStore rename+queued worksheet full-projection writer failure should keep worksheet staged rewrite");
    const fastxlsx::detail::PackageEditorOutputPlan after_writer_failed_save_output_plan =
        editor.planned_output();
    check_output_entry_staged_replacement_chunks(after_writer_failed_save_output_plan.entries,
        worksheet_part.zip_path(), true,
        "source-backed CellStore rename+queued worksheet full-projection writer failure should preserve staged chunks");
    check(fastxlsx::test::read_file(output) == writer_failure_output_sentinel,
        "source-backed CellStore rename+queued worksheet full-projection writer failure should not overwrite existing output");
    check_no_new_package_editor_temp_files(temp_files_before_writer_failure,
        "source-backed CellStore rename+queued worksheet full-projection writer failure should clean source-copy temp files");
    check_no_new_package_editor_output_sibling_temp_files(
        output_temp_files_before_writer_failure, output,
        "source-backed CellStore rename+queued worksheet full-projection writer failure should clean output sibling temp files");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("Renamed Projection") == worksheet_part,
        "source-backed CellStore rename+queued worksheet full-projection output should expose planned sheet name");
    bool old_name_failed = false;
    try {
        (void)output_reader.worksheet_part_by_sheet_name("Sheet1");
    } catch (const std::exception&) {
        old_name_failed = true;
    }
    check(old_name_failed,
        "source-backed CellStore rename+queued worksheet full-projection output should not expose old sheet name");

    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Renamed Projection")",
        "source-backed CellStore rename+queued worksheet full-projection output should preserve renamed catalog");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "source-backed CellStore rename+queued worksheet full-projection output should keep recalculation request");

    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:G7"/>)",
        "source-backed CellStore rename+queued worksheet full-projection output should refresh dimension");
    check_contains(worksheet_xml, R"(<f>SUM(B1:C1)</f>)",
        "source-backed CellStore rename+queued worksheet full-projection output should retain loaded source formula");
    check_contains(worksheet_xml,
        R"(<c r="F6" t="inlineStr"><is><t>renamed queued full projection</t></is></c>)",
        "source-backed CellStore rename+queued worksheet full-projection output should write new text cell");
    check_contains(worksheet_xml, R"(<c r="G7"/>)",
        "source-backed CellStore rename+queued worksheet full-projection output should write explicit blank extent");
    check_not_contains(worksheet_xml, R"(<sheetView workbookViewId="53"/>)",
        "source-backed CellStore rename+queued worksheet full-projection output should not preserve queued sheetViews");
    check_not_contains(worksheet_xml, R"(<autoFilter ref="E9:F9"/>)",
        "source-backed CellStore rename+queued worksheet full-projection output should not preserve queued autoFilter");
    check_not_contains(worksheet_xml, R"(<v>999</v>)",
        "source-backed CellStore rename+queued worksheet full-projection output should remove queued rows");
    check(output_reader.find_entry("xl/calcChain.xml") == nullptr,
        "source-backed CellStore rename+queued worksheet full-projection output should keep stale calcChain omitted");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "source-backed CellStore rename+queued worksheet full-projection output should preserve unknown bytes");

    const fastxlsx::detail::PackageEditorOutputPlan after_successful_save_output_plan =
        editor.planned_output();
    check_output_entry_staged_replacement_chunks(after_successful_save_output_plan.entries,
        worksheet_part.zip_path(), true,
        "source-backed CellStore rename+queued worksheet full-projection successful save should preserve staged chunks");

    editor.save_as(second_output);

    const fastxlsx::detail::PackageReader second_output_reader =
        fastxlsx::detail::PackageReader::open(second_output);
    check(second_output_reader.worksheet_part_by_sheet_name("Renamed Projection") == worksheet_part,
        "source-backed CellStore rename+queued worksheet full-projection second output should expose planned sheet name");
    check(second_output_reader.read_entry("xl/workbook.xml") == workbook_xml,
        "source-backed CellStore rename+queued worksheet full-projection second output should reuse workbook bytes");
    check(second_output_reader.read_entry("xl/worksheets/sheet1.xml") == worksheet_xml,
        "source-backed CellStore rename+queued worksheet full-projection second output should reuse worksheet staged chunks");
    check(second_output_reader.find_entry("xl/calcChain.xml") == nullptr,
        "source-backed CellStore rename+queued worksheet full-projection second output should keep stale calcChain omitted");
    check(second_output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "source-backed CellStore rename+queued worksheet full-projection second output should preserve unknown bytes");
}

void test_package_editor_source_loaded_cell_store_worksheet_chunks_reject_after_workbook_removal()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-source-cellstore-workbook-removal-chunks-source.xlsx");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    fastxlsx::detail::CellStore store =
        fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Sheet1");
    store.set_cell(4, 4, fastxlsx::CellValue::text("should not be consumed"));

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    editor.remove_part(workbook_part,
        "explicit workbook removal before CellStore chunk handoff");

    const std::size_t removal_plan_size = editor.edit_plan().size();
    const std::size_t removal_note_count = editor.edit_plan().notes().size();
    const std::size_t removal_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t removal_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const std::size_t removal_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const fastxlsx::detail::CalcChainAction removal_calc_chain_action =
        editor.edit_plan().calc_chain_action();

    std::size_t worksheet_chunk_reads = 0;
    fastxlsx::detail::WorksheetInputChunkCallback worksheet_source =
        fastxlsx::detail::cell_store_worksheet_chunk_source(store);
    fastxlsx::detail::WorksheetInputChunkCallback counted_worksheet_source =
        [&worksheet_source, &worksheet_chunk_reads](std::string& output_chunk) {
            ++worksheet_chunk_reads;
            return worksheet_source(output_chunk);
        };

    bool failed = false;
    try {
        editor.replace_worksheet_part_from_chunk_source_by_name(
            "Sheet1", counted_worksheet_source);
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "workbook sheet catalog",
            "source-backed CellStore workbook-removal failure should name the catalog");
        check_contains(error.what(), "removed",
            "source-backed CellStore workbook-removal failure should name planned removal");
    }
    check(failed,
        "PackageEditor should reject source-loaded CellStore full projection after planned workbook removal");
    check(worksheet_chunk_reads == 0,
        "source-backed CellStore workbook-removal failure should not consume chunk source");

    check(editor.edit_plan().size() == removal_plan_size,
        "source-backed CellStore workbook-removal failure should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == removal_note_count,
        "source-backed CellStore workbook-removal failure should preserve note count");
    check(editor.edit_plan().package_entries().size() == removal_package_entry_count,
        "source-backed CellStore workbook-removal failure should preserve package-entry audits");
    check(editor.edit_plan().removed_package_entries().size()
            == removal_removed_package_entry_count,
        "source-backed CellStore workbook-removal failure should preserve removed package-entry audits");
    check(editor.edit_plan().removed_parts().size() == removal_removed_part_count,
        "source-backed CellStore workbook-removal failure should preserve removed-part audits");
    check(editor.edit_plan().calc_chain_action() == removal_calc_chain_action,
        "source-backed CellStore workbook-removal failure should preserve calc policy");
    check(editor.edit_plan().find_removed_part(workbook_part) != nullptr,
        "source-backed CellStore workbook-removal failure should keep workbook removed");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/workbook.xml.rels")
            != nullptr,
        "source-backed CellStore workbook-removal failure should keep workbook relationships omitted");
    check(editor.manifest().find_part(workbook_part) == nullptr,
        "source-backed CellStore workbook-removal failure should keep workbook absent from manifest");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-backed CellStore workbook-removal failure should leave worksheet copy-original");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-backed CellStore workbook-removal failure should leave calcChain copy-original");
    const fastxlsx::detail::PackageEditorOutputPlan output_plan =
        editor.planned_output();
    check_output_entry_staged_replacement_chunks(output_plan.entries,
        worksheet_part.zip_path(), false,
        "source-backed CellStore workbook-removal failure should not stage worksheet chunks");
}

void test_package_editor_source_loaded_cell_store_worksheet_chunks_reject_invalid_planned_catalog()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-source-cellstore-invalid-planned-catalog-chunks-source.xlsx");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    fastxlsx::detail::CellStore store =
        fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Sheet1");
    store.set_cell(5, 5, fastxlsx::CellValue::text("invalid catalog should not consume this"));

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::string planned_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Broken" sheetId="1" r:id="missingRel"/></sheets>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, planned_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "invalid planned workbook catalog before CellStore chunk handoff");

    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    const std::size_t queued_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t queued_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const std::size_t queued_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const fastxlsx::detail::CalcChainAction queued_calc_chain_action =
        editor.edit_plan().calc_chain_action();

    std::size_t worksheet_chunk_reads = 0;
    fastxlsx::detail::WorksheetInputChunkCallback worksheet_source =
        fastxlsx::detail::cell_store_worksheet_chunk_source(store);
    fastxlsx::detail::WorksheetInputChunkCallback counted_worksheet_source =
        [&worksheet_source, &worksheet_chunk_reads](std::string& output_chunk) {
            ++worksheet_chunk_reads;
            return worksheet_source(output_chunk);
        };

    bool failed = false;
    try {
        editor.replace_worksheet_part_from_chunk_source_by_name(
            "Broken", counted_worksheet_source);
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "relationship id is not present",
            "source-backed CellStore invalid planned catalog failure should explain missing relationship id");
    }
    check(failed,
        "PackageEditor should reject source-loaded CellStore full projection with invalid planned catalog");
    check(worksheet_chunk_reads == 0,
        "source-backed CellStore invalid planned catalog failure should not consume chunk source");

    check(editor.edit_plan().size() == queued_plan_size,
        "source-backed CellStore invalid planned catalog failure should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "source-backed CellStore invalid planned catalog failure should not append notes");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "source-backed CellStore invalid planned catalog failure should preserve package-entry audits");
    check(editor.edit_plan().removed_package_entries().size()
            == queued_removed_package_entry_count,
        "source-backed CellStore invalid planned catalog failure should preserve removed package-entry audits");
    check(editor.edit_plan().removed_parts().size() == queued_removed_part_count,
        "source-backed CellStore invalid planned catalog failure should preserve removed-part audits");
    check(!editor.edit_plan().full_calculation_on_load(),
        "source-backed CellStore invalid planned catalog failure should not request recalculation");
    check(editor.edit_plan().calc_chain_action() == queued_calc_chain_action,
        "source-backed CellStore invalid planned catalog failure should preserve calc policy");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "source-backed CellStore invalid planned catalog failure should keep workbook replacement");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-backed CellStore invalid planned catalog failure should leave worksheet copy-original");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-backed CellStore invalid planned catalog failure should leave calcChain copy-original");
    const fastxlsx::detail::PackageEditorOutputPlan output_plan =
        editor.planned_output();
    check_output_entry_staged_replacement_chunks(output_plan.entries,
        worksheet_part.zip_path(), false,
        "source-backed CellStore invalid planned catalog failure should not stage worksheet chunks");
}

void test_package_editor_source_loaded_cell_store_worksheet_chunks_reject_wrong_namespace_planned_catalog_id()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-source-cellstore-wrong-namespace-planned-catalog-chunks-source.xlsx");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    fastxlsx::detail::CellStore store =
        fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Sheet1");
    store.set_cell(5, 5, fastxlsx::CellValue::text("wrong namespace should not consume this"));

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::string planned_workbook =
        R"(<workbook xmlns:x="urn:fastxlsx:not-relationships">)"
        R"(<sheets><sheet name="WrongNs" sheetId="1" x:id="rId1"/></sheets>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, planned_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "wrong-namespace planned workbook catalog before CellStore chunk handoff");

    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    const std::size_t queued_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t queued_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const std::size_t queued_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const fastxlsx::detail::CalcChainAction queued_calc_chain_action =
        editor.edit_plan().calc_chain_action();

    std::size_t worksheet_chunk_reads = 0;
    fastxlsx::detail::WorksheetInputChunkCallback worksheet_source =
        fastxlsx::detail::cell_store_worksheet_chunk_source(store);
    fastxlsx::detail::WorksheetInputChunkCallback counted_worksheet_source =
        [&worksheet_source, &worksheet_chunk_reads](std::string& output_chunk) {
            ++worksheet_chunk_reads;
            return worksheet_source(output_chunk);
        };

    bool failed = false;
    try {
        editor.replace_worksheet_part_from_chunk_source_by_name(
            "WrongNs", counted_worksheet_source);
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "missing relationship id",
            "source-backed CellStore wrong-namespace planned catalog failure should explain missing relationship id");
    }
    check(failed,
        "PackageEditor should reject source-loaded CellStore full projection with wrong-namespace planned catalog id");
    check(worksheet_chunk_reads == 0,
        "source-backed CellStore wrong-namespace planned catalog failure should not consume chunk source");

    check(editor.edit_plan().size() == queued_plan_size,
        "source-backed CellStore wrong-namespace planned catalog failure should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "source-backed CellStore wrong-namespace planned catalog failure should not append notes");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "source-backed CellStore wrong-namespace planned catalog failure should preserve package-entry audits");
    check(editor.edit_plan().removed_package_entries().size()
            == queued_removed_package_entry_count,
        "source-backed CellStore wrong-namespace planned catalog failure should preserve removed package-entry audits");
    check(editor.edit_plan().removed_parts().size() == queued_removed_part_count,
        "source-backed CellStore wrong-namespace planned catalog failure should preserve removed-part audits");
    check(!editor.edit_plan().full_calculation_on_load(),
        "source-backed CellStore wrong-namespace planned catalog failure should not request recalculation");
    check(editor.edit_plan().calc_chain_action() == queued_calc_chain_action,
        "source-backed CellStore wrong-namespace planned catalog failure should preserve calc policy");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "source-backed CellStore wrong-namespace planned catalog failure should keep workbook replacement");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-backed CellStore wrong-namespace planned catalog failure should leave worksheet copy-original");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-backed CellStore wrong-namespace planned catalog failure should leave calcChain copy-original");
    const fastxlsx::detail::PackageEditorOutputPlan output_plan =
        editor.planned_output();
    check_output_entry_staged_replacement_chunks(output_plan.entries,
        worksheet_part.zip_path(), false,
        "source-backed CellStore wrong-namespace planned catalog failure should not stage worksheet chunks");
}

void test_package_editor_source_loaded_cell_store_worksheet_chunks_reject_unqualified_planned_catalog_id()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-source-cellstore-unqualified-planned-catalog-chunks-source.xlsx");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    fastxlsx::detail::CellStore store =
        fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Sheet1");
    store.set_cell(5, 5, fastxlsx::CellValue::text("plain id should not consume this"));

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::string planned_workbook =
        R"(<workbook><sheets><sheet name="PlainId" sheetId="1" id="rId1"/></sheets></workbook>)";
    editor.replace_part(workbook_part, planned_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "plain-id planned workbook catalog before CellStore chunk handoff");

    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    const std::size_t queued_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t queued_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const std::size_t queued_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const fastxlsx::detail::CalcChainAction queued_calc_chain_action =
        editor.edit_plan().calc_chain_action();

    std::size_t worksheet_chunk_reads = 0;
    fastxlsx::detail::WorksheetInputChunkCallback worksheet_source =
        fastxlsx::detail::cell_store_worksheet_chunk_source(store);
    fastxlsx::detail::WorksheetInputChunkCallback counted_worksheet_source =
        [&worksheet_source, &worksheet_chunk_reads](std::string& output_chunk) {
            ++worksheet_chunk_reads;
            return worksheet_source(output_chunk);
        };

    bool failed = false;
    try {
        editor.replace_worksheet_part_from_chunk_source_by_name(
            "PlainId", counted_worksheet_source);
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "missing relationship id",
            "source-backed CellStore unqualified planned catalog failure should explain missing relationship id");
    }
    check(failed,
        "PackageEditor should reject source-loaded CellStore full projection with unqualified planned catalog id");
    check(worksheet_chunk_reads == 0,
        "source-backed CellStore unqualified planned catalog failure should not consume chunk source");

    check(editor.edit_plan().size() == queued_plan_size,
        "source-backed CellStore unqualified planned catalog failure should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "source-backed CellStore unqualified planned catalog failure should not append notes");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "source-backed CellStore unqualified planned catalog failure should preserve package-entry audits");
    check(editor.edit_plan().removed_package_entries().size()
            == queued_removed_package_entry_count,
        "source-backed CellStore unqualified planned catalog failure should preserve removed package-entry audits");
    check(editor.edit_plan().removed_parts().size() == queued_removed_part_count,
        "source-backed CellStore unqualified planned catalog failure should preserve removed-part audits");
    check(!editor.edit_plan().full_calculation_on_load(),
        "source-backed CellStore unqualified planned catalog failure should not request recalculation");
    check(editor.edit_plan().calc_chain_action() == queued_calc_chain_action,
        "source-backed CellStore unqualified planned catalog failure should preserve calc policy");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "source-backed CellStore unqualified planned catalog failure should keep workbook replacement");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-backed CellStore unqualified planned catalog failure should leave worksheet copy-original");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-backed CellStore unqualified planned catalog failure should leave calcChain copy-original");
    const fastxlsx::detail::PackageEditorOutputPlan output_plan =
        editor.planned_output();
    check_output_entry_staged_replacement_chunks(output_plan.entries,
        worksheet_part.zip_path(), false,
        "source-backed CellStore unqualified planned catalog failure should not stage worksheet chunks");
}

void test_package_editor_source_loaded_cell_store_worksheet_chunks_reject_unregistered_planned_catalog_target()
{
    CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-source-cellstore-unregistered-planned-catalog-chunks-source.xlsx");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    fastxlsx::detail::CellStore store =
        fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Sheet1");
    store.set_cell(5, 5, fastxlsx::CellValue::text("unregistered target should not consume this"));

    source.workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/missing.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="calcChain.xml"/>)"
        R"(</Relationships>)";
    rewrite_calc_source_package(source);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::string planned_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="BrokenTarget" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, planned_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "unregistered-target planned workbook catalog before CellStore chunk handoff");

    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    const std::size_t queued_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t queued_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const std::size_t queued_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const fastxlsx::detail::CalcChainAction queued_calc_chain_action =
        editor.edit_plan().calc_chain_action();

    std::size_t worksheet_chunk_reads = 0;
    fastxlsx::detail::WorksheetInputChunkCallback worksheet_source =
        fastxlsx::detail::cell_store_worksheet_chunk_source(store);
    fastxlsx::detail::WorksheetInputChunkCallback counted_worksheet_source =
        [&worksheet_source, &worksheet_chunk_reads](std::string& output_chunk) {
            ++worksheet_chunk_reads;
            return worksheet_source(output_chunk);
        };

    bool failed = false;
    try {
        editor.replace_worksheet_part_from_chunk_source_by_name(
            "BrokenTarget", counted_worksheet_source);
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "unknown part",
            "source-backed CellStore unregistered planned catalog target failure should explain the missing part");
    }
    check(failed,
        "PackageEditor should reject source-loaded CellStore full projection with unregistered planned catalog target");
    check(worksheet_chunk_reads == 0,
        "source-backed CellStore unregistered planned catalog target failure should not consume chunk source");

    check(editor.edit_plan().size() == queued_plan_size,
        "source-backed CellStore unregistered planned catalog target failure should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "source-backed CellStore unregistered planned catalog target failure should not append notes");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "source-backed CellStore unregistered planned catalog target failure should preserve package-entry audits");
    check(editor.edit_plan().removed_package_entries().size()
            == queued_removed_package_entry_count,
        "source-backed CellStore unregistered planned catalog target failure should preserve removed package-entry audits");
    check(editor.edit_plan().removed_parts().size() == queued_removed_part_count,
        "source-backed CellStore unregistered planned catalog target failure should preserve removed-part audits");
    check(!editor.edit_plan().full_calculation_on_load(),
        "source-backed CellStore unregistered planned catalog target failure should not request recalculation");
    check(editor.edit_plan().calc_chain_action() == queued_calc_chain_action,
        "source-backed CellStore unregistered planned catalog target failure should preserve calc policy");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "source-backed CellStore unregistered planned catalog target failure should keep workbook replacement");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-backed CellStore unregistered planned catalog target failure should leave worksheet copy-original");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-backed CellStore unregistered planned catalog target failure should leave calcChain copy-original");
    const fastxlsx::detail::PackageEditorOutputPlan output_plan =
        editor.planned_output();
    check_output_entry_staged_replacement_chunks(output_plan.entries,
        worksheet_part.zip_path(), false,
        "source-backed CellStore unregistered planned catalog target failure should not stage worksheet chunks");
}

void test_package_editor_source_loaded_cell_store_worksheet_chunks_replace_queued_worksheet()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-source-cellstore-queued-worksheet-chunks-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-source-cellstore-queued-worksheet-chunks-output.xlsx");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    fastxlsx::detail::CellStore store =
        fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Sheet1");
    store.set_cell(6, 4, fastxlsx::CellValue::text("full worksheet projection"));
    store.set_cell(7, 5, fastxlsx::CellValue::blank());

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::string queued_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetViews><sheetView workbookViewId="31"/></sheetViews>)"
        R"(<sheetData><row r="3"><c r="A3"><v>333</v></c></row></sheetData>)"
        R"(<autoFilter ref="A3:B3"/>)"
        R"(<extLst><ext uri="{queued-before-full-projection}"/></extLst>)"
        R"(</worksheet>)";
    replace_worksheet_part_by_name_from_single_chunk_source(editor, "Sheet1", queued_worksheet);

    const fastxlsx::detail::WorksheetInputChunkCallback worksheet_source =
        fastxlsx::detail::cell_store_worksheet_chunk_source(store);
    editor.replace_worksheet_part_from_chunk_source_by_name("Sheet1", worksheet_source);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "source-backed CellStore full projection should keep worksheet in the edit plan");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "source-backed CellStore full projection should end as stream rewrite");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr
            && workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "source-backed CellStore full projection should keep workbook calc rewrite");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "source-backed CellStore full projection should keep stale calcChain removed");
    check(editor.edit_plan().full_calculation_on_load(),
        "source-backed CellStore full projection should keep full calculation request");
    check(has_note_containing(editor.edit_plan().notes(),
              {"pull-based chunk source", "file-backed staged chunk"}),
        "source-backed CellStore full projection should expose staged chunk-source evidence");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, worksheet_part.zip_path(),
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "source-backed CellStore full projection output plan should stream-rewrite worksheet");
    check_output_entry_staged_replacement_chunks(output_plan.entries,
        worksheet_part.zip_path(), true,
        "source-backed CellStore full projection output plan should expose staged chunks");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:E7"/>)",
        "source-backed CellStore full projection output should refresh dimension");
    check_contains(worksheet_xml, R"(<f>SUM(B1:C1)</f>)",
        "source-backed CellStore full projection output should retain loaded source formula");
    check_contains(worksheet_xml,
        R"(<c r="D6" t="inlineStr"><is><t>full worksheet projection</t></is></c>)",
        "source-backed CellStore full projection output should write new text cell");
    check_contains(worksheet_xml, R"(<c r="E7"/>)",
        "source-backed CellStore full projection output should write explicit blank extent");
    check_not_contains(worksheet_xml, R"(<sheetView workbookViewId="31"/>)",
        "source-backed CellStore full projection output should not preserve prior queued wrapper");
    check_not_contains(worksheet_xml, R"(<autoFilter ref="A3:B3"/>)",
        "source-backed CellStore full projection output should not preserve prior queued autoFilter");
    check_not_contains(worksheet_xml, R"(<v>333</v>)",
        "source-backed CellStore full projection output should remove prior queued rows");
    check(output_reader.find_entry("xl/calcChain.xml") == nullptr,
        "source-backed CellStore full projection output should keep stale calcChain omitted");
    check_contains(output_reader.read_entry("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "source-backed CellStore full projection output should keep workbook recalculation request");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "source-backed CellStore full projection output should preserve unknown bytes");
}

void test_package_editor_replaces_worksheet_from_cell_store_worksheet_chunks_by_name()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-cellstore-worksheet-chunks-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-cellstore-worksheet-chunks-output.xlsx");

    fastxlsx::detail::CellStore store;
    store.set_cell(2, 2, fastxlsx::CellValue::text("projected & escaped"));
    store.set_cell(4, 1, fastxlsx::CellValue::formula("LEN(B2)&\"<ok>\""));
    store.set_cell(5, 5, fastxlsx::CellValue::blank());

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::WorksheetInputChunkCallback worksheet_source =
        fastxlsx::detail::cell_store_worksheet_chunk_source(store);
    editor.replace_worksheet_part_from_chunk_source_by_name("Sheet1", worksheet_source);

    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "CellStore worksheet chunk handoff should plan the worksheet replacement");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "CellStore worksheet chunk handoff should use staged stream rewrite");
    check(has_note_containing(editor.edit_plan().notes(),
              {"pull-based chunk source", "file-backed staged chunk"}),
        "CellStore worksheet chunk handoff should expose chunk-source staging");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "CellStore worksheet chunk handoff should remove stale calcChain by default");
    check(editor.edit_plan().full_calculation_on_load(),
        "CellStore worksheet chunk handoff should request workbook recalculation");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, worksheet_part.zip_path(),
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "CellStore worksheet chunk output plan should expose staged worksheet rewrite");
    check_output_entry_staged_replacement_chunks(output_plan.entries,
        worksheet_part.zip_path(), true,
        "CellStore worksheet chunk output plan should expose staged chunks");
    check_output_entry_plan(output_plan.entries, workbook_part.zip_path(),
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "CellStore worksheet chunk output plan should rewrite workbook calc metadata");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "CellStore worksheet chunk output should omit stale calcChain");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A2:E5"/>)",
        "CellStore worksheet chunk output should refresh worksheet dimension");
    check_contains(worksheet_xml,
        R"(<c r="B2" t="inlineStr"><is><t>projected &amp; escaped</t></is></c>)",
        "CellStore worksheet chunk output should write projected inline text");
    check_contains(worksheet_xml,
        R"(<c r="A4"><f>LEN(B2)&amp;"&lt;ok&gt;"</f></c>)",
        "CellStore worksheet chunk output should write projected formula text");
    check_contains(worksheet_xml, R"(<c r="E5"/>)",
        "CellStore worksheet chunk output should write explicit blank extent records");
    check_not_contains(worksheet_xml, "SUM(B1:C1)",
        "CellStore worksheet chunk output should remove old source formula payload");
    check_contains(output_reader.read_entry("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "CellStore worksheet chunk output should request workbook recalculation");
    check_not_contains(output_reader.read_entry("[Content_Types].xml"), "calcChain+xml",
        "CellStore worksheet chunk output should remove calcChain content type");
    check_not_contains(output_reader.read_entry("xl/_rels/workbook.xml.rels"),
        "relationships/calcChain",
        "CellStore worksheet chunk output should remove calcChain relationship");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "CellStore worksheet chunk output should preserve unknown bytes");
}

void test_package_editor_source_loaded_cell_store_worksheet_chunks_use_planned_name_after_rename()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-source-cellstore-worksheet-planned-rename-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-source-cellstore-worksheet-planned-rename-output.xlsx");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    fastxlsx::detail::CellStore store =
        fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Sheet1");
    store.set_cell(5, 5, fastxlsx::CellValue::blank());

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    editor.rename_sheet_catalog_entry("Sheet1", "Projected Data");

    const std::size_t renamed_plan_size = editor.edit_plan().size();
    const std::size_t renamed_note_count = editor.edit_plan().notes().size();
    const fastxlsx::detail::CalcChainAction renamed_calc_chain_action =
        editor.edit_plan().calc_chain_action();

    std::size_t worksheet_chunk_reads = 0;
    fastxlsx::detail::WorksheetInputChunkCallback worksheet_source =
        fastxlsx::detail::cell_store_worksheet_chunk_source(store);
    fastxlsx::detail::WorksheetInputChunkCallback counted_worksheet_source =
        [&worksheet_source, &worksheet_chunk_reads](std::string& output_chunk) {
            ++worksheet_chunk_reads;
            return worksheet_source(output_chunk);
        };

    bool failed_old_name = false;
    try {
        editor.replace_worksheet_part_from_chunk_source_by_name(
            "Sheet1", counted_worksheet_source);
    } catch (const fastxlsx::FastXlsxError& error) {
        failed_old_name = true;
        check_contains(error.what(), "workbook sheet name is not present",
            "source-backed CellStore worksheet old-name handoff should use planned catalog");
    }
    check(failed_old_name,
        "PackageEditor should reject source name after queued rename for CellStore worksheet handoff");

    check(editor.edit_plan().size() == renamed_plan_size,
        "source-backed CellStore worksheet old-name failure should preserve queued rename plan size");
    check(editor.edit_plan().notes().size() == renamed_note_count,
        "source-backed CellStore worksheet old-name failure should not append notes");
    check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
        "source-backed CellStore worksheet old-name failure should not queue calcChain removal");
    check(!editor.edit_plan().full_calculation_on_load(),
        "source-backed CellStore worksheet old-name failure should not request recalculation");
    check(editor.edit_plan().calc_chain_action() == renamed_calc_chain_action,
        "source-backed CellStore worksheet old-name failure should not change calcChain policy");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "source-backed CellStore worksheet old-name failure should preserve workbook rename rewrite");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-backed CellStore worksheet old-name failure should leave worksheet copy-original");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-backed CellStore worksheet old-name failure should leave calcChain copy-original");
    check(worksheet_chunk_reads == 0,
        "source-backed CellStore worksheet old-name failure should not consume worksheet chunks");

    editor.replace_worksheet_part_from_chunk_source_by_name(
        "Projected Data", counted_worksheet_source);
    check(worksheet_chunk_reads > 0,
        "source-backed CellStore worksheet planned-name handoff should consume worksheet chunks");

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr
            && worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "source-backed CellStore worksheet planned-name handoff should stage stream rewrite");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "source-backed CellStore worksheet planned-name handoff should remove stale calcChain");
    check(editor.edit_plan().full_calculation_on_load(),
        "source-backed CellStore worksheet planned-name handoff should request recalculation");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "source-backed CellStore worksheet planned-name output should omit stale calcChain");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("Projected Data") == worksheet_part,
        "source-backed CellStore worksheet planned-name output should expose renamed sheet");

    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Projected Data")",
        "source-backed CellStore worksheet planned-name output should preserve renamed catalog");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "source-backed CellStore worksheet planned-name output should request workbook recalculation");

    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:E5"/>)",
        "source-backed CellStore worksheet planned-name output should refresh dimension");
    check_contains(worksheet_xml, R"(<f>SUM(B1:C1)</f>)",
        "source-backed CellStore worksheet planned-name output should retain loaded source formula");
    check_contains(worksheet_xml, R"(<c r="E5"/>)",
        "source-backed CellStore worksheet planned-name output should write explicit blank extent");
    check_not_contains(worksheet_xml, "<v>3</v>",
        "source-backed CellStore worksheet planned-name output should drop old cached formula value");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "source-backed CellStore worksheet planned-name output should preserve unknown bytes");
}

void test_package_editor_source_loaded_cell_store_loads_semantic_values_by_name()
{
    CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-source-cellstore-semantic-source.xlsx");
    source.worksheet =
        R"(<worksheet><sheetData><row r="1">)"
        R"(<c r="A1"><v>12.5</v></c>)"
        R"(<c r="B1" t="b"><v>1</v></c>)"
        R"(<c r="C1" t="inlineStr"><is><t xml:space="preserve"> hello &amp; raw </t></is></c>)"
        R"(<c r="D1"><f>SUM(A1:B1)&amp;"&lt;ok&gt;"</f><v>999</v></c>)"
        R"(<c r="E1"/>)"
        R"(<c r="F1" t="e"><v>#VALUE!</v></c>)"
        R"(</row><row r="2"><c r="A2"><v>0</v></c></row></sheetData></worksheet>)";
    rewrite_calc_source_package(source);
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-source-cellstore-semantic-output.xlsx");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    const fastxlsx::detail::CellStore store =
        fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Sheet1");

    check(store.cell_count() == 7,
        "package-backed CellStore semantic loader should materialize explicit source cells");
    const fastxlsx::detail::CellRecord* number = store.find_cell(1, 1);
    check(number != nullptr && number->kind == fastxlsx::CellValueKind::Number,
        "package-backed CellStore semantic loader should load numeric cells");
    check(number->number_value == 12.5,
        "package-backed CellStore semantic loader numeric payload mismatch");
    const fastxlsx::detail::CellRecord* boolean = store.find_cell(1, 2);
    check(boolean != nullptr && boolean->kind == fastxlsx::CellValueKind::Boolean,
        "package-backed CellStore semantic loader should load boolean cells");
    check(boolean->boolean_value,
        "package-backed CellStore semantic loader boolean payload mismatch");
    const fastxlsx::detail::CellRecord* text = store.find_cell(1, 3);
    check(text != nullptr && text->kind == fastxlsx::CellValueKind::Text,
        "package-backed CellStore semantic loader should load inline string cells");
    check(text->text_value == " hello & raw ",
        "package-backed CellStore semantic loader should decode inline string text");
    const fastxlsx::detail::CellRecord* formula = store.find_cell(1, 4);
    check(formula != nullptr && formula->kind == fastxlsx::CellValueKind::Formula,
        "package-backed CellStore semantic loader should load formula cells");
    check(formula->text_value == "SUM(A1:B1)&\"<ok>\"",
        "package-backed CellStore semantic loader should decode formula text and ignore cached values");
    const fastxlsx::detail::CellRecord* blank = store.find_cell(1, 5);
    check(blank != nullptr && blank->kind == fastxlsx::CellValueKind::Blank,
        "package-backed CellStore semantic loader should keep explicit blank cells");
    const fastxlsx::detail::CellRecord* error = store.find_cell(1, 6);
    check(error != nullptr && error->kind == fastxlsx::CellValueKind::Error,
        "package-backed CellStore semantic loader should load error cells");
    check(error->text_value == "#VALUE!",
        "package-backed CellStore semantic loader error payload mismatch");
    const fastxlsx::detail::CellRecord* zero = store.find_cell(2, 1);
    check(zero != nullptr && zero->kind == fastxlsx::CellValueKind::Number
            && zero->number_value == 0.0,
        "package-backed CellStore semantic loader should preserve zero numeric values");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::WorksheetInputChunkCallback replacement_sheet_data_source =
        fastxlsx::detail::cell_store_sheet_data_chunk_source(store);
    editor.replace_worksheet_sheet_data_from_chunk_source_by_name(
        "Sheet1", replacement_sheet_data_source);
    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<c r="A1"><v>12.5</v></c>)",
        "package-backed CellStore semantic output should write numeric cells");
    check_contains(worksheet_xml, R"(<c r="B1" t="b"><v>1</v></c>)",
        "package-backed CellStore semantic output should write boolean cells");
    check_contains(worksheet_xml,
        R"(<c r="C1" t="inlineStr"><is><t xml:space="preserve"> hello &amp; raw </t></is></c>)",
        "package-backed CellStore semantic output should re-escape inline text");
    check_contains(worksheet_xml,
        R"(<c r="D1"><f>SUM(A1:B1)&amp;"&lt;ok&gt;"</f></c>)",
        "package-backed CellStore semantic output should write semantic formula text without cached value");
    check_contains(worksheet_xml, R"(<c r="E1"/>)",
        "package-backed CellStore semantic output should preserve explicit blank cells");
    check_contains(worksheet_xml, R"(<c r="F1" t="e"><v>#VALUE!</v></c>)",
        "package-backed CellStore semantic output should write error cells");
    check_contains(worksheet_xml, R"(<c r="A2"><v>0</v></c>)",
        "package-backed CellStore semantic output should preserve zero numeric cells");
    check_not_contains(worksheet_xml, "<v>999</v>",
        "package-backed CellStore semantic output should drop old cached formula values");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "package-backed CellStore semantic output should omit stale calcChain");
    check_contains(output_reader.read_entry("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "package-backed CellStore semantic output should request workbook recalculation");
    check_not_contains(output_reader.read_entry("[Content_Types].xml"), "calcChain+xml",
        "package-backed CellStore semantic output should remove calcChain content type");
    check_not_contains(output_reader.read_entry("xl/_rels/workbook.xml.rels"),
        "relationships/calcChain",
        "package-backed CellStore semantic output should remove calcChain relationship");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "package-backed CellStore semantic output should preserve unknown bytes");
}

void test_package_editor_source_loaded_cell_store_preserves_unreferenced_styles()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-editor-source-cellstore-unreferenced-styles-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-source-cellstore-unreferenced-styles-output.xlsx");

    {
        auto workbook = fastxlsx::WorkbookWriter::create(source_path);
        const auto text_style = workbook.add_style(fastxlsx::CellStyle {"@"});
        auto plain = workbook.add_worksheet("Plain");
        auto style_owner = workbook.add_worksheet("StyleOwner");
        plain.append_row({fastxlsx::CellView::text("unstyled source")});
        style_owner.append_row({fastxlsx::CellView::text("styled keeper").with_style(text_style)});
        workbook.close();
    }

    const auto source_entries = fastxlsx::test::read_zip_entries(source_path);
    check(source_entries.find("xl/styles.xml") != source_entries.end(),
        "source-loaded CellStore unreferenced-style fixture should contain styles.xml");
    const std::string styles_before = source_entries.at("xl/styles.xml");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source_path);
    fastxlsx::detail::CellStore store =
        fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Plain");
    const fastxlsx::detail::CellRecord* source_cell = store.try_cell(1, 1);
    check(source_cell != nullptr && source_cell->kind == fastxlsx::CellValueKind::Text,
        "source-loaded CellStore should load unstyled cells even when styles.xml exists");
    check(!source_cell->style_id.has_value(),
        "source-loaded CellStore should not invent style handles for unstyled source cells");
    store.set_cell(1, 2, fastxlsx::CellValue::text("patched"));

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source_path);
    const fastxlsx::detail::WorksheetInputChunkCallback replacement_sheet_data_source =
        fastxlsx::detail::cell_store_sheet_data_chunk_source(store);
    editor.replace_worksheet_sheet_data_from_chunk_source_by_name(
        "Plain", replacement_sheet_data_source);

    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "source-loaded CellStore output plan should preserve unreferenced styles");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>unstyled source</t></is></c><c r="B1" t="inlineStr"><is><t>patched</t></is></c></row></sheetData>)",
        "source-loaded CellStore should rewrite unstyled source cells without style attributes");
    check_not_contains(worksheet_xml, R"(<c r="A1" s=)",
        "source-loaded CellStore should not invent style references for unstyled source cells");
    check_not_contains(worksheet_xml, R"(<c r="B1" s=)",
        "source-loaded CellStore should not invent style references for new unstyled cells");
    check(output_reader.read_entry("xl/styles.xml") == styles_before,
        "source-loaded CellStore output should preserve unreferenced styles.xml bytes");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "source-loaded CellStore output should preserve styles content type");
    check_contains(output_reader.read_entry("xl/_rels/workbook.xml.rels"),
        "relationships/styles",
        "source-loaded CellStore output should preserve unreferenced styles relationship");
    const fastxlsx::detail::RelationshipGraph graph = output_reader.relationship_graph();
    const fastxlsx::detail::RelationshipSet* workbook_relationships =
        graph.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "source-loaded CellStore output graph should preserve workbook relationships");
    check(std::any_of(workbook_relationships->relationships().begin(),
              workbook_relationships->relationships().end(),
              [](const fastxlsx::detail::Relationship& relationship) {
                  return relationship.type
                          == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles"
                      && relationship.target == "styles.xml"
                      && relationship.target_mode
                          == fastxlsx::detail::Relationship::TargetMode::Internal;
              }),
        "source-loaded CellStore output graph should re-read the styles relationship");
}

void test_package_editor_source_loaded_cell_store_preserves_unreferenced_shared_strings()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-editor-source-cellstore-unreferenced-sharedstrings-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-source-cellstore-unreferenced-sharedstrings-output.xlsx");

    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        auto workbook = fastxlsx::WorkbookWriter::create(source_path, options);
        auto plain = workbook.add_worksheet("Plain");
        auto shared_owner = workbook.add_worksheet("SharedOwner");
        plain.append_row({fastxlsx::CellView::number(123.0)});
        shared_owner.append_row({fastxlsx::CellView::text("shared keeper")});
        workbook.close();
    }

    const auto source_entries = fastxlsx::test::read_zip_entries(source_path);
    check(source_entries.find("xl/sharedStrings.xml") != source_entries.end(),
        "source-loaded CellStore unreferenced-sharedStrings fixture should contain sharedStrings.xml");
    const std::string shared_strings_before = source_entries.at("xl/sharedStrings.xml");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source_path);
    fastxlsx::detail::CellStore store =
        fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Plain");
    const fastxlsx::detail::CellRecord* source_cell = store.try_cell(1, 1);
    check(source_cell != nullptr && source_cell->kind == fastxlsx::CellValueKind::Number,
        "source-loaded CellStore should load non-shared cells even when sharedStrings.xml exists");
    store.set_cell(1, 2, fastxlsx::CellValue::text("patched inline"));

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source_path);
    const fastxlsx::detail::WorksheetInputChunkCallback replacement_sheet_data_source =
        fastxlsx::detail::cell_store_sheet_data_chunk_source(store);
    editor.replace_worksheet_sheet_data_from_chunk_source_by_name(
        "Plain", replacement_sheet_data_source);

    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "source-loaded CellStore output plan should preserve unreferenced sharedStrings");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<sheetData><row r="1"><c r="A1"><v>123</v></c><c r="B1" t="inlineStr"><is><t>patched inline</t></is></c></row></sheetData>)",
        "source-loaded CellStore should rewrite non-shared source cells without shared string indexes");
    check_not_contains(worksheet_xml, R"(t="s")",
        "source-loaded CellStore should not invent shared string references");
    check(output_reader.read_entry("xl/sharedStrings.xml") == shared_strings_before,
        "source-loaded CellStore output should preserve unreferenced sharedStrings bytes");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "source-loaded CellStore output should preserve sharedStrings content type");
    check_contains(output_reader.read_entry("xl/_rels/workbook.xml.rels"),
        "relationships/sharedStrings",
        "source-loaded CellStore output should preserve unreferenced sharedStrings relationship");
    const fastxlsx::detail::RelationshipGraph graph = output_reader.relationship_graph();
    const fastxlsx::detail::RelationshipSet* workbook_relationships =
        graph.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "source-loaded CellStore output graph should preserve workbook relationships");
    check(std::any_of(workbook_relationships->relationships().begin(),
              workbook_relationships->relationships().end(),
              [](const fastxlsx::detail::Relationship& relationship) {
                  return relationship.type
                          == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings"
                      && relationship.target == "sharedStrings.xml"
                      && relationship.target_mode
                          == fastxlsx::detail::Relationship::TargetMode::Internal;
              }),
        "source-loaded CellStore output graph should re-read the sharedStrings relationship");
}

void test_package_editor_source_loaded_cell_store_materializes_prefixed_shared_strings()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-editor-source-cellstore-prefixed-sharedstrings-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-source-cellstore-prefixed-sharedstrings-output.xlsx");

    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        auto workbook = fastxlsx::WorkbookWriter::create(source_path, options);
        auto data = workbook.add_worksheet("Data");
        auto untouched = workbook.add_worksheet("Untouched");
        data.append_row({fastxlsx::CellView::text("prefix-placeholder-a"),
            fastxlsx::CellView::text("prefix-placeholder-b")});
        untouched.append_row({fastxlsx::CellView::text("keep-prefixed-package")});
        workbook.close();
    }

    const std::string prefixed_shared_strings =
        R"(<?xml version='1.1' encoding='UTF_8-Test.1' standalone='no'?>)"
        R"(<?xml-stylesheet type="text/xsl" href="prefixed-sharedStrings.xsl"?>)"
        R"(<?fastxlsx.data-1:probe legal-target?>)"
        R"(<?_fastxlsx legal-start?>)"
        R"(<?:fastxlsx legal-colon-start?>)"
        R"(<?fastxlsx?>)"
        R"(<x:sst xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:fx="urn:fastxlsx:test" count="2" uniqueCount="2">)"
        R"(<x:si><x:t>package-prefixed-A&amp;B</x:t></x:si>)"
        R"(<x:si><x:r><x:t>rich-</x:t></x:r><x:r><x:t xml:space="preserve"> tail </x:t></x:r>)"
        R"(<x:rPh sb="1" eb="1"/><x:phoneticPr fontId="1"/><x:extLst/>)"
        R"(<x:rPh sb="0" eb="1"><fx:opaque><x:r><x:t>ignored-nested-phonetic</x:t></x:r></fx:opaque></x:rPh>)"
        R"(<x:phoneticPr fontId="1"/>)"
        R"(<x:extLst><x:ext uri="{fastxlsx-test}"><fx:opaque><x:r><x:t>ignored-nested-ext</x:t></x:r></fx:opaque></x:ext></x:extLst></x:si>)"
        R"(<x:phoneticPr fontId="2"/><x:extLst/><x:extLst><x:ext uri="{fastxlsx-root}"><fx:opaque><x:t>ignored-root-ext</x:t></fx:opaque></x:ext></x:extLst>)"
        R"(</x:sst>)";
    check_contains(prefixed_shared_strings,
        "version='1.1' encoding='UTF_8-Test.1' standalone='no'",
        "package-backed prefixed sharedStrings fixture should cover legal declaration metadata");
    check_contains(prefixed_shared_strings, "<?xml-stylesheet",
        "package-backed prefixed sharedStrings fixture should cover xml-stylesheet PI trivia");
    check_contains(prefixed_shared_strings, "<?fastxlsx.data-1:probe",
        "package-backed prefixed sharedStrings fixture should cover legal PI target continuation trivia");
    check_contains(prefixed_shared_strings, "<?_fastxlsx",
        "package-backed prefixed sharedStrings fixture should cover underscore-start PI target trivia");
    check_contains(prefixed_shared_strings, "<?:fastxlsx",
        "package-backed prefixed sharedStrings fixture should cover colon-start PI target trivia");
    check_contains(prefixed_shared_strings, "<?fastxlsx?>",
        "package-backed prefixed sharedStrings fixture should cover empty-data PI trivia");
    rewrite_package_entry_as_stored(source_path, "xl/sharedStrings.xml", prefixed_shared_strings);

    const auto source_entries = fastxlsx::test::read_zip_entries(source_path);
    const std::string shared_strings_before = source_entries.at("xl/sharedStrings.xml");
    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source_path);
    fastxlsx::detail::CellStore store =
        fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Data");

    const fastxlsx::detail::CellRecord* a1 = store.try_cell(1, 1);
    const fastxlsx::detail::CellRecord* b1 = store.try_cell(1, 2);
    check(a1 != nullptr && a1->kind == fastxlsx::CellValueKind::Text
            && a1->text_value == "package-prefixed-A&B",
        "package-backed CellStore should materialize prefixed sharedStrings text");
    check(b1 != nullptr && b1->kind == fastxlsx::CellValueKind::Text
            && b1->text_value == "rich- tail ",
        "package-backed CellStore should flatten prefixed rich sharedStrings by local-name");
    check_contains(shared_strings_before, "ignored-nested-ext",
        "package-backed prefixed sharedStrings fixture should carry nested ignored extension text");
    check_contains(shared_strings_before, "ignored-root-ext",
        "package-backed prefixed sharedStrings fixture should carry root-level ignored extension text");
    check_contains(shared_strings_before, "<x:extLst/>",
        "package-backed prefixed sharedStrings fixture should carry self-closing ignored metadata");

    store.set_cell(1, 3, fastxlsx::CellValue::text("package-prefixed-patched"));

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source_path);
    editor.replace_worksheet_sheet_data_from_chunk_source_by_name(
        "Data", fastxlsx::detail::cell_store_sheet_data_chunk_source(store));

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "prefixed sharedStrings CellStore output plan should preserve sharedStrings");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>package-prefixed-A&amp;B</t></is></c>)",
        "prefixed sharedStrings CellStore output should project A1 as inline text");
    check_contains(worksheet_xml,
        R"(<c r="B1" t="inlineStr"><is><t xml:space="preserve">rich- tail </t></is></c>)",
        "prefixed sharedStrings CellStore output should preserve flattened whitespace");
    check_contains(worksheet_xml,
        R"(<c r="C1" t="inlineStr"><is><t>package-prefixed-patched</t></is></c>)",
        "prefixed sharedStrings CellStore output should include the new edit");
    check_not_contains(worksheet_xml, "ignored-nested-phonetic",
        "prefixed sharedStrings CellStore output should not leak nested ignored phonetic text");
    check_not_contains(worksheet_xml, "ignored-nested-ext",
        "prefixed sharedStrings CellStore output should not leak nested ignored extension text");
    check_not_contains(worksheet_xml, "ignored-root-ext",
        "prefixed sharedStrings CellStore output should not leak root-level ignored extension text");
    check_not_contains(worksheet_xml, R"(t="s")",
        "prefixed sharedStrings CellStore output should not write shared string indexes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == shared_strings_before,
        "prefixed sharedStrings CellStore output should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "prefixed sharedStrings CellStore output should preserve untouched sheets");
}

void test_package_editor_source_loaded_cell_store_materializes_prefixed_inline_strings()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-editor-source-cellstore-prefixed-inline-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-source-cellstore-prefixed-inline-output.xlsx");

    {
        auto workbook = fastxlsx::WorkbookWriter::create(source_path);
        auto data = workbook.add_worksheet("Data");
        auto untouched = workbook.add_worksheet("Untouched");
        data.append_row({fastxlsx::CellView::text("prefixed-inline-placeholder")});
        untouched.append_row({fastxlsx::CellView::text("keep-prefixed-inline-package")});
        workbook.close();
    }

    const std::string worksheet_xml =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<x:worksheet xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:fx="urn:fastxlsx:test">)"
          R"(<x:sheetData>)"
          R"(<x:row r="1">)"
          R"(<x:c r="A1" t="inlineStr"><x:is><x:t>package-prefixed-inline</x:t></x:is></x:c>)"
          R"(<x:c r="B1" t="inlineStr"><x:is><x:t xml:space="preserve"> package space </x:t></x:is></x:c>)"
          R"(<x:c r="C1" t="inlineStr"><x:is>)"
          R"(<x:r><x:rPr><x:b/></x:rPr><x:t>pkg-rich-</x:t></x:r>)"
          R"(<x:r><x:t>tail</x:t></x:r>)"
          R"(<x:rPh sb="1" eb="1"/><x:phoneticPr fontId="1"/><x:extLst/>)"
          R"(<x:rPh sb="0" eb="1"><fx:opaque><x:r><x:t>ignored-nested-phonetic</x:t></x:r></fx:opaque></x:rPh>)"
          R"(<x:extLst><x:ext uri="{fastxlsx-test}"><fx:opaque><x:r><x:t>ignored-nested-ext</x:t></x:r></fx:opaque></x:ext></x:extLst>)"
          R"(</x:is></x:c>)"
          R"(</x:row>)"
          R"(<x:row r="2">)"
          R"(<x:c r="A2"><x:v>42</x:v></x:c>)"
          R"(<x:c r="B2" t="b"><x:v>1</x:v></x:c>)"
          R"(<x:c r="C2"><x:f>SUM(A2:A2)</x:f><x:v>999</x:v></x:c>)"
          R"(<x:c r="D2"><x:f t="array" ref="D2" aca="1" ca="1" bx="1">SUM(A2:A2)</x:f><x:v>42</x:v></x:c>)"
          R"(<x:c r="E2"><x:f t="shared" si="0" dt2D="1" dtr="1" del1="0" del2="0" r1="A1" r2="B1"/><x:v>7</x:v></x:c>)"
          R"(<x:c r="F2" ph="1"><x:v>8</x:v></x:c>)"
          R"(</x:row>)"
          R"(</x:sheetData>)"
          R"(</x:worksheet>)";
    rewrite_package_entry_as_stored(source_path, "xl/worksheets/sheet1.xml", worksheet_xml);

    const auto source_entries = fastxlsx::test::read_zip_entries(source_path);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "ignored-nested-ext",
        "package-backed prefixed inline fixture should carry nested ignored extension text");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "<x:extLst/>",
        "package-backed prefixed inline fixture should carry self-closing ignored metadata");
    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source_path);
    fastxlsx::detail::CellStore store =
        fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Data");

    const fastxlsx::detail::CellRecord* a1 = store.try_cell(1, 1);
    const fastxlsx::detail::CellRecord* b1 = store.try_cell(1, 2);
    const fastxlsx::detail::CellRecord* c1 = store.try_cell(1, 3);
    const fastxlsx::detail::CellRecord* a2 = store.try_cell(2, 1);
    const fastxlsx::detail::CellRecord* b2 = store.try_cell(2, 2);
    const fastxlsx::detail::CellRecord* c2 = store.try_cell(2, 3);
    const fastxlsx::detail::CellRecord* d2 = store.try_cell(2, 4);
    const fastxlsx::detail::CellRecord* e2 = store.try_cell(2, 5);
    const fastxlsx::detail::CellRecord* f2 = store.try_cell(2, 6);
    check(a1 != nullptr && a1->kind == fastxlsx::CellValueKind::Text
            && a1->text_value == "package-prefixed-inline",
        "package-backed CellStore should materialize prefixed inline text by local-name");
    check(b1 != nullptr && b1->kind == fastxlsx::CellValueKind::Text
            && b1->text_value == " package space ",
        "package-backed CellStore should keep prefixed inline xml:space text");
    check(c1 != nullptr && c1->kind == fastxlsx::CellValueKind::Text
            && c1->text_value == "pkg-rich-tail",
        "package-backed CellStore should flatten prefixed inline rich text by local-name");
    check(a2 != nullptr && a2->kind == fastxlsx::CellValueKind::Number
            && a2->number_value == 42.0,
        "package-backed CellStore should materialize prefixed numeric values");
    check(b2 != nullptr && b2->kind == fastxlsx::CellValueKind::Boolean
            && b2->boolean_value,
        "package-backed CellStore should materialize prefixed boolean values");
    check(c2 != nullptr && c2->kind == fastxlsx::CellValueKind::Formula
            && c2->text_value == "SUM(A2:A2)",
        "package-backed CellStore should materialize prefixed formula wrappers");
    check(d2 != nullptr && d2->kind == fastxlsx::CellValueKind::Formula
            && d2->text_value == "SUM(A2:A2)",
        "package-backed CellStore should flatten prefixed formula metadata attributes");
    check(e2 != nullptr && e2->kind == fastxlsx::CellValueKind::Number
            && e2->number_value == 7.0,
        "package-backed CellStore should materialize cached values for metadata-only formulas");
    check(f2 != nullptr && f2->kind == fastxlsx::CellValueKind::Number
            && f2->number_value == 8.0,
        "package-backed CellStore should ignore source phonetic cell metadata attributes");

    store.set_cell(2, 7, fastxlsx::CellValue::text("package-prefixed-inline-patched"));

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source_path);
    editor.replace_worksheet_sheet_data_from_chunk_source_by_name(
        "Data", fastxlsx::detail::cell_store_sheet_data_chunk_source(store));

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string output_worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(output_worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>package-prefixed-inline</t></is></c>)",
        "source-loaded CellStore should project prefixed inline text as inlineStr");
    check_contains(output_worksheet_xml,
        R"(<c r="B1" t="inlineStr"><is><t xml:space="preserve"> package space </t></is></c>)",
        "source-loaded CellStore should preserve prefixed inline whitespace in projection");
    check_contains(output_worksheet_xml,
        R"(<c r="C1" t="inlineStr"><is><t>pkg-rich-tail</t></is></c>)",
        "source-loaded CellStore should project flattened prefixed inline rich text");
    check_contains(output_worksheet_xml, R"(<c r="A2"><v>42</v></c>)",
        "source-loaded CellStore should project prefixed numeric values");
    check_contains(output_worksheet_xml, R"(<c r="B2" t="b"><v>1</v></c>)",
        "source-loaded CellStore should project prefixed boolean values");
    check_contains(output_worksheet_xml, R"(<c r="C2"><f>SUM(A2:A2)</f></c>)",
        "source-loaded CellStore should project formulas without cached values");
    check_contains(output_worksheet_xml, R"(<c r="D2"><f>SUM(A2:A2)</f></c>)",
        "source-loaded CellStore should project flattened formula metadata without cached values");
    check_contains(output_worksheet_xml, R"(<c r="E2"><v>7</v></c>)",
        "source-loaded CellStore should project metadata-only shared formulas as cached values");
    check_contains(output_worksheet_xml, R"(<c r="F2"><v>8</v></c>)",
        "source-loaded CellStore should project source phonetic cells without ph metadata");
    check_contains(output_worksheet_xml,
        R"(<c r="G2" t="inlineStr"><is><t>package-prefixed-inline-patched</t></is></c>)",
        "source-loaded CellStore should include edits beside prefixed source cells");
    check_not_contains(output_worksheet_xml, "ignored-phonetic",
        "source-loaded CellStore projection should not keep prefixed phonetic text");
    check_not_contains(output_worksheet_xml, "ignored-ext",
        "source-loaded CellStore projection should not keep prefixed extension text");
    check_not_contains(output_worksheet_xml, "ignored-nested-phonetic",
        "source-loaded CellStore projection should not keep nested ignored phonetic text");
    check_not_contains(output_worksheet_xml, "ignored-nested-ext",
        "source-loaded CellStore projection should not keep nested ignored extension text");
    check_not_contains(output_worksheet_xml, "<x:v>999</x:v>",
        "source-loaded CellStore projection should not keep stale cached values");
    check_not_contains(output_worksheet_xml, R"(ca="1")",
        "source-loaded CellStore projection should not preserve formula calc metadata");
    check_not_contains(output_worksheet_xml, R"(dt2D="1")",
        "source-loaded CellStore projection should not preserve dataTable formula metadata");
    check(output_reader.read_entry("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "prefixed inline CellStore output should preserve untouched sheets");
}

void test_package_editor_source_loaded_cell_store_materializes_local_names_without_namespace_validation()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-editor-source-cellstore-local-name-no-namespace-validation-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-source-cellstore-local-name-no-namespace-validation-output.xlsx");

    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        auto workbook = fastxlsx::WorkbookWriter::create(source_path, options);
        auto data = workbook.add_worksheet("Data");
        auto untouched = workbook.add_worksheet("Untouched");
        data.append_row({fastxlsx::CellView::text("wrong-ns-package-a"),
            fastxlsx::CellView::text("wrong-ns-package-b")});
        untouched.append_row({fastxlsx::CellView::text("keep-wrong-ns-package")});
        workbook.close();
    }

    const std::string wrong_namespace_shared_strings =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<bad:sst xmlns:bad="urn:fastxlsx:not-spreadsheetml" count="2" uniqueCount="2">)"
        R"(<bad:si><bad:t>package-wrong-ns-shared</bad:t></bad:si>)"
        R"(<bad:si><bad:r><bad:t>package-wrong-rich-</bad:t></bad:r><bad:r><bad:t>tail</bad:t></bad:r></bad:si>)"
        R"(</bad:sst>)";
    rewrite_package_entry_as_stored(
        source_path, "xl/sharedStrings.xml", wrong_namespace_shared_strings);

    const std::string wrong_namespace_worksheet =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<bad:worksheet xmlns:bad="urn:fastxlsx:not-spreadsheetml">)"
          R"(<bad:sheetData><bad:row r="1">)"
          R"(<bad:c r="A1" t="s"><bad:v>0</bad:v></bad:c>)"
          R"(<bad:c r="B1" t="inlineStr"><bad:is><bad:t>package-wrong-ns-inline</bad:t></bad:is></bad:c>)"
          R"(<bad:c r="C1" t="s"><bad:v>1</bad:v></bad:c>)"
          R"(</bad:row></bad:sheetData>)"
          R"(</bad:worksheet>)";
    rewrite_package_entry_as_stored(
        source_path, "xl/worksheets/sheet1.xml", wrong_namespace_worksheet);

    const auto source_entries = fastxlsx::test::read_zip_entries(source_path);
    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source_path);
    fastxlsx::detail::CellStore store =
        fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Data");

    const fastxlsx::detail::CellRecord* a1 = store.try_cell(1, 1);
    const fastxlsx::detail::CellRecord* b1 = store.try_cell(1, 2);
    const fastxlsx::detail::CellRecord* c1 = store.try_cell(1, 3);
    check(a1 != nullptr && a1->kind == fastxlsx::CellValueKind::Text
            && a1->text_value == "package-wrong-ns-shared",
        "package-backed CellStore should materialize sharedStrings by local-name without namespace URI validation");
    check(b1 != nullptr && b1->kind == fastxlsx::CellValueKind::Text
            && b1->text_value == "package-wrong-ns-inline",
        "package-backed CellStore should materialize inline text by local-name without namespace URI validation");
    check(c1 != nullptr && c1->kind == fastxlsx::CellValueKind::Text
            && c1->text_value == "package-wrong-rich-tail",
        "package-backed CellStore should flatten rich sharedStrings without namespace URI validation");

    store.set_cell(1, 4, fastxlsx::CellValue::text("package-wrong-ns-patched"));

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source_path);
    editor.replace_worksheet_sheet_data_from_chunk_source_by_name(
        "Data", fastxlsx::detail::cell_store_sheet_data_chunk_source(store));

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>package-wrong-ns-shared</t></is></c>)",
        "wrong-namespace CellStore output should project sharedStrings text as inline text");
    check_contains(worksheet_xml,
        R"(<c r="B1" t="inlineStr"><is><t>package-wrong-ns-inline</t></is></c>)",
        "wrong-namespace CellStore output should project inline text as inline text");
    check_contains(worksheet_xml,
        R"(<c r="C1" t="inlineStr"><is><t>package-wrong-rich-tail</t></is></c>)",
        "wrong-namespace CellStore output should project flattened rich sharedStrings text");
    check_contains(worksheet_xml,
        R"(<c r="D1" t="inlineStr"><is><t>package-wrong-ns-patched</t></is></c>)",
        "wrong-namespace CellStore output should include edits beside source-loaded cells");
    check(output_reader.read_entry("xl/sharedStrings.xml")
            == source_entries.at("xl/sharedStrings.xml"),
        "wrong-namespace CellStore output should preserve source sharedStrings bytes");
    check(output_reader.read_entry("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "wrong-namespace CellStore output should preserve untouched sheets");
}

void test_package_editor_source_loaded_cell_store_distinguishes_blank_and_erase()
{
    CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-source-cellstore-blank-erase-source.xlsx");
    source.worksheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>11</v></c><c r="B1"><v>22</v></c></row></sheetData></worksheet>)";
    rewrite_calc_source_package(source);
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-source-cellstore-blank-erase-output.xlsx");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    fastxlsx::detail::CellStore store =
        fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Sheet1");
    check(store.cell_count() == 2,
        "blank-vs-erase source-backed CellStore should load source cells");
    check(store.try_cell(1, 1) != nullptr && store.try_cell(1, 2) != nullptr,
        "blank-vs-erase source-backed CellStore should expose both source cells");

    store.set_cell(1, 1, fastxlsx::CellValue::blank());
    store.erase_cell(1, 2);
    const fastxlsx::detail::CellRecord* blank = store.try_cell(1, 1);
    check(blank != nullptr && blank->kind == fastxlsx::CellValueKind::Blank,
        "blank-vs-erase CellStore should keep explicit blank overwrite records");
    check(store.try_cell(1, 2) == nullptr,
        "blank-vs-erase CellStore should remove erased source records");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    editor.replace_worksheet_sheet_data_from_chunk_source_by_name(
        "Sheet1", fastxlsx::detail::cell_store_sheet_data_chunk_source(store));

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "blank-vs-erase source-backed CellStore output should omit stale calcChain");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<sheetData><row r="1"><c r="A1"/></row></sheetData>)",
        "blank-vs-erase output should write explicit blank source overwrite");
    check_not_contains(worksheet_xml, R"(r="B1")",
        "blank-vs-erase output should omit erased source cells");
    check_not_contains(worksheet_xml, R"(<v>11</v>)",
        "blank-vs-erase output should remove the old blanked source value");
    check_not_contains(worksheet_xml, R"(<v>22</v>)",
        "blank-vs-erase output should remove the old erased source value");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "blank-vs-erase source-backed CellStore output should preserve unknown bytes");
}

void test_package_editor_source_loaded_cell_store_preserves_loader_options()
{
    CalcSourcePackage max_source =
        write_calc_source_package("fastxlsx-package-editor-source-cellstore-option-max-source.xlsx");
    max_source.worksheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>11</v></c><c r="B1"><v>22</v></c></row></sheetData></worksheet>)";
    rewrite_calc_source_package(max_source);

    const fastxlsx::detail::PackageReader max_source_reader =
        fastxlsx::detail::PackageReader::open(max_source.path);
    fastxlsx::detail::CellStoreOptions max_options;
    max_options.max_cells = 2;
    fastxlsx::detail::CellStore max_guarded_store =
        fastxlsx::detail::load_cell_store_from_workbook_sheet(
            max_source_reader, "Sheet1", max_options);
    check(max_guarded_store.options().max_cells == 2,
        "package-backed CellStore loader should preserve max_cells options");

    bool max_failed = false;
    try {
        max_guarded_store.set_cell(1, 3, fastxlsx::CellValue::number(33.0));
    } catch (const fastxlsx::FastXlsxError&) {
        max_failed = true;
    }
    check(max_failed,
        "package-backed CellStore loader returned store should keep enforcing max_cells");
    check(max_guarded_store.cell_count() == 2,
        "package-backed CellStore max_cells failure should preserve source-loaded records");
    check(max_guarded_store.find_cell(1, 3) == nullptr,
        "package-backed CellStore max_cells failure should not leave rejected records");

    CalcSourcePackage memory_source =
        write_calc_source_package("fastxlsx-package-editor-source-cellstore-option-memory-source.xlsx");
    memory_source.worksheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>a</t></is></c></row></sheetData></worksheet>)";
    rewrite_calc_source_package(memory_source);

    fastxlsx::detail::CellStore memory_sizing_store;
    memory_sizing_store.set_cell(1, 1, fastxlsx::CellValue::text("a"));
    fastxlsx::detail::CellStoreOptions memory_options;
    memory_options.memory_budget_bytes = memory_sizing_store.estimated_memory_usage();

    const fastxlsx::detail::PackageReader memory_source_reader =
        fastxlsx::detail::PackageReader::open(memory_source.path);
    fastxlsx::detail::CellStore memory_guarded_store =
        fastxlsx::detail::load_cell_store_from_workbook_sheet(
            memory_source_reader, "Sheet1", memory_options);
    check(memory_guarded_store.options().memory_budget_bytes
            == memory_sizing_store.estimated_memory_usage(),
        "package-backed CellStore loader should preserve memory budget options");

    bool memory_failed = false;
    try {
        memory_guarded_store.set_cell(
            1, 1, fastxlsx::CellValue::text(std::string(1024, 'x')));
    } catch (const fastxlsx::FastXlsxError&) {
        memory_failed = true;
    }
    check(memory_failed,
        "package-backed CellStore loader returned store should keep enforcing memory budget");
    const fastxlsx::detail::CellRecord* memory_guarded_cell =
        memory_guarded_store.find_cell(1, 1);
    check(memory_guarded_cell != nullptr,
        "package-backed CellStore memory failure should preserve the source-loaded cell");
    check(memory_guarded_cell->text_value == "a",
        "package-backed CellStore memory failure should preserve the source-loaded payload");
}

void test_package_editor_source_loaded_cell_store_option_failure_preserves_editor_state()
{
    const auto run_guardrail_failure_case =
        [](const char* source_name, const char* worksheet_xml,
            const fastxlsx::detail::CellStoreOptions& options,
            std::string_view expected_diagnostic) {
            CalcSourcePackage source = write_calc_source_package(source_name);
            source.worksheet = worksheet_xml;
            rewrite_calc_source_package(source);

            const fastxlsx::detail::PackageReader source_reader =
                fastxlsx::detail::PackageReader::open(source.path);
            fastxlsx::detail::PackageEditor editor =
                fastxlsx::detail::PackageEditor::open(source.path);
            const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
            const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
            const std::size_t initial_plan_size = editor.edit_plan().size();
            const std::size_t initial_note_count = editor.edit_plan().notes().size();
            const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
                editor.edit_plan().calc_chain_action();

            bool failed = false;
            try {
                (void)fastxlsx::detail::load_cell_store_from_workbook_sheet(
                    source_reader, "Sheet1", options);
            } catch (const fastxlsx::FastXlsxError& error) {
                failed = true;
                check_contains(error.what(),
                    "failed to load CellStore from workbook sheet 'Sheet1'",
                    "package-backed CellStore option failure should identify the workbook sheet");
                check_contains(error.what(), "worksheet part '/xl/worksheets/sheet1.xml'",
                    "package-backed CellStore option failure should identify the worksheet part");
                check_contains(error.what(), "ZIP entry 'xl/worksheets/sheet1.xml'",
                    "package-backed CellStore option failure should identify the worksheet ZIP entry");
                check_contains(error.what(), expected_diagnostic,
                    "package-backed CellStore option failure should preserve the guardrail diagnostic");
            }
            check(failed,
                "package-backed CellStore load should fail on load-time option guardrails");

            check(editor.edit_plan().size() == initial_plan_size,
                "package-backed CellStore option failure should not mutate edit-plan parts");
            check(editor.edit_plan().notes().size() == initial_note_count,
                "package-backed CellStore option failure should not append notes");
            check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
                "package-backed CellStore option failure should not queue calcChain removal");
            check(!editor.edit_plan().full_calculation_on_load(),
                "package-backed CellStore option failure should not request recalculation");
            check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
                "package-backed CellStore option failure should not change calcChain policy");
            check_manifest_write_mode(editor, worksheet_part,
                fastxlsx::detail::PartWriteMode::CopyOriginal,
                "package-backed CellStore option failure should keep worksheet copy-original");
            check_manifest_write_mode(editor, calc_chain_part,
                fastxlsx::detail::PartWriteMode::CopyOriginal,
                "package-backed CellStore option failure should keep calcChain copy-original");

            const fastxlsx::detail::PackageEditorOutputPlan output_plan =
                editor.planned_output();
            check(output_plan.full_calculation_on_load == false,
                "package-backed CellStore option output plan should not request recalculation");
            check(output_plan.calc_chain_action == initial_calc_chain_action,
                "package-backed CellStore option output plan should preserve calcChain policy");
            check(output_plan.notes.size() == initial_note_count,
                "package-backed CellStore option output plan should not add notes");
            check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
                fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
                "package-backed CellStore option output plan should preserve workbook");
            check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
                fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
                "package-backed CellStore option output plan should preserve workbook rels");
            check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
                fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
                "package-backed CellStore option output plan should preserve worksheet");
            check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
                fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
                "package-backed CellStore option output plan should preserve calcChain");
            check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
                fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
                "package-backed CellStore option output plan should preserve unknown bytes");
        };

    fastxlsx::detail::CellStoreOptions max_cell_options;
    max_cell_options.max_cells = 1;
    run_guardrail_failure_case(
        "fastxlsx-package-editor-source-cellstore-option-max-failure-source.xlsx",
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</v></c><c r="B1"><v>2</v></c></row></sheetData></worksheet>)",
        max_cell_options, "CellStore max_cells guardrail exceeded");

    fastxlsx::detail::CellStore memory_sizing_store;
    memory_sizing_store.set_cell(1, 1, fastxlsx::CellValue::text("a"));
    fastxlsx::detail::CellStoreOptions memory_options;
    memory_options.memory_budget_bytes = memory_sizing_store.estimated_memory_usage();
    run_guardrail_failure_case(
        "fastxlsx-package-editor-source-cellstore-option-memory-failure-source.xlsx",
        R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>a</t></is></c><c r="B1" t="inlineStr"><is><t>oversized materialized text</t></is></c></row></sheetData></worksheet>)",
        memory_options, "CellStore memory_budget_bytes guardrail exceeded");
}

void test_package_editor_source_loaded_cell_store_shared_strings_payload_failure_preserves_editor_state()
{
    struct SharedStringsPayloadFailureCase {
        const char* source_name;
        const char* output_name;
        const char* shared_strings_xml;
        const char* expected_diagnostic;
    };

    const SharedStringsPayloadFailureCase cases[] = {
        {
            "fastxlsx-package-editor-source-cellstore-wrong-namespace-sharedstrings-unsupported-item-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-wrong-namespace-sharedstrings-unsupported-item-output.xlsx",
            R"(<?xml version="1.0" encoding="UTF-8"?><bad:sst xmlns:bad="urn:fastxlsx:not-spreadsheetml" count="1" uniqueCount="1"><bad:si><bad:unsupportedItem><bad:t>bad</bad:t></bad:unsupportedItem></bad:si></bad:sst>)",
            "unsupported shared string item element",
        },
        {
            "fastxlsx-package-editor-source-cellstore-wrong-namespace-sharedstrings-unsupported-rich-run-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-wrong-namespace-sharedstrings-unsupported-rich-run-output.xlsx",
            R"(<?xml version="1.0" encoding="UTF-8"?><bad:sst xmlns:bad="urn:fastxlsx:not-spreadsheetml" count="1" uniqueCount="1"><bad:si><bad:r><bad:unsupportedRun><bad:t>bad</bad:t></bad:unsupportedRun></bad:r></bad:si></bad:sst>)",
            "unsupported shared string rich text element",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-mixed-direct-rich-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-mixed-direct-rich-output.xlsx",
            R"(<?xml version="1.0" encoding="UTF-8"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><t>direct</t><r><t>rich</t></r></si></sst>)",
            "mixed direct and rich text in a shared string item",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-rpr-outside-run-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-rpr-outside-run-output.xlsx",
            R"(<?xml version="1.0" encoding="UTF-8"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><rPr><b/></rPr></si></sst>)",
            "malformed rich text metadata",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-text-inside-rpr-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-text-inside-rpr-output.xlsx",
            R"(<?xml version="1.0" encoding="UTF-8"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><r><rPr><t>not-text</t></rPr><t>rich</t></r></si></sst>)",
            "malformed rich text metadata",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-ignored-nested-si-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-ignored-nested-si-output.xlsx",
            R"(<?xml version="1.0" encoding="UTF-8"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><rPh sb="0" eb="1"><si><t>decoy</t></si></rPh><t>real</t></si></sst>)",
            "nested shared string item",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-ignored-nested-markup-in-text-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-ignored-nested-markup-in-text-output.xlsx",
            R"(<?xml version="1.0" encoding="UTF-8"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><rPh sb="0" eb="1"><t>ignored<r/>text</t></rPh><t>real</t></si></sst>)",
            "nested markup inside a text element",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-ignored-orphan-closing-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-ignored-orphan-closing-output.xlsx",
            R"(<?xml version="1.0" encoding="UTF-8"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><t>bad</t></rPh></si></sst>)",
            "mismatched closing tags",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-ignored-unclosed-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-ignored-unclosed-output.xlsx",
            R"(<?xml version="1.0" encoding="UTF-8"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><rPh sb="0" eb="1"><t>ignored</t>)",
            "malformed XML",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-comment-inside-text-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-comment-inside-text-output.xlsx",
            R"(<?xml version="1.0" encoding="UTF-8"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><t>bad<!--hidden-->text</t></si></sst>)",
            "nested markup inside a text element",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-processing-instruction-inside-text-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-processing-instruction-inside-text-output.xlsx",
            R"(<?xml version="1.0" encoding="UTF-8"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><t>bad<?fx hidden?>text</t></si></sst>)",
            "nested markup inside a text element",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-malformed-pi-before-root-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-malformed-pi-before-root-output.xlsx",
            R"(<?fastxlsx broken><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><t>real</t></si></sst>)",
            "malformed processing instruction",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-malformed-pi-inside-item-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-malformed-pi-inside-item-output.xlsx",
            R"(<?xml version="1.0" encoding="UTF-8"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><?fastxlsx broken><t>real</t></si></sst>)",
            "malformed processing instruction",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-empty-pi-target-before-root-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-empty-pi-target-before-root-output.xlsx",
            R"(<? ?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><t>real</t></si></sst>)",
            "malformed processing instruction",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-empty-pi-target-inside-item-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-empty-pi-target-inside-item-output.xlsx",
            R"(<?xml version="1.0" encoding="UTF-8"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><? ?><t>real</t></si></sst>)",
            "malformed processing instruction",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-invalid-pi-target-start-before-root-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-invalid-pi-target-start-before-root-output.xlsx",
            R"(<?-bad?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><t>real</t></si></sst>)",
            "malformed processing instruction",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-invalid-pi-target-start-inside-item-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-invalid-pi-target-start-inside-item-output.xlsx",
            R"(<?xml version="1.0" encoding="UTF-8"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><?-bad?><t>real</t></si></sst>)",
            "malformed processing instruction",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-pi-target-missing-separator-before-root-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-pi-target-missing-separator-before-root-output.xlsx",
            R"(<?target?data?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><t>real</t></si></sst>)",
            "malformed processing instruction",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-pi-target-missing-separator-inside-item-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-pi-target-missing-separator-inside-item-output.xlsx",
            R"(<?xml version="1.0" encoding="UTF-8"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><?target?data?><t>real</t></si></sst>)",
            "malformed processing instruction",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-invalid-pi-target-char-before-root-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-invalid-pi-target-char-before-root-output.xlsx",
            R"(<?bad^name?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><t>real</t></si></sst>)",
            "malformed processing instruction",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-invalid-pi-target-char-inside-item-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-invalid-pi-target-char-inside-item-output.xlsx",
            R"(<?xml version="1.0" encoding="UTF-8"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><?bad^name?><t>real</t></si></sst>)",
            "malformed processing instruction",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-declaration-inside-item-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-declaration-inside-item-output.xlsx",
            R"(<?xml version="1.0" encoding="UTF-8"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><?xml version="1.0"?><t>real</t></si></sst>)",
            "XML declaration after sharedStrings root start",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-duplicate-xml-declaration-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-duplicate-xml-declaration-output.xlsx",
            R"(<?xml version="1.0" encoding="UTF-8"?><?xml version="1.0"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><t>real</t></si></sst>)",
            "duplicate XML declaration",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-like-pi-before-root-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-like-pi-before-root-output.xlsx",
            R"(<?XML version="1.0"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><t>real</t></si></sst>)",
            "reserved XML processing instruction target",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-like-pi-inside-item-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-like-pi-inside-item-output.xlsx",
            R"(<?xml version="1.0" encoding="UTF-8"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><?Xml version="1.0"?><t>real</t></si></sst>)",
            "reserved XML processing instruction target",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-declaration-missing-version-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-declaration-missing-version-output.xlsx",
            R"(<?xml?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><t>real</t></si></sst>)",
            "malformed XML declaration",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-declaration-unsupported-version-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-declaration-unsupported-version-output.xlsx",
            R"(<?xml version="2.0" encoding="UTF-8"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><t>real</t></si></sst>)",
            "unsupported XML declaration version",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-declaration-duplicate-encoding-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-declaration-duplicate-encoding-output.xlsx",
            R"(<?xml version="1.0" encoding="UTF-8" encoding="UTF-16"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><t>real</t></si></sst>)",
            "malformed XML declaration",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-declaration-unknown-attribute-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-declaration-unknown-attribute-output.xlsx",
            R"(<?xml version="1.0" flavor="fastxlsx"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><t>real</t></si></sst>)",
            "malformed XML declaration",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-declaration-encoding-after-standalone-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-declaration-encoding-after-standalone-output.xlsx",
            R"(<?xml version="1.0" standalone="yes" encoding="UTF-8"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><t>real</t></si></sst>)",
            "malformed XML declaration",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-declaration-duplicate-standalone-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-declaration-duplicate-standalone-output.xlsx",
            R"(<?xml version="1.0" standalone="yes" standalone="no"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><t>real</t></si></sst>)",
            "malformed XML declaration",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-declaration-empty-standalone-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-declaration-empty-standalone-output.xlsx",
            R"(<?xml version="1.0" standalone=""?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><t>real</t></si></sst>)",
            "malformed XML declaration",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-declaration-invalid-standalone-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-declaration-invalid-standalone-output.xlsx",
            R"(<?xml version="1.0" standalone="maybe"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><t>real</t></si></sst>)",
            "malformed XML declaration",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-declaration-empty-encoding-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-declaration-empty-encoding-output.xlsx",
            R"(<?xml version="1.0" encoding=""?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><t>real</t></si></sst>)",
            "malformed XML declaration",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-declaration-digit-start-encoding-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-declaration-digit-start-encoding-output.xlsx",
            R"(<?xml version="1.0" encoding="8BIT"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><t>real</t></si></sst>)",
            "malformed XML declaration",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-declaration-invalid-encoding-character-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-declaration-invalid-encoding-character-output.xlsx",
            R"(<?xml version="1.0" encoding="UTF:8"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><t>real</t></si></sst>)",
            "malformed XML declaration",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-comment-before-xml-declaration-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-comment-before-xml-declaration-output.xlsx",
            R"(<!--before-declaration--><?xml version="1.0" encoding="UTF-8"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><t>real</t></si></sst>)",
            "XML declaration after sharedStrings prolog markup",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-pi-before-xml-declaration-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-pi-before-xml-declaration-output.xlsx",
            R"(<?fastxlsx before-declaration?><?xml version="1.0" encoding="UTF-8"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><t>real</t></si></sst>)",
            "XML declaration after sharedStrings prolog markup",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-whitespace-before-xml-declaration-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-whitespace-before-xml-declaration-output.xlsx",
            " \r\n\t"
            R"(<?xml version="1.0" encoding="UTF-8"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><t>real</t></si></sst>)",
            "XML declaration after sharedStrings prolog text",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-declaration-after-root-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-xml-declaration-after-root-output.xlsx",
            R"(<?xml version="1.0" encoding="UTF-8"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><t>real</t></si></sst><?xml version="1.0"?>)",
            "XML declaration after sharedStrings root start",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-cdata-inside-text-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-cdata-inside-text-output.xlsx",
            R"(<?xml version="1.0" encoding="UTF-8"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><t><![CDATA[hidden]]></t></si></sst>)",
            "nested markup inside a text element",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sharedstrings-cdata-outside-text-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sharedstrings-cdata-outside-text-output.xlsx",
            R"(<?xml version="1.0" encoding="UTF-8"?><sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><![CDATA[hidden]]><t>real</t></si></sst>)",
            "unsupported markup declaration",
        },
    };

    for (const SharedStringsPayloadFailureCase& test_case : cases) {
        const std::filesystem::path source_path = output_path(test_case.source_name);
        {
            fastxlsx::WorkbookWriterOptions options;
            options.string_strategy = fastxlsx::StringStrategy::SharedString;
            fastxlsx::WorkbookWriter writer =
                fastxlsx::WorkbookWriter::create(source_path, options);
            fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
            data.append_row({fastxlsx::CellView::text("sharedstrings-payload-failure")});
            fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
            untouched.append_row({fastxlsx::CellView::text("keep-sharedstrings-payload-failure")});
            writer.close();
        }
        rewrite_package_entry_as_stored(
            source_path, "xl/sharedStrings.xml", test_case.shared_strings_xml);

        const std::filesystem::path output = output_path(test_case.output_name);
        const std::map<std::string, std::string> source_entries =
            fastxlsx::test::read_zip_entries(source_path);
        const fastxlsx::detail::PackageReader source_reader =
            fastxlsx::detail::PackageReader::open(source_path);
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source_path);
        const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
        const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
        const std::size_t initial_plan_size = editor.edit_plan().size();
        const std::size_t initial_note_count = editor.edit_plan().notes().size();
        const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
            editor.edit_plan().calc_chain_action();

        bool failed = false;
        try {
            (void)fastxlsx::detail::load_cell_store_from_workbook_sheet(
                source_reader, "Data");
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "failed to load CellStore from workbook sheet 'Data'",
                "package-backed sharedStrings payload failure should identify the workbook sheet");
            check_contains(error.what(), "worksheet part '/xl/worksheets/sheet1.xml'",
                "package-backed sharedStrings payload failure should identify the worksheet part");
            check_contains(error.what(), "ZIP entry 'xl/worksheets/sheet1.xml'",
                "package-backed sharedStrings payload failure should identify the worksheet ZIP entry");
            check_contains(error.what(), "failed to load workbook sharedStrings part '/xl/sharedStrings.xml'",
                "package-backed sharedStrings payload failure should identify the sharedStrings part");
            check_contains(error.what(), test_case.expected_diagnostic,
                "package-backed sharedStrings payload failure should preserve the parser diagnostic");
        }
        check(failed,
            "package-backed CellStore load should fail on unsupported sharedStrings local-names");

        check(editor.edit_plan().size() == initial_plan_size,
            "package-backed sharedStrings payload failure should not mutate edit-plan parts");
        check(editor.edit_plan().notes().size() == initial_note_count,
            "package-backed sharedStrings payload failure should not append notes");
        check(!editor.edit_plan().full_calculation_on_load(),
            "package-backed sharedStrings payload failure should not request recalculation");
        check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
            "package-backed sharedStrings payload failure should not change calcChain policy");
        check_manifest_write_mode(editor, worksheet_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "package-backed sharedStrings payload failure should keep worksheet copy-original");
        check_manifest_write_mode(editor, shared_strings_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "package-backed sharedStrings payload failure should keep sharedStrings copy-original");

        editor.save_as(output);
        check(fastxlsx::test::read_zip_entries(output) == source_entries,
            "package-backed sharedStrings payload failure output should preserve source entries");
    }
}

void test_package_editor_source_loaded_cell_store_source_shape_failure_preserves_editor_state()
{
    struct SourceShapeFailureCase {
        const char* source_name;
        const char* output_name;
        const char* worksheet_xml;
        const char* expected_diagnostic;
    };

    const SourceShapeFailureCase cases[] = {
        {
            "fastxlsx-package-editor-source-cellstore-duplicate-cell-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-duplicate-cell-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</v></c><c r="A1"><v>2</v></c></row></sheetData></worksheet>)",
            "out-of-order cell references",
        },
        {
            "fastxlsx-package-editor-source-cellstore-duplicate-row-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-duplicate-row-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</v></c></row><row r="1"><c r="B1"><v>2</v></c></row></sheetData></worksheet>)",
            "duplicate row numbers",
        },
        {
            "fastxlsx-package-editor-source-cellstore-out-of-order-row-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-out-of-order-row-output.xlsx",
            R"(<worksheet><sheetData><row r="2"><c r="A2"><v>2</v></c></row><row r="1"><c r="A1"><v>1</v></c></row></sheetData></worksheet>)",
            "out-of-order row numbers",
        },
        {
            "fastxlsx-package-editor-source-cellstore-out-of-order-cell-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-out-of-order-cell-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="B1"><v>2</v></c><c r="A1"><v>1</v></c></row></sheetData></worksheet>)",
            "out-of-order cell references",
        },
    };

    for (const SourceShapeFailureCase& test_case : cases) {
        CalcSourcePackage source = write_calc_source_package(test_case.source_name);
        source.worksheet = test_case.worksheet_xml;
        rewrite_calc_source_package(source);
        const std::filesystem::path output = output_path(test_case.output_name);

        const fastxlsx::detail::PackageReader source_reader =
            fastxlsx::detail::PackageReader::open(source.path);
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);
        const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
        const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
        const std::size_t initial_plan_size = editor.edit_plan().size();
        const std::size_t initial_note_count = editor.edit_plan().notes().size();
        const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
            editor.edit_plan().calc_chain_action();

        bool failed = false;
        try {
            (void)fastxlsx::detail::load_cell_store_from_workbook_sheet(
                source_reader, "Sheet1");
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "failed to load CellStore from workbook sheet 'Sheet1'",
                "package-backed CellStore source-shape failure should identify the workbook sheet");
            check_contains(error.what(), "worksheet part '/xl/worksheets/sheet1.xml'",
                "package-backed CellStore source-shape failure should identify the worksheet part");
            check_contains(error.what(), "ZIP entry 'xl/worksheets/sheet1.xml'",
                "package-backed CellStore source-shape failure should identify the worksheet ZIP entry");
            check_contains(error.what(), test_case.expected_diagnostic,
                "package-backed CellStore source-shape failure should preserve the loader diagnostic");
        }
        check(failed,
            "package-backed CellStore load should fail on unsupported source worksheet shape");

        check(editor.edit_plan().size() == initial_plan_size,
            "package-backed CellStore source-shape failure should not mutate edit-plan parts");
        check(editor.edit_plan().notes().size() == initial_note_count,
            "package-backed CellStore source-shape failure should not append notes");
        check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
            "package-backed CellStore source-shape failure should not queue calcChain removal");
        check(!editor.edit_plan().full_calculation_on_load(),
            "package-backed CellStore source-shape failure should not request recalculation");
        check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
            "package-backed CellStore source-shape failure should not change calcChain policy");
        check_manifest_write_mode(editor, worksheet_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "package-backed CellStore source-shape failure should keep worksheet copy-original");
        check_manifest_write_mode(editor, calc_chain_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "package-backed CellStore source-shape failure should keep calcChain copy-original");

        editor.save_as(output);
        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(output);
        check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
            "package-backed CellStore source-shape output should preserve workbook bytes");
        check(output_reader.read_entry("xl/_rels/workbook.xml.rels") == source.workbook_relationships,
            "package-backed CellStore source-shape output should preserve workbook rels bytes");
        check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
            "package-backed CellStore source-shape output should preserve worksheet bytes");
        check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
            "package-backed CellStore source-shape output should preserve calcChain bytes");
        check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
            "package-backed CellStore source-shape output should preserve unknown bytes");
    }
}

void test_package_editor_source_loaded_cell_store_metadata_shape_failure_preserves_editor_state()
{
    struct MetadataShapeFailureCase {
        const char* source_name;
        const char* output_name;
        const char* worksheet_xml;
        const char* expected_diagnostic;
    };

    const MetadataShapeFailureCase cases[] = {
        {
            "fastxlsx-package-editor-source-cellstore-row-metadata-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-row-metadata-output.xlsx",
            R"(<worksheet><sheetData><row r="1" unsupportedRowMetadata="1"><c r="A1"><v>1</v></c></row></sheetData></worksheet>)",
            "row metadata attributes",
        },
        {
            "fastxlsx-package-editor-source-cellstore-cell-metadata-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-cell-metadata-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1" cm="1"><v>1</v></c></row></sheetData></worksheet>)",
            "cell metadata attributes",
        },
        {
            "fastxlsx-package-editor-source-cellstore-scalar-attr-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-scalar-attr-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1"><v foo="1">1</v></c></row></sheetData></worksheet>)",
            "scalar value attributes",
        },
        {
            "fastxlsx-package-editor-source-cellstore-duplicate-formula-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-duplicate-formula-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1"><f>1</f><f>2</f></c></row></sheetData></worksheet>)",
            "duplicate formula elements",
        },
        {
            "fastxlsx-package-editor-source-cellstore-empty-formula-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-empty-formula-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1"><f/></c></row></sheetData></worksheet>)",
            "empty formula text",
        },
        {
            "fastxlsx-package-editor-source-cellstore-formula-attr-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-formula-attr-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1"><f foo="1">1</f></c></row></sheetData></worksheet>)",
            "formula attributes",
        },
        {
            "fastxlsx-package-editor-source-cellstore-inline-text-attr-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-inline-text-attr-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t foo="1">a</t></is></c></row></sheetData></worksheet>)",
            "inline text attributes",
        },
        {
            "fastxlsx-package-editor-source-cellstore-inline-direct-plus-rich-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-inline-direct-plus-rich-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>direct-</t><r><t>rich</t></r></is></c></row></sheetData></worksheet>)",
            "duplicate inline text elements",
        },
        {
            "fastxlsx-package-editor-source-cellstore-inline-rpr-outside-run-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-inline-rpr-outside-run-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><rPr><b/></rPr></is></c></row></sheetData></worksheet>)",
            "malformed inline rich text metadata",
        },
        {
            "fastxlsx-package-editor-source-cellstore-inline-text-inside-rpr-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-inline-text-inside-rpr-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><r><rPr><t>not-text</t></rPr><t>rich</t></r></is></c></row></sheetData></worksheet>)",
            "malformed inline rich text metadata",
        },
        {
            "fastxlsx-package-editor-source-cellstore-inline-ignored-nested-si-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-inline-ignored-nested-si-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><rPh sb="0" eb="1"><si><t>decoy</t></si></rPh><t>real</t></is></c></row></sheetData></worksheet>)",
            "malformed inline rich text metadata",
        },
        {
            "fastxlsx-package-editor-source-cellstore-inline-ignored-nested-markup-in-text-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-inline-ignored-nested-markup-in-text-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><rPh sb="0" eb="1"><t>ignored<r/>text</t></rPh><t>real</t></is></c></row></sheetData></worksheet>)",
            "malformed inline rich text metadata",
        },
        {
            "fastxlsx-package-editor-source-cellstore-inline-ignored-orphan-closing-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-inline-ignored-orphan-closing-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>real</t></rPh></is></c></row></sheetData></worksheet>)",
            "malformed inline rich text metadata",
        },
        {
            "fastxlsx-package-editor-source-cellstore-inline-ignored-unclosed-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-inline-ignored-unclosed-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><rPh sb="0" eb="1"><t>ignored</t></is></c></row></sheetData></worksheet>)",
            "malformed inline rich text metadata",
        },
        {
            "fastxlsx-package-editor-source-cellstore-inline-unknown-metadata-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-inline-unknown-metadata-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><unknownInlineMetadata/></is></c></row></sheetData></worksheet>)",
            "unsupported inline string metadata",
        },
        {
            "fastxlsx-package-editor-source-cellstore-wrong-namespace-unknown-cell-metadata-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-wrong-namespace-unknown-cell-metadata-output.xlsx",
            R"(<bad:worksheet xmlns:bad="urn:fastxlsx:not-spreadsheetml"><bad:sheetData><bad:row r="1"><bad:c r="A1"><bad:unsupportedCellMetadata/></bad:c></bad:row></bad:sheetData></bad:worksheet>)",
            "unsupported cell metadata",
        },
        {
            "fastxlsx-package-editor-source-cellstore-wrong-namespace-unknown-inline-metadata-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-wrong-namespace-unknown-inline-metadata-output.xlsx",
            R"(<bad:worksheet xmlns:bad="urn:fastxlsx:not-spreadsheetml"><bad:sheetData><bad:row r="1"><bad:c r="A1" t="inlineStr"><bad:is><bad:unsupportedInlineMetadata/></bad:is></bad:c></bad:row></bad:sheetData></bad:worksheet>)",
            "unsupported inline string metadata",
        },
    };

    for (const MetadataShapeFailureCase& test_case : cases) {
        CalcSourcePackage source = write_calc_source_package(test_case.source_name);
        source.worksheet = test_case.worksheet_xml;
        rewrite_calc_source_package(source);
        const std::filesystem::path output = output_path(test_case.output_name);

        const fastxlsx::detail::PackageReader source_reader =
            fastxlsx::detail::PackageReader::open(source.path);
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);
        const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
        const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
        const std::size_t initial_plan_size = editor.edit_plan().size();
        const std::size_t initial_note_count = editor.edit_plan().notes().size();
        const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
            editor.edit_plan().calc_chain_action();

        bool failed = false;
        try {
            (void)fastxlsx::detail::load_cell_store_from_workbook_sheet(
                source_reader, "Sheet1");
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "failed to load CellStore from workbook sheet 'Sheet1'",
                "package-backed CellStore metadata-shape failure should identify the workbook sheet");
            check_contains(error.what(), "worksheet part '/xl/worksheets/sheet1.xml'",
                "package-backed CellStore metadata-shape failure should identify the worksheet part");
            check_contains(error.what(), "ZIP entry 'xl/worksheets/sheet1.xml'",
                "package-backed CellStore metadata-shape failure should identify the worksheet ZIP entry");
            check_contains(error.what(), test_case.expected_diagnostic,
                "package-backed CellStore metadata-shape failure should preserve the loader diagnostic");
        }
        check(failed,
            "package-backed CellStore load should fail on unsupported source worksheet metadata shape");

        check(editor.edit_plan().size() == initial_plan_size,
            "package-backed CellStore metadata-shape failure should not mutate edit-plan parts");
        check(editor.edit_plan().notes().size() == initial_note_count,
            "package-backed CellStore metadata-shape failure should not append notes");
        check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
            "package-backed CellStore metadata-shape failure should not queue calcChain removal");
        check(!editor.edit_plan().full_calculation_on_load(),
            "package-backed CellStore metadata-shape failure should not request recalculation");
        check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
            "package-backed CellStore metadata-shape failure should not change calcChain policy");
        check_manifest_write_mode(editor, worksheet_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "package-backed CellStore metadata-shape failure should keep worksheet copy-original");
        check_manifest_write_mode(editor, calc_chain_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "package-backed CellStore metadata-shape failure should keep calcChain copy-original");

        editor.save_as(output);
        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(output);
        check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
            "package-backed CellStore metadata-shape output should preserve workbook bytes");
        check(output_reader.read_entry("xl/_rels/workbook.xml.rels") == source.workbook_relationships,
            "package-backed CellStore metadata-shape output should preserve workbook rels bytes");
        check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
            "package-backed CellStore metadata-shape output should preserve worksheet bytes");
        check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
            "package-backed CellStore metadata-shape output should preserve calcChain bytes");
        check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
            "package-backed CellStore metadata-shape output should preserve unknown bytes");
    }
}

void test_package_editor_source_loaded_cell_store_payload_failure_preserves_editor_state()
{
    struct PayloadFailureCase {
        const char* source_name;
        const char* output_name;
        const char* worksheet_xml;
        const char* expected_diagnostic;
    };

    const PayloadFailureCase cases[] = {
        {
            "fastxlsx-package-editor-source-cellstore-shared-string-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-shared-string-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1" t="s"><v>0</v></c></row></sheetData></worksheet>)",
            "shared string indexes",
        },
        {
            "fastxlsx-package-editor-source-cellstore-unsupported-type-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-unsupported-type-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1" t="z"><v>1</v></c></row></sheetData></worksheet>)",
            "unsupported cell type",
        },
        {
            "fastxlsx-package-editor-source-cellstore-invalid-boolean-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-invalid-boolean-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1" t="b"><v>2</v></c></row></sheetData></worksheet>)",
            "invalid boolean cell value",
        },
        {
            "fastxlsx-package-editor-source-cellstore-duplicate-scalar-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-duplicate-scalar-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</v><v>2</v></c></row></sheetData></worksheet>)",
            "duplicate scalar value elements",
        },
        {
            "fastxlsx-package-editor-source-cellstore-duplicate-inline-text-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-duplicate-inline-text-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>a</t><t>b</t></is></c></row></sheetData></worksheet>)",
            "duplicate inline text elements",
        },
        {
            "fastxlsx-package-editor-source-cellstore-cell-comment-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-cell-comment-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>a<!--hidden-->b</t></is></c></row></sheetData></worksheet>)",
            "cell comments, processing instructions, or unsupported markup",
        },
        {
            "fastxlsx-package-editor-source-cellstore-cell-pi-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-cell-pi-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>a<?fx hidden?>b</t></is></c></row></sheetData></worksheet>)",
            "cell comments, processing instructions, or unsupported markup",
        },
        {
            "fastxlsx-package-editor-source-cellstore-cell-cdata-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-cell-cdata-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t><![CDATA[hidden]]></t></is></c></row></sheetData></worksheet>)",
            "cell comments, processing instructions, or unsupported markup",
        },
        {
            "fastxlsx-package-editor-source-cellstore-nested-cell-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-nested-cell-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1"><c r="B1"><v>1</v></c></c></row></sheetData></worksheet>)",
            "invalid cell boundary",
        },
        {
            "fastxlsx-package-editor-source-cellstore-cell-outside-row-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-cell-outside-row-output.xlsx",
            R"(<worksheet><sheetData><c r="A1"><v>1</v></c></sheetData></worksheet>)",
            "cell outside row",
        },
    };

    for (const PayloadFailureCase& test_case : cases) {
        CalcSourcePackage source = write_calc_source_package(test_case.source_name);
        source.worksheet = test_case.worksheet_xml;
        rewrite_calc_source_package(source);
        const std::filesystem::path output = output_path(test_case.output_name);

        const fastxlsx::detail::PackageReader source_reader =
            fastxlsx::detail::PackageReader::open(source.path);
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);
        const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
        const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
        const std::size_t initial_plan_size = editor.edit_plan().size();
        const std::size_t initial_note_count = editor.edit_plan().notes().size();
        const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
            editor.edit_plan().calc_chain_action();

        bool failed = false;
        try {
            (void)fastxlsx::detail::load_cell_store_from_workbook_sheet(
                source_reader, "Sheet1");
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "failed to load CellStore from workbook sheet 'Sheet1'",
                "package-backed CellStore payload failure should identify the workbook sheet");
            check_contains(error.what(), "worksheet part '/xl/worksheets/sheet1.xml'",
                "package-backed CellStore payload failure should identify the worksheet part");
            check_contains(error.what(), "ZIP entry 'xl/worksheets/sheet1.xml'",
                "package-backed CellStore payload failure should identify the worksheet ZIP entry");
            if (std::string_view(error.what()).find(test_case.expected_diagnostic)
                == std::string_view::npos) {
                throw TestFailure(std::string(
                                      "package-backed CellStore payload failure diagnostic mismatch for ")
                    + test_case.source_name + ": " + error.what());
            }
        }
        check(failed,
            "package-backed CellStore load should fail on unsupported source cell payloads");

        check(editor.edit_plan().size() == initial_plan_size,
            "package-backed CellStore payload failure should not mutate edit-plan parts");
        check(editor.edit_plan().notes().size() == initial_note_count,
            "package-backed CellStore payload failure should not append notes");
        check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
            "package-backed CellStore payload failure should not queue calcChain removal");
        check(!editor.edit_plan().full_calculation_on_load(),
            "package-backed CellStore payload failure should not request recalculation");
        check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
            "package-backed CellStore payload failure should not change calcChain policy");
        check_manifest_write_mode(editor, worksheet_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "package-backed CellStore payload failure should keep worksheet copy-original");
        check_manifest_write_mode(editor, calc_chain_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "package-backed CellStore payload failure should keep calcChain copy-original");

        editor.save_as(output);
        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(output);
        check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
            "package-backed CellStore payload output should preserve workbook bytes");
        check(output_reader.read_entry("xl/_rels/workbook.xml.rels") == source.workbook_relationships,
            "package-backed CellStore payload output should preserve workbook rels bytes");
        check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
            "package-backed CellStore payload output should preserve worksheet bytes");
        check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
            "package-backed CellStore payload output should preserve calcChain bytes");
        check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
            "package-backed CellStore payload output should preserve unknown bytes");
    }
}

void test_package_editor_source_loaded_cell_store_reference_failure_preserves_editor_state()
{
    struct ReferenceFailureCase {
        const char* source_name;
        const char* output_name;
        const char* worksheet_xml;
        const char* expected_diagnostic;
    };

    const ReferenceFailureCase cases[] = {
        {
            "fastxlsx-package-editor-source-cellstore-missing-cell-ref-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-missing-cell-ref-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c><v>1</v></c></row></sheetData></worksheet>)",
            "requires explicit cell references",
        },
        {
            "fastxlsx-package-editor-source-cellstore-column-overflow-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-column-overflow-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="XFE1"><v>1</v></c></row></sheetData></worksheet>)",
            "cell column exceeds Excel limits",
        },
        {
            "fastxlsx-package-editor-source-cellstore-zero-cell-row-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-zero-cell-row-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A0"><v>1</v></c></row></sheetData></worksheet>)",
            "invalid cell reference",
        },
        {
            "fastxlsx-package-editor-source-cellstore-cell-row-overflow-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-cell-row-overflow-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1048577"><v>1</v></c></row></sheetData></worksheet>)",
            "cell row exceeds Excel limits",
        },
        {
            "fastxlsx-package-editor-source-cellstore-invalid-cell-ref-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-invalid-cell-ref-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="1A"><v>1</v></c></row></sheetData></worksheet>)",
            "invalid cell reference",
        },
        {
            "fastxlsx-package-editor-source-cellstore-row-cell-mismatch-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-row-cell-mismatch-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="B2"><v>2</v></c></row></sheetData></worksheet>)",
            "row and cell reference do not match",
        },
        {
            "fastxlsx-package-editor-source-cellstore-zero-row-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-zero-row-output.xlsx",
            R"(<worksheet><sheetData><row r="0"><c r="A1"><v>1</v></c></row></sheetData></worksheet>)",
            "row must be one-based",
        },
        {
            "fastxlsx-package-editor-source-cellstore-row-overflow-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-row-overflow-output.xlsx",
            R"(<worksheet><sheetData><row r="1048577"><c r="A1048577"><v>1</v></c></row></sheetData></worksheet>)",
            "row exceeds Excel limits",
        },
        {
            "fastxlsx-package-editor-source-cellstore-invalid-row-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-invalid-row-output.xlsx",
            R"(<worksheet><sheetData><row r="bad"><c r="A1"><v>1</v></c></row></sheetData></worksheet>)",
            "invalid row number",
        },
        {
            "fastxlsx-package-editor-source-cellstore-formula-inline-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-formula-inline-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><f>1</f></c></row></sheetData></worksheet>)",
            "formula in an unsupported cell type",
        },
        {
            "fastxlsx-package-editor-source-cellstore-nonfinite-number-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-nonfinite-number-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1e999</v></c></row></sheetData></worksheet>)",
            "invalid numeric cell value",
        },
    };

    for (const ReferenceFailureCase& test_case : cases) {
        CalcSourcePackage source = write_calc_source_package(test_case.source_name);
        source.worksheet = test_case.worksheet_xml;
        rewrite_calc_source_package(source);
        const std::filesystem::path output = output_path(test_case.output_name);

        const fastxlsx::detail::PackageReader source_reader =
            fastxlsx::detail::PackageReader::open(source.path);
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);
        const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
        const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
        const std::size_t initial_plan_size = editor.edit_plan().size();
        const std::size_t initial_note_count = editor.edit_plan().notes().size();
        const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
            editor.edit_plan().calc_chain_action();

        bool failed = false;
        try {
            (void)fastxlsx::detail::load_cell_store_from_workbook_sheet(
                source_reader, "Sheet1");
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "failed to load CellStore from workbook sheet 'Sheet1'",
                "package-backed CellStore reference failure should identify the workbook sheet");
            check_contains(error.what(), "worksheet part '/xl/worksheets/sheet1.xml'",
                "package-backed CellStore reference failure should identify the worksheet part");
            check_contains(error.what(), "ZIP entry 'xl/worksheets/sheet1.xml'",
                "package-backed CellStore reference failure should identify the worksheet ZIP entry");
            if (std::string_view(error.what()).find(test_case.expected_diagnostic)
                == std::string_view::npos) {
                throw TestFailure(std::string(
                                      "package-backed CellStore reference failure diagnostic mismatch for ")
                    + test_case.source_name + ": " + error.what());
            }
        }
        check(failed,
            "package-backed CellStore load should fail on invalid source coordinates or numeric semantics");

        check(editor.edit_plan().size() == initial_plan_size,
            "package-backed CellStore reference failure should not mutate edit-plan parts");
        check(editor.edit_plan().notes().size() == initial_note_count,
            "package-backed CellStore reference failure should not append notes");
        check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
            "package-backed CellStore reference failure should not queue calcChain removal");
        check(!editor.edit_plan().full_calculation_on_load(),
            "package-backed CellStore reference failure should not request recalculation");
        check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
            "package-backed CellStore reference failure should not change calcChain policy");
        check_manifest_write_mode(editor, worksheet_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "package-backed CellStore reference failure should keep worksheet copy-original");
        check_manifest_write_mode(editor, calc_chain_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "package-backed CellStore reference failure should keep calcChain copy-original");

        editor.save_as(output);
        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(output);
        check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
            "package-backed CellStore reference output should preserve workbook bytes");
        check(output_reader.read_entry("xl/_rels/workbook.xml.rels") == source.workbook_relationships,
            "package-backed CellStore reference output should preserve workbook rels bytes");
        check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
            "package-backed CellStore reference output should preserve worksheet bytes");
        check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
            "package-backed CellStore reference output should preserve calcChain bytes");
        check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
            "package-backed CellStore reference output should preserve unknown bytes");
    }
}

void test_package_editor_source_loaded_cell_store_attribute_failure_preserves_editor_state()
{
    struct AttributeFailureCase {
        const char* source_name;
        const char* output_name;
        const char* worksheet_xml;
        const char* expected_diagnostic;
    };

    const AttributeFailureCase cases[] = {
        {
            "fastxlsx-package-editor-source-cellstore-unquoted-cell-ref-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-unquoted-cell-ref-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r=A1><v>1</v></c></row></sheetData></worksheet>)",
            "unquoted attribute value",
        },
        {
            "fastxlsx-package-editor-source-cellstore-unterminated-cell-ref-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-unterminated-cell-ref-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1><v>1</v></c></row></sheetData></worksheet>)",
            "unterminated markup",
        },
        {
            "fastxlsx-package-editor-source-cellstore-duplicate-cell-ref-attr-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-duplicate-cell-ref-attr-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1" r="B1"><v>1</v></c></row></sheetData></worksheet>)",
            "duplicate attributes",
        },
        {
            "fastxlsx-package-editor-source-cellstore-duplicate-row-ref-attr-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-duplicate-row-ref-attr-output.xlsx",
            R"(<worksheet><sheetData><row r="1" r="2"><c r="A1"><v>1</v></c></row></sheetData></worksheet>)",
            "duplicate attributes",
        },
        {
            "fastxlsx-package-editor-source-cellstore-duplicate-cell-type-attr-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-duplicate-cell-type-attr-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1" t="n" t="b"><v>1</v></c></row></sheetData></worksheet>)",
            "duplicate attributes",
        },
    };

    for (const AttributeFailureCase& test_case : cases) {
        CalcSourcePackage source = write_calc_source_package(test_case.source_name);
        source.worksheet = test_case.worksheet_xml;
        rewrite_calc_source_package(source);
        const std::filesystem::path output = output_path(test_case.output_name);

        const fastxlsx::detail::PackageReader source_reader =
            fastxlsx::detail::PackageReader::open(source.path);
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);
        const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
        const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
        const std::size_t initial_plan_size = editor.edit_plan().size();
        const std::size_t initial_note_count = editor.edit_plan().notes().size();
        const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
            editor.edit_plan().calc_chain_action();

        bool failed = false;
        try {
            (void)fastxlsx::detail::load_cell_store_from_workbook_sheet(
                source_reader, "Sheet1");
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "failed to load CellStore from workbook sheet 'Sheet1'",
                "package-backed CellStore attribute failure should identify the workbook sheet");
            check_contains(error.what(), "worksheet part '/xl/worksheets/sheet1.xml'",
                "package-backed CellStore attribute failure should identify the worksheet part");
            check_contains(error.what(), "ZIP entry 'xl/worksheets/sheet1.xml'",
                "package-backed CellStore attribute failure should identify the worksheet ZIP entry");
            if (std::string_view(error.what()).find(test_case.expected_diagnostic)
                == std::string_view::npos) {
                throw TestFailure(std::string(
                                      "package-backed CellStore attribute failure diagnostic mismatch for ")
                    + test_case.source_name + ": " + error.what());
            }
        }
        check(failed,
            "package-backed CellStore load should fail on malformed or duplicate source attributes");

        check(editor.edit_plan().size() == initial_plan_size,
            "package-backed CellStore attribute failure should not mutate edit-plan parts");
        check(editor.edit_plan().notes().size() == initial_note_count,
            "package-backed CellStore attribute failure should not append notes");
        check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
            "package-backed CellStore attribute failure should not queue calcChain removal");
        check(!editor.edit_plan().full_calculation_on_load(),
            "package-backed CellStore attribute failure should not request recalculation");
        check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
            "package-backed CellStore attribute failure should not change calcChain policy");
        check_manifest_write_mode(editor, worksheet_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "package-backed CellStore attribute failure should keep worksheet copy-original");
        check_manifest_write_mode(editor, calc_chain_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "package-backed CellStore attribute failure should keep calcChain copy-original");

        editor.save_as(output);
        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(output);
        check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
            "package-backed CellStore attribute output should preserve workbook bytes");
        check(output_reader.read_entry("xl/_rels/workbook.xml.rels") == source.workbook_relationships,
            "package-backed CellStore attribute output should preserve workbook rels bytes");
        check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
            "package-backed CellStore attribute output should preserve worksheet bytes");
        check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
            "package-backed CellStore attribute output should preserve calcChain bytes");
        check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
            "package-backed CellStore attribute output should preserve unknown bytes");
    }
}

void test_package_editor_source_loaded_cell_store_entity_failure_preserves_editor_state()
{
    struct EntityFailureCase {
        const char* source_name;
        const char* output_name;
        const char* worksheet_xml;
        const char* expected_diagnostic;
    };

    const EntityFailureCase cases[] = {
        {
            "fastxlsx-package-editor-source-cellstore-unknown-entity-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-unknown-entity-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>bad &unknown;</t></is></c></row></sheetData></worksheet>)",
            "unknown XML entity reference",
        },
        {
            "fastxlsx-package-editor-source-cellstore-unterminated-entity-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-unterminated-entity-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>bad &amp</t></is></c></row></sheetData></worksheet>)",
            "unterminated XML entity",
        },
        {
            "fastxlsx-package-editor-source-cellstore-invalid-char-ref-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-invalid-char-ref-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>bad &#xZZ;</t></is></c></row></sheetData></worksheet>)",
            "invalid XML character reference",
        },
        {
            "fastxlsx-package-editor-source-cellstore-out-of-range-char-ref-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-out-of-range-char-ref-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>bad &#x110000;</t></is></c></row></sheetData></worksheet>)",
            "XML character reference exceeds Unicode range",
        },
    };

    for (const EntityFailureCase& test_case : cases) {
        CalcSourcePackage source = write_calc_source_package(test_case.source_name);
        source.worksheet = test_case.worksheet_xml;
        rewrite_calc_source_package(source);
        const std::filesystem::path output = output_path(test_case.output_name);

        const fastxlsx::detail::PackageReader source_reader =
            fastxlsx::detail::PackageReader::open(source.path);
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);
        const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
        const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
        const std::size_t initial_plan_size = editor.edit_plan().size();
        const std::size_t initial_note_count = editor.edit_plan().notes().size();
        const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
            editor.edit_plan().calc_chain_action();

        bool failed = false;
        try {
            (void)fastxlsx::detail::load_cell_store_from_workbook_sheet(
                source_reader, "Sheet1");
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "failed to load CellStore from workbook sheet 'Sheet1'",
                "package-backed CellStore entity failure should identify the workbook sheet");
            check_contains(error.what(), "worksheet part '/xl/worksheets/sheet1.xml'",
                "package-backed CellStore entity failure should identify the worksheet part");
            check_contains(error.what(), "ZIP entry 'xl/worksheets/sheet1.xml'",
                "package-backed CellStore entity failure should identify the worksheet ZIP entry");
            if (std::string_view(error.what()).find(test_case.expected_diagnostic)
                == std::string_view::npos) {
                throw TestFailure(std::string(
                                      "package-backed CellStore entity failure diagnostic mismatch for ")
                    + test_case.source_name + ": " + error.what());
            }
        }
        check(failed,
            "package-backed CellStore load should fail on unsupported source XML entity text");

        check(editor.edit_plan().size() == initial_plan_size,
            "package-backed CellStore entity failure should not mutate edit-plan parts");
        check(editor.edit_plan().notes().size() == initial_note_count,
            "package-backed CellStore entity failure should not append notes");
        check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
            "package-backed CellStore entity failure should not queue calcChain removal");
        check(!editor.edit_plan().full_calculation_on_load(),
            "package-backed CellStore entity failure should not request recalculation");
        check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
            "package-backed CellStore entity failure should not change calcChain policy");
        check_manifest_write_mode(editor, worksheet_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "package-backed CellStore entity failure should keep worksheet copy-original");
        check_manifest_write_mode(editor, calc_chain_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "package-backed CellStore entity failure should keep calcChain copy-original");

        editor.save_as(output);
        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(output);
        check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
            "package-backed CellStore entity output should preserve workbook bytes");
        check(output_reader.read_entry("xl/_rels/workbook.xml.rels") == source.workbook_relationships,
            "package-backed CellStore entity output should preserve workbook rels bytes");
        check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
            "package-backed CellStore entity output should preserve worksheet bytes");
        check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
            "package-backed CellStore entity output should preserve calcChain bytes");
        check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
            "package-backed CellStore entity output should preserve unknown bytes");
    }
}

void test_package_editor_source_loaded_cell_store_cell_type_shape_failure_preserves_editor_state()
{
    struct CellTypeShapeFailureCase {
        const char* source_name;
        const char* output_name;
        const char* worksheet_xml;
        const char* expected_diagnostic;
    };

    const CellTypeShapeFailureCase cases[] = {
        {
            "fastxlsx-package-editor-source-cellstore-missing-error-cell-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-missing-error-cell-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1" t="e"/></row></sheetData></worksheet>)",
            "invalid error cell value",
        },
        {
            "fastxlsx-package-editor-source-cellstore-empty-error-cell-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-empty-error-cell-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1" t="e"><v></v></c></row></sheetData></worksheet>)",
            "invalid error cell value",
        },
        {
            "fastxlsx-package-editor-source-cellstore-date-cell-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-date-cell-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1" t="d"><v>2026-06-16T00:00:00Z</v></c></row></sheetData></worksheet>)",
            "unsupported cell type",
        },
        {
            "fastxlsx-package-editor-source-cellstore-inline-in-non-inline-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-inline-in-non-inline-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1"><is><t>bad</t></is></c></row></sheetData></worksheet>)",
            "inline-string metadata in a non-inline string cell",
        },
        {
            "fastxlsx-package-editor-source-cellstore-value-in-inline-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-value-in-inline-output.xlsx",
            R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><v>bad</v></c></row></sheetData></worksheet>)",
            "non-inline value in an inline string cell",
        },
    };

    for (const CellTypeShapeFailureCase& test_case : cases) {
        CalcSourcePackage source = write_calc_source_package(test_case.source_name);
        source.worksheet = test_case.worksheet_xml;
        rewrite_calc_source_package(source);
        const std::filesystem::path output = output_path(test_case.output_name);

        const fastxlsx::detail::PackageReader source_reader =
            fastxlsx::detail::PackageReader::open(source.path);
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);
        const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
        const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
        const std::size_t initial_plan_size = editor.edit_plan().size();
        const std::size_t initial_note_count = editor.edit_plan().notes().size();
        const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
            editor.edit_plan().calc_chain_action();

        bool failed = false;
        try {
            (void)fastxlsx::detail::load_cell_store_from_workbook_sheet(
                source_reader, "Sheet1");
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "failed to load CellStore from workbook sheet 'Sheet1'",
                "package-backed CellStore cell-type failure should identify the workbook sheet");
            check_contains(error.what(), "worksheet part '/xl/worksheets/sheet1.xml'",
                "package-backed CellStore cell-type failure should identify the worksheet part");
            check_contains(error.what(), "ZIP entry 'xl/worksheets/sheet1.xml'",
                "package-backed CellStore cell-type failure should identify the worksheet ZIP entry");
            if (std::string_view(error.what()).find(test_case.expected_diagnostic)
                == std::string_view::npos) {
                throw TestFailure(std::string(
                                      "package-backed CellStore cell-type failure diagnostic mismatch for ")
                    + test_case.source_name + ": " + error.what());
            }
        }
        check(failed,
            "package-backed CellStore load should fail on unsupported cell type, invalid error value, or inline-string shapes");

        check(editor.edit_plan().size() == initial_plan_size,
            "package-backed CellStore cell-type failure should not mutate edit-plan parts");
        check(editor.edit_plan().notes().size() == initial_note_count,
            "package-backed CellStore cell-type failure should not append notes");
        check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
            "package-backed CellStore cell-type failure should not queue calcChain removal");
        check(!editor.edit_plan().full_calculation_on_load(),
            "package-backed CellStore cell-type failure should not request recalculation");
        check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
            "package-backed CellStore cell-type failure should not change calcChain policy");
        check_manifest_write_mode(editor, worksheet_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "package-backed CellStore cell-type failure should keep worksheet copy-original");
        check_manifest_write_mode(editor, calc_chain_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "package-backed CellStore cell-type failure should keep calcChain copy-original");

        editor.save_as(output);
        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(output);
        check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
            "package-backed CellStore cell-type output should preserve workbook bytes");
        check(output_reader.read_entry("xl/_rels/workbook.xml.rels") == source.workbook_relationships,
            "package-backed CellStore cell-type output should preserve workbook rels bytes");
        check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
            "package-backed CellStore cell-type output should preserve worksheet bytes");
        check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
            "package-backed CellStore cell-type output should preserve calcChain bytes");
        check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
            "package-backed CellStore cell-type output should preserve unknown bytes");
    }
}

void test_package_editor_source_loaded_cell_store_failure_preserves_editor_state()
{
    CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-source-cellstore-failure-source.xlsx");
    source.worksheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>11</v></c><c r="B1" s="+1"><v>22</v></c></row></sheetData></worksheet>)";
    rewrite_calc_source_package(source);

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    bool failed = false;
    try {
        (void)fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Sheet1");
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "failed to load CellStore from workbook sheet 'Sheet1'",
            "source-backed CellStore load failure should identify the workbook sheet");
        check_contains(error.what(), "worksheet part '/xl/worksheets/sheet1.xml'",
            "source-backed CellStore load failure should identify the worksheet part");
        check_contains(error.what(), "ZIP entry 'xl/worksheets/sheet1.xml'",
            "source-backed CellStore load failure should identify the worksheet ZIP entry");
        check_contains(error.what(), "invalid style id reference",
            "source-backed CellStore load failure should report the unsupported source semantic");
    }
    check(failed, "source-backed CellStore load should fail on unsupported source semantics");

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "source-backed CellStore load failure should leave the worksheet plan visible");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-backed CellStore load failure should not stage a worksheet rewrite");
    check(editor.manifest().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-backed CellStore load failure should not dirty the worksheet manifest");
    check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
        "source-backed CellStore load failure should not queue calcChain removal");
    check(!editor.edit_plan().full_calculation_on_load(),
        "source-backed CellStore load failure should not request workbook recalculation");
    check(editor.edit_plan().notes().empty(),
        "source-backed CellStore load failure should not add audit notes");
    check_entry_bytes(source_reader, "xl/worksheets/sheet1.xml", source.worksheet);
    check_entry_bytes(source_reader, "xl/calcChain.xml", source.calc_chain);
    check_entry_bytes(source_reader, "custom/opaque.bin", source.unknown);
}

void test_package_editor_source_loaded_cell_store_missing_entry_failure_preserves_editor_state()
{
    SourcePackage source = write_missing_worksheet_entry_source_package(
        "fastxlsx-package-editor-source-cellstore-missing-entry-source.xlsx");
    source.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"docProps/core.xml", source.core_properties},
            {"custom/opaque.bin", source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-source-cellstore-missing-entry-output.xlsx");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
        editor.edit_plan().calc_chain_action();

    bool failed = false;
    try {
        (void)fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Sheet1");
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "failed to resolve workbook sheet 'Sheet1'",
            "source-backed CellStore missing entry failure should identify the workbook sheet");
        check_contains(error.what(), "workbook sheet relationship targets an unknown part",
            "source-backed CellStore missing entry failure should report the catalog target gap");
    }
    check(failed,
        "source-backed CellStore load should fail when the target worksheet entry is missing");

    check(editor.edit_plan().size() == initial_plan_size,
        "source-backed CellStore missing entry failure should not mutate edit-plan parts");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "source-backed CellStore missing entry failure should not append notes");
    check(editor.edit_plan().find_removed_part(worksheet_part) == nullptr,
        "source-backed CellStore missing entry failure should not queue worksheet removal");
    check(!editor.edit_plan().full_calculation_on_load(),
        "source-backed CellStore missing entry failure should not request recalculation");
    check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
        "source-backed CellStore missing entry failure should not change calcChain policy");
    check(editor.manifest().find_part(worksheet_part) == nullptr,
        "source-backed CellStore missing entry failure should not invent the missing worksheet part");

    editor.save_as(output);
    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.find_entry("xl/worksheets/sheet1.xml") == nullptr,
        "source-backed CellStore missing entry output should not invent worksheet XML");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "source-backed CellStore missing entry output should preserve workbook bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "source-backed CellStore missing entry output should preserve unknown bytes");
}

void test_package_editor_source_loaded_cell_store_crc_failure_preserves_editor_state()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-source-cellstore-crc-source.xlsx");
    std::string corrupted_source_bytes = fastxlsx::test::read_file(source.path);
    corrupt_first_occurrence(corrupted_source_bytes, "SUM(B1:C1)");
    write_binary_file(source.path, corrupted_source_bytes);

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
        editor.edit_plan().calc_chain_action();

    bool failed = false;
    try {
        (void)fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Sheet1");
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "failed to load CellStore from workbook sheet 'Sheet1'",
            "source-backed CellStore CRC failure should identify the workbook sheet");
        check_contains(error.what(), "worksheet part '/xl/worksheets/sheet1.xml'",
            "source-backed CellStore CRC failure should identify the worksheet part");
        check_contains(error.what(), "ZIP entry 'xl/worksheets/sheet1.xml'",
            "source-backed CellStore CRC failure should identify the worksheet ZIP entry");
        check_contains(error.what(), "CRC mismatch",
            "source-backed CellStore CRC failure should preserve the underlying ZIP error");
        check_contains(error.what(), "expected",
            "source-backed CellStore CRC failure should report expected CRC");
        check_contains(error.what(), "actual",
            "source-backed CellStore CRC failure should report actual CRC");
    }
    check(failed, "source-backed CellStore load should fail on corrupt worksheet bytes");

    check(editor.edit_plan().size() == initial_plan_size,
        "source-backed CellStore CRC failure should not mutate edit-plan parts");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "source-backed CellStore CRC failure should not append notes");
    check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
        "source-backed CellStore CRC failure should not queue calcChain removal");
    check(!editor.edit_plan().full_calculation_on_load(),
        "source-backed CellStore CRC failure should not request recalculation");
    check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
        "source-backed CellStore CRC failure should not change calcChain policy");
    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr
            && worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-backed CellStore CRC failure should keep worksheet copy-original");
    check(editor.manifest().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-backed CellStore CRC failure should not dirty the worksheet manifest");
}

void test_package_editor_source_loaded_cell_store_corrupt_workbook_catalog_preserves_editor_state()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-source-cellstore-workbook-crc-source.xlsx");
    std::string corrupted_source_bytes = fastxlsx::test::read_file(source.path);
    corrupt_first_occurrence(corrupted_source_bytes, "Sheet1");
    write_binary_file(source.path, corrupted_source_bytes);

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
        editor.edit_plan().calc_chain_action();

    bool failed = false;
    try {
        (void)fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Sheet1");
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "failed to resolve workbook sheet 'Sheet1'",
            "source-backed CellStore workbook CRC failure should identify the requested sheet");
        check_contains(error.what(), "failed to read materialized workbook sheet catalog XML",
            "source-backed CellStore workbook CRC failure should identify catalog materialization");
        check_contains(error.what(), "ZIP entry 'xl/workbook.xml'",
            "source-backed CellStore workbook CRC failure should identify the workbook ZIP entry");
        check_contains(error.what(), "CRC mismatch",
            "source-backed CellStore workbook CRC failure should preserve the ZIP error");
        check_contains(error.what(), "expected",
            "source-backed CellStore workbook CRC failure should report expected CRC");
        check_contains(error.what(), "actual",
            "source-backed CellStore workbook CRC failure should report actual CRC");
    }
    check(failed, "source-backed CellStore load should fail on corrupt workbook catalog bytes");

    check(editor.edit_plan().size() == initial_plan_size,
        "source-backed CellStore workbook CRC failure should not mutate edit-plan parts");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "source-backed CellStore workbook CRC failure should not append notes");
    check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
        "source-backed CellStore workbook CRC failure should not queue calcChain removal");
    check(!editor.edit_plan().full_calculation_on_load(),
        "source-backed CellStore workbook CRC failure should not request recalculation");
    check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
        "source-backed CellStore workbook CRC failure should not change calcChain policy");
    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr
            && worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-backed CellStore workbook CRC failure should keep worksheet copy-original");
    check(editor.manifest().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-backed CellStore workbook CRC failure should not dirty the worksheet manifest");
}

void test_package_editor_source_loaded_cell_store_duplicate_sheet_name_preserves_editor_state()
{
    CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-source-cellstore-duplicate-sheet-source.xlsx");
    source.content_types =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet2.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/calcChain.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.calcChain+xml"/>)"
        R"(</Types>)";
    source.workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet2.xml"/>)"
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="calcChain.xml"/>)"
        R"(</Relationships>)";
    source.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets>)"
        R"(<sheet name="Sheet1" sheetId="1" r:id="rId1"/>)"
        R"(<sheet name="Sheet1" sheetId="2" r:id="rId2"/>)"
        R"(</sheets>)"
        R"(<calcPr calcId="1" fullCalcOnLoad="0"/>)"
        R"(</workbook>)";
    const std::string second_worksheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>222</v></c></row></sheetData></worksheet>)";
    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"xl/worksheets/sheet2.xml", second_worksheet},
            {"xl/calcChain.xml", source.calc_chain},
            {"custom/opaque.bin", source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-source-cellstore-duplicate-sheet-output.xlsx");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName second_worksheet_part("/xl/worksheets/sheet2.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
        editor.edit_plan().calc_chain_action();

    bool failed = false;
    try {
        (void)fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Sheet1");
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "failed to resolve workbook sheet 'Sheet1'",
            "source-backed CellStore duplicate sheet failure should identify the requested sheet");
        check_contains(error.what(), "workbook sheet name is ambiguous",
            "source-backed CellStore duplicate sheet failure should preserve the catalog diagnostic");
    }
    check(failed, "source-backed CellStore load should fail on duplicate sheet names");

    check(editor.edit_plan().size() == initial_plan_size,
        "source-backed CellStore duplicate sheet failure should not mutate edit-plan parts");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "source-backed CellStore duplicate sheet failure should not append notes");
    check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
        "source-backed CellStore duplicate sheet failure should not queue calcChain removal");
    check(!editor.edit_plan().full_calculation_on_load(),
        "source-backed CellStore duplicate sheet failure should not request recalculation");
    check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
        "source-backed CellStore duplicate sheet failure should not change calcChain policy");
    check(editor.manifest().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-backed CellStore duplicate sheet failure should keep first worksheet copy-original");
    check(editor.manifest().find_part(second_worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-backed CellStore duplicate sheet failure should keep second worksheet copy-original");

    editor.save_as(output);
    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "source-backed CellStore duplicate sheet output should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "source-backed CellStore duplicate sheet output should preserve first worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/sheet2.xml") == second_worksheet,
        "source-backed CellStore duplicate sheet output should preserve second worksheet bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "source-backed CellStore duplicate sheet output should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "source-backed CellStore duplicate sheet output should preserve unknown bytes");
}

void test_package_editor_source_loaded_cell_store_missing_sheet_relationship_preserves_editor_state()
{
    CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-source-cellstore-missing-sheet-rel-source.xlsx");
    source.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="missingRel"/></sheets>)"
        R"(<calcPr calcId="1" fullCalcOnLoad="0"/>)"
        R"(</workbook>)";
    rewrite_calc_source_package(source);
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-source-cellstore-missing-sheet-rel-output.xlsx");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
        editor.edit_plan().calc_chain_action();

    bool failed = false;
    try {
        (void)fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Sheet1");
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "failed to resolve workbook sheet 'Sheet1'",
            "source-backed CellStore missing sheet relationship failure should identify the requested sheet");
        check_contains(error.what(), "workbook sheet relationship id is not present in workbook .rels",
            "source-backed CellStore missing sheet relationship failure should preserve the catalog diagnostic");
    }
    check(failed,
        "source-backed CellStore load should fail when the sheet relationship id is absent");

    check(editor.edit_plan().size() == initial_plan_size,
        "source-backed CellStore missing sheet relationship failure should not mutate edit-plan parts");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "source-backed CellStore missing sheet relationship failure should not append notes");
    check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
        "source-backed CellStore missing sheet relationship failure should not queue calcChain removal");
    check(!editor.edit_plan().full_calculation_on_load(),
        "source-backed CellStore missing sheet relationship failure should not request recalculation");
    check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
        "source-backed CellStore missing sheet relationship failure should not change calcChain policy");
    check(editor.manifest().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-backed CellStore missing sheet relationship failure should keep worksheet copy-original");

    editor.save_as(output);
    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "source-backed CellStore missing sheet relationship output should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels") == source.workbook_relationships,
        "source-backed CellStore missing sheet relationship output should preserve workbook rels bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "source-backed CellStore missing sheet relationship output should preserve worksheet bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "source-backed CellStore missing sheet relationship output should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "source-backed CellStore missing sheet relationship output should preserve unknown bytes");
}

void test_package_editor_source_loaded_cell_store_sheet_catalog_attribute_preserves_editor_state()
{
    struct SheetCatalogAttributeFailureCase {
        const char* source_name;
        const char* output_name;
        const char* workbook_xml;
        const char* expected_diagnostic;
    };

    const SheetCatalogAttributeFailureCase cases[] = {
        {
            "fastxlsx-package-editor-source-cellstore-missing-sheet-rel-attr-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-missing-sheet-rel-attr-output.xlsx",
            R"(<workbook><sheets><sheet name="Sheet1" sheetId="1"/></sheets></workbook>)",
            "workbook sheet is missing relationship id",
        },
        {
            "fastxlsx-package-editor-source-cellstore-wrong-sheet-rel-ns-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-wrong-sheet-rel-ns-output.xlsx",
            R"(<workbook xmlns:x="urn:fastxlsx:not-relationships"><sheets><sheet name="Sheet1" sheetId="1" x:id="rId1"/></sheets></workbook>)",
            "workbook sheet is missing relationship id",
        },
        {
            "fastxlsx-package-editor-source-cellstore-unqualified-sheet-rel-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-unqualified-sheet-rel-output.xlsx",
            R"(<workbook><sheets><sheet name="Sheet1" sheetId="1" id="rId1"/></sheets></workbook>)",
            "workbook sheet is missing relationship id",
        },
        {
            "fastxlsx-package-editor-source-cellstore-namespaced-sheet-name-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-namespaced-sheet-name-output.xlsx",
            R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:x="urn:fastxlsx:not-workbook"><sheets><sheet x:name="Sheet1" sheetId="1" r:id="rId1"/></sheets></workbook>)",
            "workbook sheet is missing name",
        },
        {
            "fastxlsx-package-editor-source-cellstore-namespaced-sheet-id-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-namespaced-sheet-id-output.xlsx",
            R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:x="urn:fastxlsx:not-workbook"><sheets><sheet name="Sheet1" x:sheetId="1" r:id="rId1"/></sheets></workbook>)",
            "workbook sheet is missing sheetId",
        },
    };

    for (const SheetCatalogAttributeFailureCase& test_case : cases) {
        CalcSourcePackage source = write_calc_source_package(test_case.source_name);
        source.workbook = test_case.workbook_xml;
        rewrite_calc_source_package(source);
        const std::filesystem::path output = output_path(test_case.output_name);

        const fastxlsx::detail::PackageReader source_reader =
            fastxlsx::detail::PackageReader::open(source.path);
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);
        const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
        const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
        const std::size_t initial_plan_size = editor.edit_plan().size();
        const std::size_t initial_note_count = editor.edit_plan().notes().size();
        const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
            editor.edit_plan().calc_chain_action();

        bool failed = false;
        try {
            (void)fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Sheet1");
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "failed to resolve workbook sheet 'Sheet1'",
                "source-backed CellStore sheet catalog attribute failure should identify the requested sheet");
            check_contains(error.what(), test_case.expected_diagnostic,
                "source-backed CellStore sheet catalog attribute failure should preserve the catalog diagnostic");
        }
        check(failed,
            "source-backed CellStore load should fail for invalid workbook sheet catalog attributes");

        check(editor.edit_plan().size() == initial_plan_size,
            "source-backed CellStore sheet catalog attribute failure should not mutate edit-plan parts");
        check(editor.edit_plan().notes().size() == initial_note_count,
            "source-backed CellStore sheet catalog attribute failure should not append notes");
        check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
            "source-backed CellStore sheet catalog attribute failure should not queue calcChain removal");
        check(!editor.edit_plan().full_calculation_on_load(),
            "source-backed CellStore sheet catalog attribute failure should not request recalculation");
        check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
            "source-backed CellStore sheet catalog attribute failure should not change calcChain policy");
        check(editor.manifest().find_part(worksheet_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "source-backed CellStore sheet catalog attribute failure should keep worksheet copy-original");
        check(editor.manifest().find_part(calc_chain_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "source-backed CellStore sheet catalog attribute failure should keep calcChain copy-original");

        editor.save_as(output);
        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(output);
        check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
            "source-backed CellStore sheet catalog attribute output should preserve workbook bytes");
        check(output_reader.read_entry("xl/_rels/workbook.xml.rels") == source.workbook_relationships,
            "source-backed CellStore sheet catalog attribute output should preserve workbook rels bytes");
        check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
            "source-backed CellStore sheet catalog attribute output should preserve worksheet bytes");
        check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
            "source-backed CellStore sheet catalog attribute output should preserve calcChain bytes");
        check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
            "source-backed CellStore sheet catalog attribute output should preserve unknown bytes");
    }
}

void test_package_editor_source_loaded_cell_store_sheet_relationship_type_preserves_editor_state()
{
    CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-source-cellstore-sheet-rel-type-source.xlsx");
    source.workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="worksheets/sheet1.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="calcChain.xml"/>)"
        R"(</Relationships>)";
    rewrite_calc_source_package(source);
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-source-cellstore-sheet-rel-type-output.xlsx");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
        editor.edit_plan().calc_chain_action();

    bool failed = false;
    try {
        (void)fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Sheet1");
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "failed to resolve workbook sheet 'Sheet1'",
            "source-backed CellStore sheet relationship type failure should identify the requested sheet");
        check_contains(error.what(), "workbook sheet relationship is not a worksheet relationship",
            "source-backed CellStore sheet relationship type failure should preserve the catalog diagnostic");
    }
    check(failed,
        "source-backed CellStore load should fail when the sheet relationship type is not worksheet");

    check(editor.edit_plan().size() == initial_plan_size,
        "source-backed CellStore sheet relationship type failure should not mutate edit-plan parts");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "source-backed CellStore sheet relationship type failure should not append notes");
    check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
        "source-backed CellStore sheet relationship type failure should not queue calcChain removal");
    check(!editor.edit_plan().full_calculation_on_load(),
        "source-backed CellStore sheet relationship type failure should not request recalculation");
    check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
        "source-backed CellStore sheet relationship type failure should not change calcChain policy");
    check(editor.manifest().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-backed CellStore sheet relationship type failure should keep worksheet copy-original");

    editor.save_as(output);
    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "source-backed CellStore sheet relationship type output should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels") == source.workbook_relationships,
        "source-backed CellStore sheet relationship type output should preserve workbook rels bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "source-backed CellStore sheet relationship type output should preserve worksheet bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "source-backed CellStore sheet relationship type output should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "source-backed CellStore sheet relationship type output should preserve unknown bytes");
}

void test_package_editor_source_loaded_cell_store_sheet_relationship_target_type_preserves_editor_state()
{
    CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-source-cellstore-sheet-target-type-source.xlsx");
    source.workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="calcChain.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="calcChain.xml"/>)"
        R"(</Relationships>)";
    rewrite_calc_source_package(source);
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-source-cellstore-sheet-target-type-output.xlsx");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
        editor.edit_plan().calc_chain_action();

    bool failed = false;
    try {
        (void)fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Sheet1");
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "failed to resolve workbook sheet 'Sheet1'",
            "source-backed CellStore sheet target type failure should identify the requested sheet");
        check_contains(error.what(), "workbook sheet relationship target is not a worksheet part",
            "source-backed CellStore sheet target type failure should preserve the catalog diagnostic");
    }
    check(failed,
        "source-backed CellStore load should fail when the sheet relationship target is not a worksheet part");

    check(editor.edit_plan().size() == initial_plan_size,
        "source-backed CellStore sheet target type failure should not mutate edit-plan parts");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "source-backed CellStore sheet target type failure should not append notes");
    check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
        "source-backed CellStore sheet target type failure should not queue calcChain removal");
    check(!editor.edit_plan().full_calculation_on_load(),
        "source-backed CellStore sheet target type failure should not request recalculation");
    check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
        "source-backed CellStore sheet target type failure should not change calcChain policy");
    check(editor.manifest().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-backed CellStore sheet target type failure should keep worksheet copy-original");
    check(editor.manifest().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-backed CellStore sheet target type failure should keep calcChain copy-original");

    editor.save_as(output);
    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "source-backed CellStore sheet target type output should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels") == source.workbook_relationships,
        "source-backed CellStore sheet target type output should preserve workbook rels bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "source-backed CellStore sheet target type output should preserve worksheet bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "source-backed CellStore sheet target type output should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "source-backed CellStore sheet target type output should preserve unknown bytes");
}

void test_package_editor_source_loaded_cell_store_sheet_relationship_target_uri_preserves_editor_state()
{
    struct TargetFailureCase {
        const char* source_name;
        const char* output_name;
        const char* workbook_relationships;
        const char* expected_diagnostic;
    };

    const TargetFailureCase cases[] = {
        {
            "fastxlsx-package-editor-source-cellstore-sheet-external-target-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sheet-external-target-output.xlsx",
            R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
            R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="https://example.invalid/sheet1.xml" TargetMode="External"/>)"
            R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="calcChain.xml"/>)"
            R"(</Relationships>)",
            "workbook sheet relationship target cannot be external",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sheet-uri-target-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sheet-uri-target-output.xlsx",
            R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
            R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml?version=1"/>)"
            R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="calcChain.xml"/>)"
            R"(</Relationships>)",
            "workbook sheet relationship target must be a package part",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sheet-incomplete-percent-target-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sheet-incomplete-percent-target-output.xlsx",
            R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
            R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml%"/>)"
            R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="calcChain.xml"/>)"
            R"(</Relationships>)",
            "relationship target percent escape is incomplete",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sheet-invalid-percent-target-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sheet-invalid-percent-target-output.xlsx",
            R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
            R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet%GG.xml"/>)"
            R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="calcChain.xml"/>)"
            R"(</Relationships>)",
            "relationship target percent escape is invalid",
        },
        {
            "fastxlsx-package-editor-source-cellstore-sheet-null-percent-target-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-sheet-null-percent-target-output.xlsx",
            R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
            R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet%00.xml"/>)"
            R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="calcChain.xml"/>)"
            R"(</Relationships>)",
            "relationship target cannot contain null bytes",
        },
    };

    for (const TargetFailureCase& test_case : cases) {
        CalcSourcePackage source = write_calc_source_package(test_case.source_name);
        source.workbook_relationships = test_case.workbook_relationships;
        rewrite_calc_source_package(source);
        const std::filesystem::path output = output_path(test_case.output_name);

        const fastxlsx::detail::PackageReader source_reader =
            fastxlsx::detail::PackageReader::open(source.path);
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);
        const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
        const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
        const std::size_t initial_plan_size = editor.edit_plan().size();
        const std::size_t initial_note_count = editor.edit_plan().notes().size();
        const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
            editor.edit_plan().calc_chain_action();

        bool failed = false;
        try {
            (void)fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Sheet1");
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "failed to resolve workbook sheet 'Sheet1'",
                "source-backed CellStore sheet target URI failure should identify the requested sheet");
            check_contains(error.what(), test_case.expected_diagnostic,
                "source-backed CellStore sheet target URI failure should preserve the catalog diagnostic");
        }
        check(failed,
            "source-backed CellStore load should fail for external or URI-qualified sheet relationship targets");

        check(editor.edit_plan().size() == initial_plan_size,
            "source-backed CellStore sheet target URI failure should not mutate edit-plan parts");
        check(editor.edit_plan().notes().size() == initial_note_count,
            "source-backed CellStore sheet target URI failure should not append notes");
        check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
            "source-backed CellStore sheet target URI failure should not queue calcChain removal");
        check(!editor.edit_plan().full_calculation_on_load(),
            "source-backed CellStore sheet target URI failure should not request recalculation");
        check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
            "source-backed CellStore sheet target URI failure should not change calcChain policy");
        check(editor.manifest().find_part(worksheet_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "source-backed CellStore sheet target URI failure should keep worksheet copy-original");
        check(editor.manifest().find_part(calc_chain_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "source-backed CellStore sheet target URI failure should keep calcChain copy-original");

        editor.save_as(output);
        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(output);
        check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
            "source-backed CellStore sheet target URI output should preserve workbook bytes");
        check(output_reader.read_entry("xl/_rels/workbook.xml.rels") == source.workbook_relationships,
            "source-backed CellStore sheet target URI output should preserve workbook rels bytes");
        check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
            "source-backed CellStore sheet target URI output should preserve worksheet bytes");
        check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
            "source-backed CellStore sheet target URI output should preserve calcChain bytes");
        check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
            "source-backed CellStore sheet target URI output should preserve unknown bytes");
    }
}

void test_package_editor_source_loaded_cell_store_office_document_catalog_preserves_editor_state()
{
    struct OfficeDocumentFailureCase {
        const char* source_name;
        const char* output_name;
        const char* package_relationships;
        const char* expected_diagnostic;
    };

    const OfficeDocumentFailureCase cases[] = {
        {
            "fastxlsx-package-editor-source-cellstore-missing-office-document-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-missing-office-document-output.xlsx",
            R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
            R"(<Relationship Id="rIdCustom" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXml" Target="custom/opaque.bin"/>)"
            R"(</Relationships>)",
            "workbook sheet catalog requires package officeDocument relationship",
        },
        {
            "fastxlsx-package-editor-source-cellstore-duplicate-office-document-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-duplicate-office-document-output.xlsx",
            R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
            R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
            R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
            R"(</Relationships>)",
            "workbook sheet catalog has multiple officeDocument relationships",
        },
        {
            "fastxlsx-package-editor-source-cellstore-external-office-document-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-external-office-document-output.xlsx",
            R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
            R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="https://example.invalid/workbook.xml" TargetMode="External"/>)"
            R"(</Relationships>)",
            "workbook sheet catalog officeDocument target cannot be external",
        },
        {
            "fastxlsx-package-editor-source-cellstore-uri-office-document-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-uri-office-document-output.xlsx",
            R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
            R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml?version=1"/>)"
            R"(</Relationships>)",
            "workbook sheet catalog officeDocument target must be a package part",
        },
        {
            "fastxlsx-package-editor-source-cellstore-invalid-percent-office-document-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-invalid-percent-office-document-output.xlsx",
            R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
            R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook%GG.xml"/>)"
            R"(</Relationships>)",
            "relationship target percent escape is invalid",
        },
        {
            "fastxlsx-package-editor-source-cellstore-incomplete-percent-office-document-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-incomplete-percent-office-document-output.xlsx",
            R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
            R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml%"/>)"
            R"(</Relationships>)",
            "relationship target percent escape is incomplete",
        },
        {
            "fastxlsx-package-editor-source-cellstore-null-percent-office-document-source.xlsx",
            "fastxlsx-package-editor-source-cellstore-null-percent-office-document-output.xlsx",
            R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
            R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook%00.xml"/>)"
            R"(</Relationships>)",
            "relationship target cannot contain null bytes",
        },
    };

    for (const OfficeDocumentFailureCase& test_case : cases) {
        CalcSourcePackage source = write_calc_source_package(test_case.source_name);
        source.package_relationships = test_case.package_relationships;
        rewrite_calc_source_package(source);
        const std::filesystem::path output = output_path(test_case.output_name);

        const fastxlsx::detail::PackageReader source_reader =
            fastxlsx::detail::PackageReader::open(source.path);
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);
        const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
        const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
        const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
        const std::size_t initial_plan_size = editor.edit_plan().size();
        const std::size_t initial_note_count = editor.edit_plan().notes().size();
        const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
            editor.edit_plan().calc_chain_action();

        bool failed = false;
        try {
            (void)fastxlsx::detail::load_cell_store_from_workbook_sheet(source_reader, "Sheet1");
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "failed to resolve workbook sheet 'Sheet1'",
                "source-backed CellStore officeDocument catalog failure should identify the requested sheet");
            check_contains(error.what(), test_case.expected_diagnostic,
                "source-backed CellStore officeDocument catalog failure should preserve the catalog diagnostic");
        }
        check(failed,
            "source-backed CellStore load should fail for invalid package officeDocument catalogs");

        check(editor.edit_plan().size() == initial_plan_size,
            "source-backed CellStore officeDocument catalog failure should not mutate edit-plan parts");
        check(editor.edit_plan().notes().size() == initial_note_count,
            "source-backed CellStore officeDocument catalog failure should not append notes");
        check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
            "source-backed CellStore officeDocument catalog failure should not queue calcChain removal");
        check(!editor.edit_plan().full_calculation_on_load(),
            "source-backed CellStore officeDocument catalog failure should not request recalculation");
        check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
            "source-backed CellStore officeDocument catalog failure should not change calcChain policy");
        check(editor.manifest().find_part(workbook_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "source-backed CellStore officeDocument catalog failure should keep workbook copy-original");
        check(editor.manifest().find_part(worksheet_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "source-backed CellStore officeDocument catalog failure should keep worksheet copy-original");
        check(editor.manifest().find_part(calc_chain_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "source-backed CellStore officeDocument catalog failure should keep calcChain copy-original");

        editor.save_as(output);
        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(output);
        check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
            "source-backed CellStore officeDocument catalog output should preserve package rels bytes");
        check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
            "source-backed CellStore officeDocument catalog output should preserve workbook bytes");
        check(output_reader.read_entry("xl/_rels/workbook.xml.rels") == source.workbook_relationships,
            "source-backed CellStore officeDocument catalog output should preserve workbook rels bytes");
        check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
            "source-backed CellStore officeDocument catalog output should preserve worksheet bytes");
        check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
            "source-backed CellStore officeDocument catalog output should preserve calcChain bytes");
        check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
            "source-backed CellStore officeDocument catalog output should preserve unknown bytes");
    }

    const std::filesystem::path root_source =
        output_path("fastxlsx-package-editor-source-cellstore-root-office-document-source.xlsx");
    const std::filesystem::path root_output =
        output_path("fastxlsx-package-editor-source-cellstore-root-office-document-output.xlsx");
    const std::string root_content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(</Types>)";
    const std::string root_package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="workbook.xml"/>)"
        R"(</Relationships>)";
    const std::string root_workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="sheet1.xml"/>)"
        R"(</Relationships>)";
    const std::string root_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<calcPr calcId="1" fullCalcOnLoad="0"/>)"
        R"(</workbook>)";
    const std::string root_worksheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>11</v></c></row></sheetData></worksheet>)";
    const std::string root_unknown = std::string("root-office-document\0opaque", 27);
    fastxlsx::detail::write_package(root_source,
        {
            {"[Content_Types].xml", root_content_types},
            {"_rels/.rels", root_package_relationships},
            {"workbook.xml", root_workbook},
            {"_rels/workbook.xml.rels", root_workbook_relationships},
            {"sheet1.xml", root_worksheet},
            {"custom/opaque.bin", root_unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PackageReader root_reader =
        fastxlsx::detail::PackageReader::open(root_source);
    fastxlsx::detail::CellStore root_store =
        fastxlsx::detail::load_cell_store_from_workbook_sheet(root_reader, "Sheet1");
    const fastxlsx::detail::CellRecord* a1 = root_store.try_cell(1, 1);
    check(a1 != nullptr && a1->kind == fastxlsx::CellValueKind::Number
            && a1->number_value == 11.0,
        "source-backed CellStore should load root-level officeDocument worksheets");
    root_store.set_cell(1, 2, fastxlsx::CellValue::text("root-edited"));

    fastxlsx::detail::PackageEditor root_editor =
        fastxlsx::detail::PackageEditor::open(root_source);
    root_editor.replace_worksheet_sheet_data_from_chunk_source_by_name(
        "Sheet1", fastxlsx::detail::cell_store_sheet_data_chunk_source(root_store));
    root_editor.save_as(root_output);

    const fastxlsx::detail::PackageReader root_output_reader =
        fastxlsx::detail::PackageReader::open(root_output);
    check(root_output_reader.workbook_part() == fastxlsx::detail::PartName("/workbook.xml"),
        "PackageEditor output should preserve the root-level officeDocument target");
    check_contains(root_output_reader.read_entry("sheet1.xml"),
        R"(<c r="B1" t="inlineStr"><is><t>root-edited</t></is></c>)",
        "PackageEditor should patch root-level worksheet sheetData by name");
    check_contains(root_output_reader.read_entry("workbook.xml"), R"(fullCalcOnLoad="1")",
        "PackageEditor should request recalculation in the root-level workbook part");
    check(root_output_reader.read_entry("_rels/workbook.xml.rels")
            == root_workbook_relationships,
        "PackageEditor should preserve root-level workbook relationships without stale calcChain");
    check(root_output_reader.read_entry("custom/opaque.bin") == root_unknown,
        "PackageEditor should preserve unrelated unknown bytes in root-level packages");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor cellstore shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "cellstore-core")) {
        test_package_editor_patches_cell_store_sheet_data_by_sheet_name();
        test_package_editor_patches_cell_store_sheet_data_with_writer_style();
        test_package_editor_patches_source_loaded_cell_store_sheet_data_by_sheet_name();
        test_package_editor_source_loaded_cell_store_uses_planned_name_after_rename();
        test_package_editor_source_loaded_cell_store_patches_queued_worksheet_replacement();
        test_package_editor_source_loaded_cell_store_uses_planned_name_after_queued_rename_and_worksheet();
        }

        if (should_run_package_editor_shard(shard, "cellstore-chunks")) {
        test_package_editor_source_loaded_cell_store_worksheet_chunks_use_planned_name_after_queued_rename_and_worksheet();
        test_package_editor_source_loaded_cell_store_worksheet_chunks_reject_after_workbook_removal();
        test_package_editor_source_loaded_cell_store_worksheet_chunks_reject_invalid_planned_catalog();
        test_package_editor_source_loaded_cell_store_worksheet_chunks_reject_wrong_namespace_planned_catalog_id();
        test_package_editor_source_loaded_cell_store_worksheet_chunks_reject_unqualified_planned_catalog_id();
        test_package_editor_source_loaded_cell_store_worksheet_chunks_reject_unregistered_planned_catalog_target();
        test_package_editor_source_loaded_cell_store_worksheet_chunks_replace_queued_worksheet();
        test_package_editor_replaces_worksheet_from_cell_store_worksheet_chunks_by_name();
        test_package_editor_source_loaded_cell_store_worksheet_chunks_use_planned_name_after_rename();
        }

        if (should_run_package_editor_shard(shard, "cellstore-source")) {
        test_package_editor_source_loaded_cell_store_loads_semantic_values_by_name();
        test_package_editor_source_loaded_cell_store_preserves_unreferenced_styles();
        test_package_editor_source_loaded_cell_store_preserves_unreferenced_shared_strings();
        test_package_editor_source_loaded_cell_store_materializes_prefixed_shared_strings();
        test_package_editor_source_loaded_cell_store_materializes_prefixed_inline_strings();
        test_package_editor_source_loaded_cell_store_materializes_local_names_without_namespace_validation();
        test_package_editor_source_loaded_cell_store_distinguishes_blank_and_erase();
        test_package_editor_source_loaded_cell_store_preserves_loader_options();
        }

        if (should_run_package_editor_shard(shard, "cellstore-failures")) {
        test_package_editor_source_loaded_cell_store_option_failure_preserves_editor_state();
        test_package_editor_source_loaded_cell_store_shared_strings_payload_failure_preserves_editor_state();
        test_package_editor_source_loaded_cell_store_source_shape_failure_preserves_editor_state();
        test_package_editor_source_loaded_cell_store_metadata_shape_failure_preserves_editor_state();
        test_package_editor_source_loaded_cell_store_payload_failure_preserves_editor_state();
        test_package_editor_source_loaded_cell_store_reference_failure_preserves_editor_state();
        test_package_editor_source_loaded_cell_store_attribute_failure_preserves_editor_state();
        test_package_editor_source_loaded_cell_store_entity_failure_preserves_editor_state();
        test_package_editor_source_loaded_cell_store_cell_type_shape_failure_preserves_editor_state();
        test_package_editor_source_loaded_cell_store_failure_preserves_editor_state();
        }

        if (should_run_package_editor_shard(shard, "cellstore-catalog")) {
        test_package_editor_source_loaded_cell_store_missing_entry_failure_preserves_editor_state();
        test_package_editor_source_loaded_cell_store_crc_failure_preserves_editor_state();
        test_package_editor_source_loaded_cell_store_corrupt_workbook_catalog_preserves_editor_state();
        test_package_editor_source_loaded_cell_store_duplicate_sheet_name_preserves_editor_state();
        test_package_editor_source_loaded_cell_store_missing_sheet_relationship_preserves_editor_state();
        test_package_editor_source_loaded_cell_store_sheet_catalog_attribute_preserves_editor_state();
        test_package_editor_source_loaded_cell_store_sheet_relationship_type_preserves_editor_state();
        test_package_editor_source_loaded_cell_store_sheet_relationship_target_type_preserves_editor_state();
        test_package_editor_source_loaded_cell_store_sheet_relationship_target_uri_preserves_editor_state();
        test_package_editor_source_loaded_cell_store_office_document_catalog_preserves_editor_state();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
