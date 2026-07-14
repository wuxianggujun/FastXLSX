#include "../src/package_editor.hpp"
#include "../src/package_writer.hpp"
#include "zip_test_utils.hpp"

#include <fastxlsx/detail/cell_store.hpp>
#include <fastxlsx/streaming_writer.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <map>
#include <numeric>
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
    return shard == "all" || shard == "c5-linked";
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
void test_package_editor_worksheet_cell_replacement_preserves_linked_object_parts()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package(
            "fastxlsx-package-editor-cell-replacement-linked-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-cell-replacement-linked-output.xlsx");
    const std::vector<std::filesystem::path> temp_files_before =
        package_editor_temp_files();

    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName vml_drawing_part("/xl/drawings/vmlDrawing1.vml");
    const fastxlsx::detail::PartName percent_encoded_drawing_part(
        "/xl/drawings/drawing space.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName vba_part("/xl/vbaProject.bin");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::array replacements {
        worksheet_cell_replacement(
            "A1", R"(<c r="A1" t="inlineStr"><is><t>linked patch</t></is></c>)"),
    };

    {
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);

        editor.replace_worksheet_cells_by_name("Sheet1", replacements);

        const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
        check(worksheet_plan != nullptr,
            "cell replacement linked fixture should keep worksheet in edit plan");
        check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
            "cell replacement linked fixture should stream-rewrite worksheet");
        check(worksheet_plan->reason.find("file-backed stream rewrite")
                != std::string::npos,
            "cell replacement linked fixture should use file-backed staged output");
        check(editor.edit_plan().full_calculation_on_load(),
            "cell replacement linked fixture should request full calculation");
        check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
            "cell replacement linked fixture should remove stale calcChain");

        const auto check_copy_original_part =
            [&](const fastxlsx::detail::PartName& part, const char* message) {
                const auto* part_plan = editor.edit_plan().find_part(part);
                check(part_plan != nullptr, message);
                check(part_plan->write_mode
                        == fastxlsx::detail::PartWriteMode::CopyOriginal,
                    message);
            };
        check_copy_original_part(drawing_part,
            "cell replacement should preserve linked drawing part");
        check_copy_original_part(chart_part,
            "cell replacement should preserve linked chart part");
        check_copy_original_part(image_part,
            "cell replacement should preserve linked image part");
        check_copy_original_part(table_part,
            "cell replacement should preserve linked table part");
        check_copy_original_part(vml_drawing_part,
            "cell replacement should preserve URI-qualified VML target");
        check_copy_original_part(percent_encoded_drawing_part,
            "cell replacement should preserve percent-decoded drawing target");
        check_copy_original_part(shared_strings_part,
            "cell replacement should preserve sharedStrings part");
        check_copy_original_part(styles_part,
            "cell replacement should preserve styles part");
        check_copy_original_part(vba_part,
            "cell replacement should preserve VBA part");
        check_copy_original_part(opaque_extension_part,
            "cell replacement should preserve reachable unknown extension part");

        const auto* worksheet_relationships_plan =
            editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels");
        check(worksheet_relationships_plan != nullptr
                && worksheet_relationships_plan->write_mode
                    == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "cell replacement should audit preserved worksheet relationships");
        const auto* drawing_relationships_plan =
            editor.edit_plan().find_package_entry("xl/drawings/_rels/drawing1.xml.rels");
        check(drawing_relationships_plan != nullptr
                && drawing_relationships_plan->write_mode
                    == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "cell replacement should audit preserved drawing relationships");
        const auto* shared_strings_relationships_plan =
            editor.edit_plan().find_package_entry("xl/_rels/sharedStrings.xml.rels");
        check(shared_strings_relationships_plan != nullptr
                && shared_strings_relationships_plan->write_mode
                    == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "cell replacement should audit preserved sharedStrings relationships");
        const auto* opaque_extension_relationships_plan =
            editor.edit_plan().find_package_entry("custom/_rels/opaque-extension.bin.rels");
        check(opaque_extension_relationships_plan != nullptr
                && opaque_extension_relationships_plan->write_mode
                    == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "cell replacement should audit preserved unknown extension relationships");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check(output_plan.full_calculation_on_load,
            "cell replacement linked output plan should expose recalculation request");
        check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Remove,
            "cell replacement linked output plan should expose calcChain removal");
        check(has_note_containing(output_plan.notes,
                  {"one PackageEditor-owned temporary file", "bounded in-memory exact dimension tag"}),
            "cell replacement linked output plan should expose file-backed chunk note");
        check(has_note_containing(output_plan.notes,
                  {"one source-order PackageReader ZIP-entry scan",
                      "collect dependency/relationship audits",
                      "validate the root",
                      "without materializing source XML"}),
            "cell replacement linked output plan should expose fused source transform and audits");
        check(has_note_containing(output_plan.notes, {"refreshed worksheet dimension"}),
            "cell replacement linked output plan should expose dimension refresh note");
        check(has_note_containing(output_plan.notes,
                  {"one prevalidated non-owning replacement lookup plan",
                      "one transform pass",
                      "without rebuilding selector lookup",
                      "reparsing bounded replacement cell payloads"}),
            "cell replacement linked output plan should expose single-pass lookup reuse");
        check(has_note_containing(output_plan.notes,
                  {"worksheet relationships are preserved", "policy review"}),
            "cell replacement linked output plan should expose relationship preservation note");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
            fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
            "cell replacement linked output plan should stream-rewrite worksheet");
        check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
            fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
            "cell replacement linked output plan should local-rewrite workbook metadata");
        check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
            fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
            "cell replacement linked output plan should rewrite workbook relationships");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
            "cell replacement linked output plan should rewrite content types");
        check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
            "cell replacement linked output plan should omit stale calcChain");
        check_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawing1.vml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "cell replacement linked output plan should preserve VML drawing");
        check_output_entry_plan(output_plan.entries, "xl/drawings/drawing space.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "cell replacement linked output plan should preserve percent-decoded drawing");
        check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "cell replacement linked output plan should preserve sharedStrings");
        check_output_entry_plan(output_plan.entries, "xl/_rels/sharedStrings.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "cell replacement linked output plan should preserve sharedStrings owner relationships");
        check_output_entry_part_context(output_plan.entries,
            "xl/_rels/sharedStrings.xml.rels", false, "",
            "cell replacement linked output plan should classify sharedStrings relationships as metadata");
        check_output_entry_plan(output_plan.entries, "xl/styles.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "cell replacement linked output plan should preserve styles");
        check_output_entry_plan(output_plan.entries, "xl/vbaProject.bin",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "cell replacement linked output plan should preserve VBA project");
        check_output_entry_plan(output_plan.entries, "custom/_rels/opaque-extension.bin.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "cell replacement linked output plan should preserve unknown extension relationships");
        check_output_entry_part_context(output_plan.entries,
            "custom/_rels/opaque-extension.bin.rels", false, "",
            "cell replacement linked output plan should classify unknown extension relationships as metadata");
        check_output_entry_relationship_context(output_plan.entries,
            "xl/drawings/drawing1.xml", worksheet_part.value(), "rId1",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
            "../drawings/drawing1.xml",
            "cell replacement linked output plan should keep drawing relationship audit");
        check_output_entry_relationship_context(output_plan.entries, "xl/charts/chart1.xml",
            drawing_part.value(), "rId2",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart",
            "../charts/chart1.xml",
            "cell replacement linked output plan should keep chart relationship audit");
        check_output_entry_relationship_context(output_plan.entries, "xl/media/image1.png",
            drawing_part.value(), "rId1",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image",
            "../media/image1.png",
            "cell replacement linked output plan should keep image relationship audit");
        check_output_entry_relationship_context(output_plan.entries, "xl/tables/table1.xml",
            worksheet_part.value(), "rId3",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/table",
            "../tables/table1.xml",
            "cell replacement linked output plan should keep table relationship audit");
        check_output_entry_relationship_context(output_plan.entries,
            "xl/drawings/vmlDrawing1.vml", worksheet_part.value(), "rId7",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing",
            "../drawings/vmlDrawing1.vml#shape1",
            "cell replacement linked output plan should keep VML relationship audit");
        check_output_entry_relationship_context(output_plan.entries,
            "xl/drawings/drawing space.xml", worksheet_part.value(), "rId8",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
            "../drawings/drawing%20space.xml",
            "cell replacement linked output plan should keep percent-decoded drawing audit");
        check_output_entry_relationship_context(output_plan.entries,
            "custom/opaque-extension.bin", worksheet_part.value(), "rId9",
            "https://fastxlsx.invalid/relationships/opaque-extension",
            "../../custom/opaque-extension.bin",
            "cell replacement linked output plan should keep unknown extension audit");
        check_output_entry_relationship_context(output_plan.entries,
            "xl/sharedStrings.xml", "", "", "", "",
            "cell replacement sharedStrings output plan should not invent relationship audit");
        check_output_entry_relationship_context(output_plan.entries, "xl/styles.xml",
            "", "", "", "",
            "cell replacement styles output plan should not invent relationship audit");

        editor.save_as(output);
    }

    check_no_new_package_editor_temp_files(temp_files_before,
        "cell replacement linked fixture should clean temporary XML files");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1"/>)",
        "cell replacement linked output should refresh worksheet dimension");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>linked patch</t></is></c>)",
        "cell replacement linked output should contain replacement cell");
    check_not_contains(worksheet_xml, R"(<c r="A1"><v>1</v></c>)",
        "cell replacement linked output should omit old target cell");
    check_contains(worksheet_xml, R"(<drawing r:id="rId1"/>)",
        "cell replacement linked output should preserve drawing reference");
    check_contains(worksheet_xml,
        R"(<tableParts count="1"><tablePart r:id="rId3"/></tableParts>)",
        "cell replacement linked output should preserve tableParts reference");
    check(output_reader.find_entry("xl/calcChain.xml") == nullptr,
        "cell replacement linked output should omit stale calcChain payload");

    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "cell replacement linked output should byte-preserve worksheet relationships");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "cell replacement linked output should byte-preserve drawing XML");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "cell replacement linked output should byte-preserve drawing relationships");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "cell replacement linked output should byte-preserve chart XML");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "cell replacement linked output should byte-preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "cell replacement linked output should byte-preserve table XML");
    check(output_reader.read_entry("xl/drawings/vmlDrawing1.vml")
            == source.vml_drawing,
        "cell replacement linked output should byte-preserve VML drawing");
    check(output_reader.read_entry("xl/drawings/drawing space.xml")
            == source.percent_encoded_drawing,
        "cell replacement linked output should byte-preserve percent-decoded drawing");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "cell replacement linked output should byte-preserve sharedStrings XML");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "cell replacement linked output should byte-preserve sharedStrings relationships");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "cell replacement linked output should byte-preserve styles XML");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "cell replacement linked output should byte-preserve VBA bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "cell replacement linked output should byte-preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "cell replacement linked output should byte-preserve unknown extension relationships");

    const auto* sheet_relationships = output_reader.relationships_for(worksheet_part);
    check(sheet_relationships != nullptr,
        "cell replacement linked output should keep worksheet relationships readable");
    check(sheet_relationships->find_by_id("rId1") != nullptr,
        "cell replacement linked output should keep drawing relationship readable");
    check(sheet_relationships->find_by_id("rId3") != nullptr,
        "cell replacement linked output should keep table relationship readable");
    check(sheet_relationships->find_by_id("rId8") != nullptr,
        "cell replacement linked output should keep percent-encoded relationship readable");
    check(sheet_relationships->find_by_id("rId9") != nullptr,
        "cell replacement linked output should keep unknown extension relationship readable");
    const auto* drawing_relationships = output_reader.relationships_for(drawing_part);
    check(drawing_relationships != nullptr,
        "cell replacement linked output should keep drawing relationships readable");
    check(drawing_relationships->find_by_id("rId1") != nullptr,
        "cell replacement linked output should keep image relationship readable");
    check(drawing_relationships->find_by_id("rId2") != nullptr,
        "cell replacement linked output should keep chart relationship readable");
    const auto* shared_strings_relationships =
        output_reader.relationships_for(shared_strings_part);
    check(shared_strings_relationships != nullptr,
        "cell replacement linked output should keep sharedStrings relationships readable");
    check(shared_strings_relationships->find_by_id("rIdSharedExternal") != nullptr,
        "cell replacement linked output should keep sharedStrings external relationship");
    const auto* opaque_extension_relationships =
        output_reader.relationships_for(opaque_extension_part);
    check(opaque_extension_relationships != nullptr,
        "cell replacement linked output should keep unknown extension relationships readable");
    check(opaque_extension_relationships->find_by_id("rIdOpaqueExternal") != nullptr,
        "cell replacement linked output should keep unknown extension external relationship");

    const std::string workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_contains(workbook_relationships, "relationships/sharedStrings",
        "cell replacement linked output should preserve sharedStrings workbook relationship");
    check_contains(workbook_relationships, "relationships/styles",
        "cell replacement linked output should preserve styles workbook relationship");
    check_contains(workbook_relationships, "relationships/vbaProject",
        "cell replacement linked output should preserve VBA workbook relationship");
    check_not_contains(workbook_relationships, "relationships/calcChain",
        "cell replacement linked output should remove calcChain workbook relationship");

    check(output_reader.content_types().override_for(calc_chain_part) == nullptr,
        "cell replacement linked output should remove calcChain content type");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "cell replacement linked output should preserve sharedStrings content type");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "cell replacement linked output should preserve styles content type");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "cell replacement linked output should preserve VBA content type");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "cell replacement linked output should preserve table content type");
    check(output_reader.content_types().default_for("png") != nullptr,
        "cell replacement linked output should preserve PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "cell replacement linked output should not promote PNG media to override");

    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, "ReportRange",
        "cell replacement linked output should preserve workbook definedNames");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "cell replacement linked output should request full calculation");
}

void test_package_editor_worksheet_cell_replacement_skips_old_target_cell_payload_audit()
{
    CalcSourcePackage source =
        write_calc_source_package(
            "fastxlsx-package-editor-cell-replacement-skip-old-audit-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-cell-replacement-skip-old-audit-output.xlsx");
    source.worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1">)"
        R"(<c r="A1" t="s" s="1"><f>SUM(B1:B1)</f><drawing r:id="rIdGhost"/><v>0</v></c>)"
        R"(<c r="B1"><v>2</v></c>)"
        R"(</row></sheetData></worksheet>)";
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
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const std::array replacements {
        worksheet_cell_replacement("A1", R"(<c r="A1"><v>9</v></c>)"),
    };

    editor.replace_worksheet_cells_by_name("Sheet1", replacements);

    using PayloadAuditKind = fastxlsx::detail::WorksheetPayloadDependencyAuditKind;
    using PayloadAuditScope = fastxlsx::detail::WorksheetPayloadDependencyAuditScope;
    const auto& payload_audits =
        editor.edit_plan().worksheet_payload_dependency_audits();
    check(!has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::SharedStrings,
              PayloadAuditScope::WorksheetReplacement, "c"),
        "cell replacement should skip old target cell sharedStrings audit");
    check(!has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::Styles,
              PayloadAuditScope::WorksheetReplacement, "c"),
        "cell replacement should skip old target cell styles audit");
    check(!has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::Formula,
              PayloadAuditScope::WorksheetReplacement, "f"),
        "cell replacement should skip old target cell formula audit");
    check(editor.edit_plan().worksheet_relationship_reference_audits().empty(),
        "cell replacement should skip old target cell relationship references");
    check(!has_note_containing(editor.edit_plan().notes(), {"rIdGhost"}),
        "cell replacement should not audit relationship ids from the skipped old target cell");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string worksheet_xml = output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<c r="A1"><v>9</v></c>)",
        "cell replacement skip-audit output should include replacement cell");
    check_contains(worksheet_xml, R"(<c r="B1"><v>2</v></c>)",
        "cell replacement skip-audit output should preserve non-target cell");
    check_not_contains(worksheet_xml, "rIdGhost",
        "cell replacement skip-audit output should omit old target cell XML");
}

void test_package_editor_worksheet_cell_replacement_audits_replacement_payload_policy()
{
    const CalcSourcePackage source =
        write_calc_source_package(
            "fastxlsx-package-editor-cell-replacement-payload-audit-source.xlsx");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const std::array replacements {
        worksheet_cell_replacement(
            "A1",
            R"(<c r="A1" t="s" s="1"><sheetViews/><f>SUM(A1:A1)</f><v>0</v></c>)"),
    };

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    editor.replace_worksheet_cells_by_name("Sheet1", replacements);

    using PayloadAuditKind = fastxlsx::detail::WorksheetPayloadDependencyAuditKind;
    using PayloadAuditScope = fastxlsx::detail::WorksheetPayloadDependencyAuditScope;
    const auto& payload_audits =
        editor.edit_plan().worksheet_payload_dependency_audits();
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::SharedStrings,
              PayloadAuditScope::WorksheetReplacement, "c", {"shared string indexes"}),
        "cell replacement should audit replacement shared string indexes");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::Styles,
              PayloadAuditScope::WorksheetReplacement, "c", {"style id references"}),
        "cell replacement should audit replacement style ids");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::Formula,
              PayloadAuditScope::WorksheetReplacement, "f", {"contains formulas"}),
        "cell replacement should audit replacement formulas");
    check(!has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::RangeMetadata,
              PayloadAuditScope::WorksheetReplacement, "sheetViews"),
        "cell replacement should not audit replacement cell payload as worksheet metadata");

    fastxlsx::detail::ReferencePolicy fail_policy;
    fail_policy.unsupported_linked_part_action =
        fastxlsx::detail::ReferencePolicyAction::Fail;
    fastxlsx::detail::PackageEditor fail_editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    const std::size_t initial_plan_size = fail_editor.edit_plan().size();
    const std::size_t initial_note_count = fail_editor.edit_plan().notes().size();
    const std::size_t initial_payload_audit_count =
        fail_editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::vector<std::filesystem::path> payload_temp_files_before =
        package_editor_temp_files();

    bool failed = false;
    try {
        fail_editor.replace_worksheet_cells_by_name("Sheet1", replacements, fail_policy);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "payload dependencies blocked by reference policy",
            "cell replacement payload policy failure should name reference policy");
    }
    check(failed,
        "ReferencePolicyAction::Fail should reject cell replacement payload dependencies");
    check(fail_editor.edit_plan().size() == initial_plan_size,
        "cell replacement payload policy failure should not change edit plan size");
    check(fail_editor.edit_plan().notes().size() == initial_note_count,
        "cell replacement payload policy failure should not add notes");
    check(fail_editor.edit_plan().worksheet_payload_dependency_audits().size()
            == initial_payload_audit_count,
        "cell replacement payload policy failure should not add payload audits");
    check(!fail_editor.edit_plan().full_calculation_on_load(),
        "cell replacement payload policy failure should not request recalculation");
    check_manifest_write_mode(fail_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "cell replacement payload policy failure should keep worksheet copy-original");
    check_manifest_write_mode(fail_editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "cell replacement payload policy failure should keep workbook copy-original");
    check_manifest_write_mode(fail_editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "cell replacement payload policy failure should keep calcChain copy-original");
    check_no_new_package_editor_temp_files(payload_temp_files_before,
        "cell replacement payload policy failure should clean staged output temp file immediately");

    const std::array relationship_replacements {
        worksheet_cell_replacement(
            "A1",
            R"(<c r="A1" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><drawing r:id="rIdMissing"/></c>)"),
    };
    fastxlsx::detail::PackageEditor relationship_fail_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const std::vector<std::filesystem::path> temp_files_before =
        package_editor_temp_files();
    const std::size_t initial_relationship_plan_size =
        relationship_fail_editor.edit_plan().size();
    const std::size_t initial_relationship_note_count =
        relationship_fail_editor.edit_plan().notes().size();
    const std::size_t initial_relationship_reference_audit_count =
        relationship_fail_editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t initial_relationship_payload_audit_count =
        relationship_fail_editor.edit_plan().worksheet_payload_dependency_audits().size();

    bool relationship_failed = false;
    try {
        relationship_fail_editor.replace_worksheet_cells_by_name(
            "Sheet1", relationship_replacements, fail_policy);
    } catch (const std::exception& error) {
        relationship_failed = true;
        check_contains(error.what(), "relationship references blocked by reference policy",
            "cell replacement relationship policy failure should name reference policy");
    }
    check(relationship_failed,
        "ReferencePolicyAction::Fail should reject cell replacement relationship references");
    check(relationship_fail_editor.edit_plan().size() == initial_relationship_plan_size,
        "cell replacement relationship policy failure should not change edit plan size");
    check(relationship_fail_editor.edit_plan().notes().size()
            == initial_relationship_note_count,
        "cell replacement relationship policy failure should not add notes");
    check(relationship_fail_editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_relationship_reference_audit_count,
        "cell replacement relationship policy failure should not add relationship audits");
    check(relationship_fail_editor.edit_plan().worksheet_payload_dependency_audits().size()
            == initial_relationship_payload_audit_count,
        "cell replacement relationship policy failure should not add payload audits");
    check(!relationship_fail_editor.edit_plan().full_calculation_on_load(),
        "cell replacement relationship policy failure should not request recalculation");
    check_manifest_write_mode(relationship_fail_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "cell replacement relationship policy failure should keep worksheet copy-original");
    check_manifest_write_mode(relationship_fail_editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "cell replacement relationship policy failure should keep workbook copy-original");
    check_manifest_write_mode(relationship_fail_editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "cell replacement relationship policy failure should keep calcChain copy-original");
    check_no_new_package_editor_temp_files(temp_files_before,
        "cell replacement relationship policy failure should clean staged output temp file immediately");

    const std::array<std::string_view, 2> relationship_scanner_split_chunks {
        R"(<c r="A1" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><dra)",
        R"(wing r:id="rIdMissing"/></c>)",
    };
    const std::array split_relationship_scan_replacements {
        chunked_worksheet_cell_replacement("A1", relationship_scanner_split_chunks),
    };
    fastxlsx::detail::PackageEditor split_relationship_scan_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    split_relationship_scan_editor.replace_worksheet_cells_by_name(
        "Sheet1", split_relationship_scan_replacements);
    check(!has_note_containing(split_relationship_scan_editor.edit_plan().notes(),
              {"relationship-id audit", "could not parse"}),
        "cell replacement output-side relationship scanner should keep split tags parseable");
    check(has_note_containing(split_relationship_scan_editor.edit_plan().notes(),
              {"relationship id rIdMissing", "source worksheet relationships are missing"}),
        "split replacement cell relationship should produce the specific missing-relationships audit");
    const auto& split_relationship_audits =
        split_relationship_scan_editor.edit_plan().worksheet_relationship_reference_audits();
    check(std::any_of(split_relationship_audits.begin(), split_relationship_audits.end(),
              [&](const fastxlsx::detail::WorksheetRelationshipReferenceAudit& audit) {
                  return audit.worksheet_part == worksheet_part
                      && audit.kind
                          == fastxlsx::detail::WorksheetRelationshipReferenceAuditKind::MissingRelationships
                      && audit.element == "drawing"
                      && audit.relationship_id == "rIdMissing";
              }),
        "split replacement cell relationship should record structured missing-relationships audit");

    const std::array<std::string_view, 3> cdata_relationship_text_chunks {
        R"(<c r="A1" t="inlineStr" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><is><t><![CDATA[literal > )",
        R"(<drawing r:id="rIdCdataText"/><f>not-a-formula</f>)",
        R"(]]></t></is></c>)",
    };
    const std::array cdata_relationship_text_replacements {
        chunked_worksheet_cell_replacement("A1", cdata_relationship_text_chunks),
    };
    fastxlsx::detail::PackageEditor cdata_relationship_text_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    cdata_relationship_text_editor.replace_worksheet_cells_by_name(
        "Sheet1", cdata_relationship_text_replacements);
    check(!has_note_containing(cdata_relationship_text_editor.edit_plan().notes(),
              {"rIdCdataText"}),
        "CDATA text inside replacement cell should not be scanned as a relationship reference");
    check(!has_payload_audit(
              cdata_relationship_text_editor.edit_plan().worksheet_payload_dependency_audits(),
              worksheet_part,
              PayloadAuditKind::Formula,
              PayloadAuditScope::WorksheetReplacement,
              "f"),
        "CDATA text inside replacement cell should not be scanned as formula markup");
    const auto& cdata_relationship_audits =
        cdata_relationship_text_editor.edit_plan().worksheet_relationship_reference_audits();
    check(std::none_of(cdata_relationship_audits.begin(), cdata_relationship_audits.end(),
              [](const fastxlsx::detail::WorksheetRelationshipReferenceAudit& audit) {
                  return audit.relationship_id == "rIdCdataText";
              }),
        "CDATA relationship-like text should not produce structured relationship audit");

    const std::array<std::string_view, 3> pi_relationship_text_chunks {
        R"(<c r="A1" t="inlineStr" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><is><?fastxlsx literal > )",
        R"(<drawing r:id="rIdPiText"/><f>not-a-formula</f>)",
        R"(?></is></c>)",
    };
    const std::array pi_relationship_text_replacements {
        chunked_worksheet_cell_replacement("A1", pi_relationship_text_chunks),
    };
    fastxlsx::detail::PackageEditor pi_relationship_text_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    pi_relationship_text_editor.replace_worksheet_cells_by_name(
        "Sheet1", pi_relationship_text_replacements);
    check(!has_note_containing(pi_relationship_text_editor.edit_plan().notes(),
              {"rIdPiText"}),
        "processing instruction text inside replacement cell should not be scanned as a relationship reference");
    check(!has_payload_audit(
              pi_relationship_text_editor.edit_plan().worksheet_payload_dependency_audits(),
              worksheet_part,
              PayloadAuditKind::Formula,
              PayloadAuditScope::WorksheetReplacement,
              "f"),
        "processing instruction text inside replacement cell should not be scanned as formula markup");
    const auto& pi_relationship_audits =
        pi_relationship_text_editor.edit_plan().worksheet_relationship_reference_audits();
    check(std::none_of(pi_relationship_audits.begin(), pi_relationship_audits.end(),
              [](const fastxlsx::detail::WorksheetRelationshipReferenceAudit& audit) {
                  return audit.relationship_id == "rIdPiText";
              }),
        "processing instruction relationship-like text should not produce structured relationship audit");
}

void test_replacement_cell_payload_scanner_streams_long_ignored_markup_chunks()
{
    struct IgnoredMarkupCase {
        std::string_view open;
        std::string_view close;
        const char* label;
    };

    const std::array<IgnoredMarkupCase, 3> cases {{
        {"<!--", "-->", "comment"},
        {"<![CDATA[", "]]>", "CDATA"},
        {"<?fastxlsx ", "?>", "processing instruction"},
    }};

    for (const IgnoredMarkupCase& test_case : cases) {
        std::string payload = R"(<c r="A1">)";
        payload += test_case.open;
        payload.append(
            fastxlsx::detail::package_editor_cell_replacement_event_window_byte_limit
                + 4096U,
            'x');
        payload += R"(<drawing r:id="rIdIgnored"/><f>ignored</f>)";
        payload += test_case.close;
        payload += R"(<f>real-formula</f></c>)";

        const std::array<std::string_view, 1> chunks {payload};
        const auto scan_result =
            fastxlsx::detail::testing_scan_replacement_cell_payload_start_tags(
                fastxlsx::detail::WorksheetCellReplacementPayload::from_chunks(chunks));

        check(scan_result.start_tag_count == 2,
            "long ignored markup scanner should resume after ignored payload");
        check(scan_result.formula_tag_count == 1,
            "long ignored markup scanner should only count real formula tags");
        check(scan_result.relationship_reference_tag_count == 0,
            "long ignored markup scanner should ignore fake relationship tags");
        (void)test_case.label;
    }
}

void test_sheet_data_start_tag_scanner_streams_long_ignored_markup_chunks()
{
    struct IgnoredMarkupCase {
        std::string_view open;
        std::string_view close;
        const char* label;
    };

    const std::array<IgnoredMarkupCase, 3> cases {{
        {"<!--", "-->", "comment"},
        {"<![CDATA[", "]]>", "CDATA"},
        {"<?fastxlsx ", "?>", "processing instruction"},
    }};

    for (const IgnoredMarkupCase& test_case : cases) {
        std::string payload =
            R"(<sheetData><row r="1"><c r="A1"><is><t>)";
        payload += test_case.open;
        payload.append(
            fastxlsx::detail::package_editor_cell_replacement_event_window_byte_limit
                + 4096U,
            'x');
        payload += R"(<c r="B1" t="s"/><f>ignored</f>)";
        payload += test_case.close;
        payload += R"(</t></is></c></row><row r="2"><c r="A2"><f>real-formula</f></c></row></sheetData>)";

        const std::array<std::string_view, 1> chunks {payload};
        const auto scan_result =
            fastxlsx::detail::testing_scan_sheet_data_start_tags_from_chunks(
                chunks,
                fastxlsx::detail::package_editor_cell_replacement_event_window_byte_limit);

        check(scan_result.start_tag_count == 8,
            "long sheetData ignored markup scanner should resume after ignored payload");
        check(scan_result.formula_tag_count == 1,
            "long sheetData ignored markup scanner should only count real formula tags");
        check(scan_result.shared_string_cell_tag_count == 0,
            "long sheetData ignored markup scanner should ignore fake shared-string cells");
        (void)test_case.label;
    }
}

void test_relationship_reference_scanner_streams_retained_tag_before_long_ignored_markup()
{
    const std::string first_chunk =
        R"(<c r="A1" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><dra)";
    std::string second_chunk = R"(wing r:id="rIdLongSplit"/><!--)";
    second_chunk.append(
        fastxlsx::detail::package_editor_cell_replacement_event_window_byte_limit + 4096U,
        'x');
    second_chunk += R"(<drawing r:id="rIdIgnored"/><f>ignored</f>--></c>)";

    const std::array<std::string_view, 2> chunks {first_chunk, second_chunk};
    const auto scan_result =
        fastxlsx::detail::testing_scan_worksheet_relationship_references_from_chunks(chunks);

    check(scan_result.elements.size() == 1,
        "relationship scanner should keep only the real retained split tag");
    check(scan_result.relationship_ids.size() == 1,
        "relationship scanner should emit one relationship id");
    check(scan_result.elements.front() == "drawing",
        "relationship scanner should preserve the retained split drawing tag");
    check(scan_result.relationship_ids.front() == "rIdLongSplit",
        "relationship scanner should skip fake ids inside following ignored markup");
}

void test_prevalidated_relationship_reference_scanner_limits_attribute_slow_paths()
{
    const std::array<std::string_view, 3> chunks {
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData><wrapper xmlns:r="urn:not-office-relationships"><hyper)",
        R"(link ref="A1" r:id="rIdWrong"/></wrapper><hyperlinks><hyperlink ref="B1" r:)",
        R"(id='rIdGood'/></hyperlinks></worksheet>)",
    };
    const auto scan_result = fastxlsx::detail::
        testing_scan_prevalidated_worksheet_relationship_references_from_chunks(chunks);

    check(scan_result.elements == std::vector<std::string> {"hyperlink"},
        "prevalidated relationship scanner should preserve namespace shadowing");
    check(scan_result.relationship_ids == std::vector<std::string> {"rIdGood"},
        "prevalidated relationship scanner should accept single-quoted relationship ids");
    check(scan_result.input_call_count == chunks.size(),
        "prevalidated relationship scanner input call telemetry mismatch");
    check(scan_result.input_bytes
            == std::accumulate(chunks.begin(), chunks.end(), std::uint64_t {0},
                [](std::uint64_t total, std::string_view chunk) {
                    return total + static_cast<std::uint64_t>(chunk.size());
                }),
        "prevalidated relationship scanner input byte telemetry mismatch");
    check(scan_result.boundary_carry_count == 2,
        "prevalidated relationship scanner should report both split-tag carries");
    check(scan_result.slow_path_tag_count == 4,
        "prevalidated relationship scanner should only parse namespace and relationship tags");
}

void test_package_entry_chunk_reader_rejects_stale_memory_chunk_size()
{
    const std::string valid_payload = "memory-staged-payload";
    std::vector<fastxlsx::detail::PackageEntryChunk> valid_chunks;
    valid_chunks.push_back(fastxlsx::detail::PackageEntryChunk::memory(valid_payload));
    check(fastxlsx::detail::testing_read_package_entry_chunks_to_string(valid_chunks)
            == valid_payload,
        "test hook should replay a valid memory staged chunk");

    std::vector<fastxlsx::detail::PackageEntryChunk> stale_chunks;
    stale_chunks.push_back(
        fastxlsx::detail::PackageEntryChunk::memory(valid_payload));
    stale_chunks.front().expected_size = 1;
    stale_chunks.front().has_expected_size = true;
    stale_chunks.front().has_expected_crc32 = false;

    bool failed = false;
    try {
        (void)fastxlsx::detail::testing_read_package_entry_chunks_to_string(stale_chunks);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "staged package-entry chunk 0 (memory)",
            "stale memory chunk size failure should identify the memory chunk");
        check_contains(error.what(),
            "staged package-entry chunk size changed after validation",
            "stale memory chunk size failure should name the size contract");
        check_contains(error.what(), "expected 1 bytes",
            "stale memory chunk size failure should report expected bytes");
        check_contains(error.what(),
            "actual " + std::to_string(valid_payload.size()) + " bytes",
            "stale memory chunk size failure should report actual bytes");
        check_staged_chunk_replay_cursor(error.what(), 0, 1,
            "stale memory chunk size failure should report staged replay cursor");
        check_staged_chunk_expected_size(error.what(), 1,
            "stale memory chunk size failure should report current chunk expected bytes");
        check_staged_chunk_expected_total(error.what(), 1,
            "stale memory chunk size failure should report expected staged total");
        check_staged_chunk_expected_remaining(error.what(), 1,
            "stale memory chunk size failure should report expected staged remaining bytes");
        check_staged_chunk_replay_progress(error.what(), 1, 0, 0, 0,
            "stale memory chunk size failure should report staged replay progress");
    }

    check(failed,
        "staged memory chunk reader should reject stale expected-size metadata");

    const std::string prefix_payload = "prefix-memory-chunk";
    std::vector<fastxlsx::detail::PackageEntryChunk> stale_second_chunks;
    stale_second_chunks.push_back(
        fastxlsx::detail::PackageEntryChunk::memory(prefix_payload));
    stale_second_chunks.push_back(
        fastxlsx::detail::PackageEntryChunk::memory(valid_payload));
    stale_second_chunks.back().expected_size = 1;
    stale_second_chunks.back().has_expected_size = true;
    stale_second_chunks.back().has_expected_crc32 = false;

    failed = false;
    try {
        (void)fastxlsx::detail::testing_read_package_entry_chunks_to_string(
            stale_second_chunks);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "staged package-entry chunk 1 (memory)",
            "second stale memory chunk failure should identify the failing chunk");
        check_staged_chunk_replay_cursor(error.what(), 1, 2,
            "second stale memory chunk failure should report staged replay cursor");
        check_staged_chunk_expected_size(error.what(), 1,
            "second stale memory chunk failure should report current chunk expected bytes");
        check_staged_chunk_expected_total(error.what(),
            static_cast<std::uint64_t>(prefix_payload.size() + 1U),
            "second stale memory chunk failure should report expected staged total");
        check_staged_chunk_expected_remaining(error.what(), 1,
            "second stale memory chunk failure should report expected staged remaining bytes");
        check_staged_chunk_replay_progress(error.what(),
            2,
            1,
            static_cast<std::uint64_t>(prefix_payload.size()),
            static_cast<std::uint64_t>(prefix_payload.size()),
            "second stale memory chunk failure should report prior staged replay progress");
    }

    check(failed,
        "staged memory chunk reader should report progress after a prior emitted chunk");
}

void test_package_entry_chunk_reader_reports_replay_cursor_after_prior_chunks()
{
    const std::string first_payload = "first-memory-chunk";
    const std::string second_payload = "second-memory-chunk";
    const std::string third_payload = "third-memory-chunk";
    std::vector<fastxlsx::detail::PackageEntryChunk> chunks;
    chunks.push_back(fastxlsx::detail::PackageEntryChunk::memory(first_payload));
    chunks.push_back(fastxlsx::detail::PackageEntryChunk::memory(second_payload));
    chunks.push_back(fastxlsx::detail::PackageEntryChunk::memory(third_payload));
    chunks.back().expected_size = 1;
    chunks.back().has_expected_size = true;
    chunks.back().has_expected_crc32 = false;

    bool failed = false;
    try {
        (void)fastxlsx::detail::testing_read_package_entry_chunks_to_string(chunks);
    } catch (const std::exception& error) {
        failed = true;
        const std::uint64_t emitted_bytes = static_cast<std::uint64_t>(
            first_payload.size() + second_payload.size());
        check_contains(error.what(), "staged package-entry chunk 2 (memory)",
            "third stale memory chunk failure should identify the memory chunk");
        check_staged_chunk_replay_cursor(error.what(), 2, 3,
            "third stale memory chunk failure should report staged replay cursor");
        check_staged_chunk_expected_size(error.what(), 1,
            "third stale memory chunk failure should report current chunk expected bytes");
        check_staged_chunk_expected_total(error.what(),
            static_cast<std::uint64_t>(
                first_payload.size() + second_payload.size() + 1U),
            "third stale memory chunk failure should report expected staged total");
        check_staged_chunk_expected_remaining(error.what(), 1,
            "third stale memory chunk failure should report expected staged remaining bytes");
        check_staged_chunk_replay_progress(error.what(),
            3,
            2,
            emitted_bytes,
            static_cast<std::uint64_t>(second_payload.size()),
            "third stale memory chunk failure should report prior replay progress");
        check_contains(error.what(),
            "staged package-entry chunk size changed after validation",
            "third stale memory chunk failure should name the size contract");
    }

    check(failed,
        "staged chunk reader should report replay cursor after prior emitted chunks");
}

void test_package_entry_chunk_reader_rejects_stale_empty_memory_chunk_crc()
{
    std::vector<fastxlsx::detail::PackageEntryChunk> valid_chunks;
    valid_chunks.push_back(fastxlsx::detail::PackageEntryChunk::memory(""));
    check(fastxlsx::detail::testing_read_package_entry_chunks_to_string(valid_chunks).empty(),
        "test hook should replay an empty memory staged chunk");

    std::vector<fastxlsx::detail::PackageEntryChunk> stale_chunks;
    stale_chunks.push_back(fastxlsx::detail::PackageEntryChunk::memory(""));
    stale_chunks.front().expected_crc32 = 123U;
    stale_chunks.front().has_expected_crc32 = true;

    bool failed = false;
    try {
        (void)fastxlsx::detail::testing_read_package_entry_chunks_to_string(stale_chunks);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "staged package-entry chunk 0 (memory)",
            "stale empty memory chunk CRC failure should identify the memory chunk");
        check_contains(error.what(),
            "staged package-entry chunk CRC32 changed after validation",
            "stale empty memory chunk CRC failure should name the CRC contract");
        check_contains(error.what(), "expected 123",
            "stale empty memory chunk CRC failure should report expected CRC");
        check_contains(error.what(), "actual 0",
            "stale empty memory chunk CRC failure should report actual CRC");
        check_staged_chunk_replay_cursor(error.what(), 0, 1,
            "stale empty memory chunk CRC failure should report staged replay cursor");
        check_staged_chunk_expected_size(error.what(), 0,
            "stale empty memory chunk CRC failure should report current chunk expected bytes");
        check_staged_chunk_expected_total(error.what(), 0,
            "stale empty memory chunk CRC failure should report expected staged total");
        check_staged_chunk_expected_remaining(error.what(), 0,
            "stale empty memory chunk CRC failure should report expected staged remaining bytes");
        check_staged_chunk_replay_progress(error.what(), 1, 0, 0, 0,
            "stale empty memory chunk CRC failure should report staged replay progress");
    }

    check(failed,
        "staged empty memory chunk reader should reject stale expected-CRC metadata");
}

void test_package_entry_chunk_reader_reports_unknown_chunk_kind()
{
    std::vector<fastxlsx::detail::PackageEntryChunk> chunks;
    chunks.push_back(fastxlsx::detail::PackageEntryChunk::memory("payload"));
    chunks.front().kind = static_cast<fastxlsx::detail::PackageEntryChunk::Kind>(99);

    bool failed = false;
    try {
        (void)fastxlsx::detail::testing_read_package_entry_chunks_to_string(chunks);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "staged package-entry chunk 0 (unknown)",
            "unknown staged chunk kind failure should identify the chunk kind");
        check_contains(error.what(), "unsupported staged package-entry chunk kind",
            "unknown staged chunk kind failure should preserve the unsupported-kind detail");
        check_staged_chunk_replay_cursor(error.what(), 0, 1,
            "unknown staged chunk kind failure should report staged replay cursor");
        check_staged_chunk_expected_size(error.what(), 0,
            "unknown staged chunk kind failure should report current chunk expected bytes");
        check_staged_chunk_expected_total(error.what(), 0,
            "unknown staged chunk kind failure should report expected staged total");
        check_staged_chunk_expected_remaining(error.what(), 0,
            "unknown staged chunk kind failure should report expected staged remaining bytes");
        check_staged_chunk_replay_progress(error.what(), 1, 0, 0, 0,
            "unknown staged chunk kind failure should report staged replay progress");
    }

    check(failed, "staged chunk reader should reject unknown chunk kinds");
}

void test_package_entry_chunk_reader_closes_file_on_early_exit()
{
    const std::filesystem::path chunk_path =
        output_path("fastxlsx-package-editor-staged-reader-early-exit.xml");
    write_binary_file(chunk_path, std::string(100000, 'x'));

    {
        std::vector<fastxlsx::detail::PackageEntryChunk> chunks;
        chunks.push_back(fastxlsx::detail::PackageEntryChunk::file(chunk_path));
        const std::string first_chunk =
            fastxlsx::detail::testing_read_first_package_entry_chunk_for_lifecycle(chunks);
        check(!first_chunk.empty(),
            "test hook should read one chunk from the staged file-backed chunk");
    }

    std::error_code error;
    std::filesystem::remove(chunk_path, error);
    check(!error && !std::filesystem::exists(chunk_path),
        "staged chunk reader should close file handles when destroyed before EOF");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "c5-linked")) {
            test_package_editor_worksheet_cell_replacement_preserves_linked_object_parts();
            test_package_editor_worksheet_cell_replacement_skips_old_target_cell_payload_audit();
            test_package_editor_worksheet_cell_replacement_audits_replacement_payload_policy();
            test_replacement_cell_payload_scanner_streams_long_ignored_markup_chunks();
            test_sheet_data_start_tag_scanner_streams_long_ignored_markup_chunks();
            test_relationship_reference_scanner_streams_retained_tag_before_long_ignored_markup();
            test_prevalidated_relationship_reference_scanner_limits_attribute_slow_paths();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
