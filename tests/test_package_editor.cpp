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
    return shard == "all" || shard == "core" || shard == "c5"
        || shard == "policy";
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

void test_package_editor_noop_save_preserves_all_source_entries()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-noop-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-noop-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    check(editor.edit_plan().size() == editor.manifest().size(),
        "no-op edit plan should include one part entry per manifest part");
    for (const fastxlsx::detail::PackagePart& part : editor.manifest().parts()) {
        const auto* plan_entry = editor.edit_plan().find_part(part.name);
        check(plan_entry != nullptr,
            "no-op edit plan should include every manifest part");
        check(plan_entry->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "no-op edit plan should keep manifest parts copy-original");
        check(part.write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal
                && part.preserve_original && !part.dirty && !part.generated,
            "no-op manifest should keep source parts copy-original");
    }
    check(editor.edit_plan().package_entries().empty(),
        "no-op edit plan should not record metadata package-entry rewrites");
    check(editor.edit_plan().removed_parts().empty(),
        "no-op edit plan should not remove parts");
    check(editor.edit_plan().removed_package_entries().empty(),
        "no-op edit plan should not omit package entries");
    check(!editor.edit_plan().full_calculation_on_load(),
        "no-op edit plan should not request full calculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "no-op edit plan should preserve calcChain policy");
    check(editor.edit_plan().notes().empty(),
        "no-op edit plan should not add dependency audit notes");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "no-op output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "no-op output plan should preserve calcChain policy");
    check(output_plan.notes.empty(),
        "no-op output plan should not carry audit notes");
    check(output_plan.relationship_target_audits.empty(),
        "no-op output plan should not carry relationship target audits");
    check(output_plan.entries.size() == editor.reader().entries().size(),
        "no-op output plan should include one decision per source entry");
    check(editor.planned_output_entries().size() == output_plan.entries.size(),
        "legacy output-entry preview should match aggregate output plan entries");
    for (const fastxlsx::detail::PackageReaderEntry& entry : editor.reader().entries()) {
        check_output_entry_plan(output_plan.entries, entry.name,
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "no-op output plan should copy every source entry");
    }
    check_source_package_parts_are_file_backed(editor.reader(), output_plan.entries,
        "no-op output plan should file-back every source package part copy");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "no-op output plan should classify content types as metadata entry");
    check_source_metadata_entry_is_file_backed(output_plan.entries, "[Content_Types].xml",
        "content types",
        "no-op output plan should expose file-backed content-types metadata copy");
    check_source_metadata_entry_is_file_backed(output_plan.entries, "_rels/.rels",
        "package relationships",
        "no-op output plan should expose file-backed package relationships metadata copy");
    check_source_metadata_entry_is_file_backed(output_plan.entries, "xl/_rels/workbook.xml.rels",
        "relationships",
        "no-op output plan should expose file-backed workbook relationships metadata copy");
    check_output_entry_part_context(output_plan.entries, "xl/workbook.xml", true,
        "/xl/workbook.xml",
        "no-op output plan should classify workbook XML as a package part");
    const auto* workbook_output_plan =
        find_output_entry_plan(output_plan.entries, "xl/workbook.xml");
    check(workbook_output_plan != nullptr && workbook_output_plan->file_backed_source_copy,
        "no-op output plan should copy workbook source entries through file-backed chunks");
    check_contains(workbook_output_plan->file_backed_source_copy_reason, "package part",
        "workbook file-backed source-copy plan should explain the package-part reason");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/sheet1.xml", true,
        "/xl/worksheets/sheet1.xml",
        "no-op output plan should classify worksheet XML as a package part");
    const auto* worksheet_output_plan =
        find_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml");
    check(worksheet_output_plan != nullptr && worksheet_output_plan->file_backed_source_copy,
        "no-op output plan should copy worksheet source entries through file-backed chunks");
    check_contains(worksheet_output_plan->file_backed_source_copy_reason, "worksheet",
        "worksheet file-backed source-copy plan should explain the worksheet reason");
    check_output_entry_part_context(output_plan.entries, "xl/sharedStrings.xml", true,
        "/xl/sharedStrings.xml",
        "no-op output plan should classify sharedStrings as a package part");
    const auto* shared_strings_output_plan =
        find_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml");
    check(shared_strings_output_plan != nullptr
            && shared_strings_output_plan->file_backed_source_copy,
        "no-op output plan should copy sharedStrings source entries through file-backed chunks");
    check_contains(shared_strings_output_plan->file_backed_source_copy_reason, "sharedStrings",
        "sharedStrings file-backed source-copy plan should explain the sharedStrings reason");
    check_output_entry_part_context(output_plan.entries, "custom/opaque-extension.bin", true,
        "/custom/opaque-extension.bin",
        "no-op output plan should classify unknown extension as a package part");
    const auto* opaque_output_plan =
        find_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin");
    check(opaque_output_plan != nullptr && opaque_output_plan->file_backed_source_copy,
        "no-op output plan should copy unknown package parts through file-backed chunks");
    check_contains(opaque_output_plan->file_backed_source_copy_reason, "package part",
        "unknown package part file-backed source-copy plan should explain the generic reason");
    check_output_entry_part_context(output_plan.entries, "custom/_rels/opaque-extension.bin.rels",
        false, "",
        "no-op output plan should classify unknown owner relationships as metadata entry");
    check_source_metadata_entry_is_file_backed(output_plan.entries,
        "custom/_rels/opaque-extension.bin.rels", "relationships",
        "no-op output plan should expose file-backed unknown owner relationships metadata copy");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader);
    check_entry_bytes(output_reader, "[Content_Types].xml", source.content_types);
    check_entry_bytes(output_reader, "_rels/.rels", source.package_relationships);
    check_entry_bytes(output_reader, "xl/workbook.xml", source.workbook);
    check_entry_bytes(output_reader, "xl/_rels/workbook.xml.rels",
        source.workbook_relationships);
    check_entry_bytes(output_reader, "xl/worksheets/sheet1.xml", source.worksheet);
    check_entry_bytes(output_reader, "xl/worksheets/_rels/sheet1.xml.rels",
        source.worksheet_relationships);
    check_entry_bytes(output_reader, "xl/drawings/drawing1.xml", source.drawing);
    check_entry_bytes(output_reader, "xl/drawings/_rels/drawing1.xml.rels",
        source.drawing_relationships);
    check_entry_bytes(output_reader, "xl/charts/chart1.xml", source.chart);
    check_entry_bytes(output_reader, "xl/media/image1.png", source.media);
    check_entry_bytes(output_reader, "xl/tables/table1.xml", source.table);
    check_entry_bytes(output_reader, "xl/drawings/vmlDrawing1.vml", source.vml_drawing);
    check_entry_bytes(output_reader, "xl/drawings/drawing space.xml",
        source.percent_encoded_drawing);
    check_entry_bytes(output_reader, "xl/sharedStrings.xml", source.shared_strings);
    check_entry_bytes(output_reader, "xl/_rels/sharedStrings.xml.rels",
        source.shared_strings_relationships);
    check_entry_bytes(output_reader, "xl/styles.xml", source.styles);
    check_entry_bytes(output_reader, "xl/vbaProject.bin", source.vba_project);
    check_entry_bytes(output_reader, "xl/calcChain.xml", source.calc_chain);
    check_entry_bytes(output_reader, "custom/opaque-extension.bin",
        source.opaque_extension);
    check_entry_bytes(output_reader, "custom/_rels/opaque-extension.bin.rels",
        source.opaque_extension_relationships);
}

void test_package_editor_file_backs_copy_original_package_part_source_entries()
{
    LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-large-source-copy.xlsx");
    source.opaque_extension.assign(70U * 1024U, 'L');
    rewrite_linked_object_source_package(source);

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-large-source-copy-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_source_package_parts_are_file_backed(editor.reader(), output_plan.entries,
        "large source-copy output plan should file-back every source package part copy");
    check_output_entry_part_context(output_plan.entries, "custom/opaque-extension.bin", true,
        "/custom/opaque-extension.bin",
        "large source-copy output plan should classify unknown extension as a package part");
    const auto* opaque_output_plan =
        find_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin");
    check(opaque_output_plan != nullptr && opaque_output_plan->file_backed_source_copy,
        "large copy-original package parts should use file-backed source copy");
    check_contains(opaque_output_plan->file_backed_source_copy_reason, "package part",
        "large source-copy output plan should explain the package-part reason");

    const auto* media_output_plan =
        find_output_entry_plan(output_plan.entries, "xl/media/image1.png");
    check(media_output_plan != nullptr && media_output_plan->file_backed_source_copy,
        "small package parts should also use file-backed source copy");
    check_contains(media_output_plan->file_backed_source_copy_reason, "package part",
        "small package parts should carry a generic file-backed reason");
    const auto* workbook_output_plan =
        find_output_entry_plan(output_plan.entries, "xl/workbook.xml");
    check(workbook_output_plan != nullptr && workbook_output_plan->file_backed_source_copy,
        "workbook XML source-copy output should use file-backed source copy");
    check_contains(workbook_output_plan->file_backed_source_copy_reason, "package part",
        "workbook XML source-copy output should carry a generic file-backed reason");
    const auto* opaque_rels_output_plan =
        find_output_entry_plan(output_plan.entries, "custom/_rels/opaque-extension.bin.rels");
    check(opaque_rels_output_plan != nullptr
            && opaque_rels_output_plan->file_backed_source_copy,
        "metadata relationship entries should use file-backed source copy");
    check(opaque_rels_output_plan != nullptr
            && !opaque_rels_output_plan->file_backed_source_copy_reason.empty(),
        "metadata relationship entries should carry a file-backed source-copy reason");
    check_source_metadata_entry_is_file_backed(output_plan.entries,
        "custom/_rels/opaque-extension.bin.rels", "relationships",
        "metadata relationship entries should expose file-backed metadata source-copy reason");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader);
    check_entry_bytes(output_reader, "custom/opaque-extension.bin",
        source.opaque_extension);
    check_entry_bytes(output_reader, "xl/media/image1.png", source.media);
    check_entry_bytes(output_reader, "xl/workbook.xml", source.workbook);
    check_entry_bytes(output_reader, "custom/_rels/opaque-extension.bin.rels",
        source.opaque_extension_relationships);
}

void test_package_editor_replaces_one_part_and_preserves_unknown_parts()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    check(editor.manifest().find_part(core_part) != nullptr,
        "PackageEditor manifest should include core properties");
    const auto* initial_unknown_plan = editor.edit_plan().find_part(unknown_part);
    check(initial_unknown_plan != nullptr,
        "PackageEditor initial plan should include unknown part");
    check(initial_unknown_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "PackageEditor initial plan should copy unknown part");

    const std::string replacement_core =
        "<cp:coreProperties><dc:creator>Updated</dc:creator></cp:coreProperties>";
    editor.replace_part(core_part, replacement_core,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "test core properties rewrite");

    const auto* core_plan = editor.edit_plan().find_part(core_part);
    check(core_plan != nullptr, "PackageEditor edit plan should include replaced part");
    check(core_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "PackageEditor edit plan should mark replaced part local-DOM-rewrite");
    const auto* core_manifest_part = editor.manifest().find_part(core_part);
    check(core_manifest_part != nullptr,
        "PackageEditor manifest should keep replaced part visible");
    check(core_manifest_part->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "PackageEditor manifest should mirror replaced part write mode");
    check(core_manifest_part->dirty && !core_manifest_part->preserve_original
            && !core_manifest_part->generated,
        "PackageEditor manifest should mark local-DOM replacement dirty");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "PackageEditor edit plan should keep unknown part copy-original");
    const auto* unknown_manifest_part = editor.manifest().find_part(unknown_part);
    check(unknown_manifest_part != nullptr,
        "PackageEditor manifest should keep unknown part visible");
    check(unknown_manifest_part->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal
            && unknown_manifest_part->preserve_original && !unknown_manifest_part->dirty,
        "PackageEditor manifest should keep unknown part copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan replacement_output_plan =
        editor.planned_output();
    check_output_entry_materialized_replacement(replacement_output_plan.entries,
        core_part.zip_path(), true,
        "ordinary core properties replacement should expose materialized replacement output");
    check_output_entry_materialized_replacement_reason(replacement_output_plan.entries,
        core_part.zip_path(), "core properties package part",
        "ordinary core properties replacement should explain materialized replacement boundary");
    check_output_entry_staged_replacement_chunks(replacement_output_plan.entries,
        core_part.zip_path(), false,
        "ordinary core properties replacement should not expose staged chunks");
    check_output_entry_materialized_replacement(replacement_output_plan.entries,
        unknown_part.zip_path(), false,
        "copy-original unknown part should not expose materialized replacement output");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, core_part.zip_path());
    check(output_reader.read_entry("docProps/core.xml") == replacement_core,
        "PackageEditor should write replacement part bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "PackageEditor should preserve unknown part bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "PackageEditor should preserve untouched workbook bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "PackageEditor should preserve content types bytes when unchanged");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "PackageEditor should preserve package relationships bytes when unchanged");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels") == source.workbook_relationships,
        "PackageEditor should preserve workbook relationships bytes when unchanged");

    const auto* package_relationship =
        output_reader.package_relationships().find_by_id("rId2");
    check(package_relationship != nullptr,
        "preserved package relationships should remain readable");
    check(package_relationship->target == "docProps/core.xml",
        "preserved core properties relationship target mismatch");
}

void test_package_editor_staged_chunk_part_replacement_writes_chunks()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-staged-chunks-source.xlsx");
    const std::filesystem::path chunk_output =
        output_path("fastxlsx-package-editor-staged-chunks-output.xlsx");
    const std::filesystem::path final_output =
        output_path("fastxlsx-package-editor-staged-chunks-final-output.xlsx");
    const std::filesystem::path restore_output =
        output_path("fastxlsx-package-editor-staged-chunks-restore-output.xlsx");
    const std::filesystem::path body_path =
        output_path("fastxlsx-package-editor-staged-chunks-body.xml");
    const std::string opaque_prefix = "chunked:";
    const std::string opaque_body = "file-backed-body";
    const std::string opaque_suffix = ":done";
    const std::string expected_chunked_opaque =
        opaque_prefix + opaque_body + opaque_suffix;
    write_binary_file(body_path, opaque_body);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName opaque_part("/custom/opaque.bin");
    editor.replace_part_chunks(opaque_part,
        {
            fastxlsx::detail::PackageEntryChunk::memory(opaque_prefix),
            fastxlsx::detail::PackageEntryChunk::file(body_path),
            fastxlsx::detail::PackageEntryChunk::memory(opaque_suffix),
        },
        "staged opaque stream chunks");

    const auto* opaque_plan = editor.edit_plan().find_part(opaque_part);
    check(opaque_plan != nullptr,
        "staged chunk replacement should record opaque edit-plan entry");
    check(opaque_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "staged chunk replacement should be a stream rewrite");
    check(!editor.edit_plan().full_calculation_on_load(),
        "generic non-worksheet staged chunks should not request recalculation");
    check_manifest_write_mode(editor, opaque_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "staged chunk replacement should update manifest write mode");
    const fastxlsx::detail::PackageEditorOutputPlan chunk_plan = editor.planned_output();
    check_output_entry_plan(chunk_plan.entries, opaque_part.zip_path(),
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "staged chunk replacement should appear in planned output");
    check_output_entry_staged_replacement_chunks(chunk_plan.entries, opaque_part.zip_path(),
        true,
        "staged chunk replacement output plan should expose active chunked replacement");
    check_output_entry_materialized_replacement(chunk_plan.entries, opaque_part.zip_path(),
        false,
        "staged chunk replacement output plan should not expose materialized replacement");

    editor.save_as(chunk_output);

    const fastxlsx::detail::PackageReader chunk_output_reader =
        fastxlsx::detail::PackageReader::open(chunk_output);
    check_preserved_source_entries(editor.reader(), chunk_output_reader, opaque_part.zip_path());
    check_entry_bytes(
        chunk_output_reader, opaque_part.zip_path(), expected_chunked_opaque);
    check_entry_bytes(chunk_output_reader, "xl/worksheets/sheet1.xml", source.worksheet);

    const std::string final_opaque = "final opaque bytes";
    replace_part_with_memory_chunks(editor, opaque_part, final_opaque,
        "final opaque staged rewrite");

    const auto* final_opaque_plan = editor.edit_plan().find_part(opaque_part);
    check(final_opaque_plan != nullptr,
        "final string replacement should keep opaque edit-plan entry");
    check(final_opaque_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "final staged replacement should keep staged chunk write mode");
    const fastxlsx::detail::PackageEditorOutputPlan final_plan = editor.planned_output();
    check_output_entry_staged_replacement_chunks(final_plan.entries, opaque_part.zip_path(),
        true,
        "final staged replacement output plan should keep active chunked replacement marker");
    check_output_entry_materialized_replacement(final_plan.entries, opaque_part.zip_path(),
        false,
        "final staged replacement output plan should not expose active materialized replacement");

    editor.save_as(final_output);

    const fastxlsx::detail::PackageReader final_output_reader =
        fastxlsx::detail::PackageReader::open(final_output);
    check_preserved_source_entries(editor.reader(), final_output_reader, opaque_part.zip_path());
    check_entry_bytes(final_output_reader, opaque_part.zip_path(), final_opaque);

    fastxlsx::detail::PackageEditor restore_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    restore_editor.remove_part(opaque_part, "temporary opaque removal before staged chunks");
    restore_editor.replace_part_chunks(opaque_part,
        {
            fastxlsx::detail::PackageEntryChunk::memory(opaque_prefix),
            fastxlsx::detail::PackageEntryChunk::file(body_path),
            fastxlsx::detail::PackageEntryChunk::memory(opaque_suffix),
        },
        "restored opaque staged stream chunks");

    const auto* restored_opaque_plan = restore_editor.edit_plan().find_part(opaque_part);
    check(restored_opaque_plan != nullptr,
        "staged chunks after removal should restore the opaque edit-plan entry");
    check(restored_opaque_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "staged chunks after removal should keep stream rewrite mode");
    check(restore_editor.edit_plan().find_removed_part(opaque_part) == nullptr,
        "staged chunks after removal should clear stale removed-part audit");
    check(restore_editor.edit_plan().removed_package_entries().empty(),
        "staged chunks after removal should clear stale removed package entries");
    check_manifest_write_mode(restore_editor, opaque_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "staged chunks after removal should restore manifest write mode");
    const fastxlsx::detail::PackageEditorOutputPlan restore_plan =
        restore_editor.planned_output();
    check_output_entry_plan(restore_plan.entries,
        opaque_part.zip_path(), fastxlsx::detail::PartWriteMode::StreamRewrite,
        true, false, false, false,
        "staged chunks after removal should appear as active staged output");
    check_output_entry_staged_replacement_chunks(restore_plan.entries,
        opaque_part.zip_path(), true,
        "staged chunks after removal output plan should expose active chunked replacement");
    check_output_entry_materialized_replacement(restore_plan.entries,
        opaque_part.zip_path(), false,
        "staged chunks after removal output plan should not expose materialized replacement");

    restore_editor.save_as(restore_output);

    const fastxlsx::detail::PackageReader restore_output_reader =
        fastxlsx::detail::PackageReader::open(restore_output);
    check_preserved_source_entries(
        restore_editor.reader(), restore_output_reader, opaque_part.zip_path());
    check_entry_bytes(
        restore_output_reader, opaque_part.zip_path(), expected_chunked_opaque);
}

void test_package_editor_save_as_rejects_changed_staged_chunk_size_without_state_changes()
{
    struct ChunkMutationCase {
        std::string_view name;
        std::string_view mutated_body;
    };

    const std::array cases {
        ChunkMutationCase {"truncated", "file"},
        ChunkMutationCase {"extended", "file-backed-body-extended"},
    };

    for (const ChunkMutationCase& test_case : cases) {
        const SourcePackage source = write_source_package(
            "fastxlsx-package-editor-staged-size-" + std::string(test_case.name)
            + "-source.xlsx");
        const std::filesystem::path output =
            output_path("fastxlsx-package-editor-staged-size-"
                + std::string(test_case.name) + "-output.xlsx");
        const std::filesystem::path body_path =
            output_path("fastxlsx-package-editor-staged-size-"
                + std::string(test_case.name) + "-body.bin");
        const std::string output_sentinel =
            "do not overwrite staged size failure output";
        write_binary_file(output, output_sentinel);

        const std::string opaque_prefix = "chunked:";
        const std::string opaque_body = "file-backed-body";
        const std::string opaque_suffix = ":done";
        const std::string expected_chunked_opaque =
            opaque_prefix + opaque_body + opaque_suffix;
        write_binary_file(body_path, opaque_body);

        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);
        const fastxlsx::detail::PartName opaque_part("/custom/opaque.bin");
        editor.replace_part_chunks(opaque_part,
            {
                fastxlsx::detail::PackageEntryChunk::memory(opaque_prefix),
                fastxlsx::detail::PackageEntryChunk::file(body_path),
                fastxlsx::detail::PackageEntryChunk::memory(opaque_suffix),
            },
            "staged opaque stream chunks with expected size");

        const std::size_t initial_plan_size = editor.edit_plan().size();
        const std::size_t initial_note_count = editor.edit_plan().notes().size();
        const bool initial_full_calculation =
            editor.edit_plan().full_calculation_on_load();
        const std::vector<std::filesystem::path> temp_files_before =
            package_editor_temp_files();
        const std::vector<std::filesystem::path> output_temp_files_before =
            package_editor_output_sibling_temp_files(output);

        write_binary_file(body_path, test_case.mutated_body);

        bool save_failed = false;
        try {
            editor.save_as(output);
        } catch (const std::exception& error) {
            save_failed = true;
            check_contains(error.what(),
                "ZIP entry chunk size changed after staging",
                "save_as staged chunk size mutation should report the size contract");
            check_contains(error.what(),
                std::string("expected ") + std::to_string(opaque_body.size()) + " bytes",
                "save_as staged chunk size mutation should report expected bytes");
            check_contains(error.what(),
                std::string("actual ") + std::to_string(test_case.mutated_body.size()) + " bytes",
                "save_as staged chunk size mutation should report actual bytes");
            check_contains(error.what(), "ZIP entry 'custom/opaque.bin' chunk 1",
                "save_as staged chunk size mutation should report entry/chunk context");
            check_contains(error.what(), body_path.filename().generic_string(),
                "save_as staged chunk size mutation should include the file-backed chunk path");
        }

        check(save_failed,
            "save_as should reject staged file chunks whose size changed after validation");
        check(fastxlsx::test::read_file(output) == output_sentinel,
            "save_as staged chunk size failure should preserve existing output bytes");
        check(editor.edit_plan().size() == initial_plan_size,
            "save_as staged chunk size failure should preserve edit-plan size");
        check(editor.edit_plan().notes().size() == initial_note_count,
            "save_as staged chunk size failure should not append notes");
        check(editor.edit_plan().full_calculation_on_load() == initial_full_calculation,
            "save_as staged chunk size failure should preserve calc policy");
        check_manifest_write_mode(editor, opaque_part,
            fastxlsx::detail::PartWriteMode::StreamRewrite,
            "save_as staged chunk size failure should keep prior staged part plan");
        check_no_new_package_editor_temp_files(temp_files_before,
            "save_as staged chunk size failure should clean source-copy temp files");
        check_no_new_package_editor_output_sibling_temp_files(output_temp_files_before, output,
            "save_as staged chunk size failure should clean output sibling temp files");

        write_binary_file(body_path, opaque_body);
        editor.save_as(output);

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(output);
        check_entry_bytes(output_reader, opaque_part.zip_path(), expected_chunked_opaque);
        check_entry_bytes(output_reader, "xl/worksheets/sheet1.xml", source.worksheet);
    }
}

void test_package_editor_save_as_rejects_changed_staged_chunk_crc_without_state_changes()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-staged-crc-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-staged-crc-output.xlsx");
    const std::filesystem::path body_path =
        output_path("fastxlsx-package-editor-staged-crc-body.bin");
    const std::string output_sentinel =
        "do not overwrite staged crc failure output";
    write_binary_file(output, output_sentinel);

    const std::string opaque_prefix = "chunked:";
    const std::string opaque_body = "file-backed-body";
    const std::string opaque_suffix = ":done";
    const std::string expected_chunked_opaque =
        opaque_prefix + opaque_body + opaque_suffix;
    write_binary_file(body_path, opaque_body);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName opaque_part("/custom/opaque.bin");
    editor.replace_part_chunks(opaque_part,
        {
            fastxlsx::detail::PackageEntryChunk::memory(opaque_prefix),
            fastxlsx::detail::PackageEntryChunk::file(body_path),
            fastxlsx::detail::PackageEntryChunk::memory(opaque_suffix),
        },
        "staged opaque stream chunks with expected CRC");

    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const bool initial_full_calculation =
        editor.edit_plan().full_calculation_on_load();
    const std::vector<std::filesystem::path> temp_files_before =
        package_editor_temp_files();
    const std::vector<std::filesystem::path> output_temp_files_before =
        package_editor_output_sibling_temp_files(output);

    write_binary_file(body_path, same_size_different_payload(opaque_body));

    bool save_failed = false;
    try {
        editor.save_as(output);
    } catch (const std::exception& error) {
        save_failed = true;
        check_contains(error.what(),
            "ZIP entry chunk CRC32 changed after staging",
            "save_as staged chunk CRC mutation should report the CRC contract");
        check_contains(error.what(), "expected ",
            "save_as staged chunk CRC mutation should report expected CRC");
        check_contains(error.what(), "actual ",
            "save_as staged chunk CRC mutation should report actual CRC");
        check_contains(error.what(), "ZIP entry 'custom/opaque.bin' chunk 1",
            "save_as staged chunk CRC mutation should report entry/chunk context");
        check_contains(error.what(), body_path.filename().generic_string(),
            "save_as staged chunk CRC mutation should include the file-backed chunk path");
    }

    check(save_failed,
        "save_as should reject staged file chunks whose CRC changed after validation");
    check(fastxlsx::test::read_file(output) == output_sentinel,
        "save_as staged chunk CRC failure should preserve existing output bytes");
    check(editor.edit_plan().size() == initial_plan_size,
        "save_as staged chunk CRC failure should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "save_as staged chunk CRC failure should not append notes");
    check(editor.edit_plan().full_calculation_on_load() == initial_full_calculation,
        "save_as staged chunk CRC failure should preserve calc policy");
    check_manifest_write_mode(editor, opaque_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "save_as staged chunk CRC failure should keep prior staged part plan");
    check_no_new_package_editor_temp_files(temp_files_before,
        "save_as staged chunk CRC failure should clean source-copy temp files");
    check_no_new_package_editor_output_sibling_temp_files(output_temp_files_before, output,
        "save_as staged chunk CRC failure should clean output sibling temp files");

    write_binary_file(body_path, opaque_body);
    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_entry_bytes(output_reader, opaque_part.zip_path(), expected_chunked_opaque);
    check_entry_bytes(output_reader, "xl/worksheets/sheet1.xml", source.worksheet);
}

void test_package_editor_rejects_invalid_generic_staged_chunks_without_state_changes()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-invalid-generic-staged-chunks-source.xlsx");
    const fastxlsx::detail::PartName opaque_part("/custom/opaque.bin");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    const auto expect_invalid_chunk =
        [&](fastxlsx::detail::PackageEntryChunk chunk,
            std::string_view expected_error_fragment,
            std::string_view output_name,
            const char* scenario) {
            fastxlsx::detail::PackageEditor editor =
                fastxlsx::detail::PackageEditor::open(source.path);
            const std::size_t initial_plan_size = editor.edit_plan().size();
            const std::size_t initial_note_count = editor.edit_plan().notes().size();
            const std::size_t initial_package_entry_count =
                editor.edit_plan().package_entries().size();
            const std::size_t initial_removed_package_entry_count =
                editor.edit_plan().removed_package_entries().size();

            bool failed = false;
            try {
                editor.replace_part_chunks(opaque_part, {std::move(chunk)},
                    std::string(scenario));
            } catch (const std::exception& error) {
                failed = true;
                check_contains(error.what(),
                    "generic package part staged replacement has invalid staged chunks",
                    "invalid generic staged chunk should fail before commit");
                check_contains(error.what(), "staged package-entry chunk 0",
                    "invalid generic staged chunk should identify the failing chunk");
                check_contains(error.what(), expected_error_fragment,
                    "invalid generic staged chunk should preserve chunk validation detail");
            }
            check(failed, "PackageEditor should reject invalid generic staged chunks");
            check(editor.edit_plan().size() == initial_plan_size,
                "invalid generic staged chunk failure should not mutate edit-plan parts");
            check(editor.edit_plan().notes().size() == initial_note_count,
                "invalid generic staged chunk failure should not add notes");
            check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
                "invalid generic staged chunk failure should not add package-entry audits");
            check(editor.edit_plan().removed_package_entries().size()
                    == initial_removed_package_entry_count,
                "invalid generic staged chunk failure should not add removed package-entry audits");
            check(editor.edit_plan().removed_parts().empty(),
                "invalid generic staged chunk failure should not record removed parts");
            check(!editor.edit_plan().full_calculation_on_load(),
                "invalid generic staged chunk failure should not request recalculation");
            check(editor.edit_plan().calc_chain_action()
                    == fastxlsx::detail::CalcChainAction::Preserve,
                "invalid generic staged chunk failure should not change calcChain policy");

            check_manifest_write_mode(editor, opaque_part,
                fastxlsx::detail::PartWriteMode::CopyOriginal,
                "invalid generic staged chunk failure should keep opaque copy-original");
            check_manifest_write_mode(editor, workbook_part,
                fastxlsx::detail::PartWriteMode::CopyOriginal,
                "invalid generic staged chunk failure should keep workbook copy-original");
            check_manifest_write_mode(editor, worksheet_part,
                fastxlsx::detail::PartWriteMode::CopyOriginal,
                "invalid generic staged chunk failure should keep worksheet copy-original");

            const fastxlsx::detail::PackageEditorOutputPlan failed_plan =
                editor.planned_output();
            check_output_entry_plan(failed_plan.entries, opaque_part.zip_path(),
                fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
                "invalid generic staged chunk failure should preserve opaque source entry");
            check_output_entry_staged_replacement_chunks(failed_plan.entries,
                opaque_part.zip_path(), false,
                "invalid generic staged chunk failure should not stage opaque chunks");
            check_output_entry_materialized_replacement(failed_plan.entries,
                opaque_part.zip_path(), false,
                "invalid generic staged chunk failure should not mark materialized replacement");

            const std::filesystem::path output = output_path(std::string(output_name));
            editor.save_as(output);
            const fastxlsx::detail::PackageReader output_reader =
                fastxlsx::detail::PackageReader::open(output);
            check_entry_bytes(output_reader, opaque_part.zip_path(), source.unknown);
            check_entry_bytes(output_reader, worksheet_part.zip_path(), source.worksheet);
        };

    fastxlsx::detail::PackageEntryChunk mixed_chunk =
        fastxlsx::detail::PackageEntryChunk::memory("invalid mixed payload");
    mixed_chunk.path =
        output_path("fastxlsx-package-editor-invalid-generic-staged-chunks-mixed.bin");
    expect_invalid_chunk(std::move(mixed_chunk), "cannot mix memory and file sources",
        "fastxlsx-package-editor-invalid-generic-staged-chunks-mixed-output.xlsx",
        "mixed memory/file generic staged chunk");

    fastxlsx::detail::PackageEntryChunk missing_file_chunk =
        fastxlsx::detail::PackageEntryChunk::file(
            output_path("fastxlsx-package-editor-invalid-generic-staged-chunks-missing.bin"));
    expect_invalid_chunk(std::move(missing_file_chunk),
        "failed to measure staged package-entry chunk file",
        "fastxlsx-package-editor-invalid-generic-staged-chunks-missing-output.xlsx",
        "missing file generic staged chunk");
}

void test_package_editor_rejects_invalid_worksheet_staged_chunks_without_state_changes()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-invalid-worksheet-staged-chunks-source.xlsx");
    const fastxlsx::detail::PartName opaque_part("/custom/opaque.bin");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    const auto expect_invalid_chunk =
        [&](fastxlsx::detail::PackageEntryChunk chunk,
            std::string_view expected_error_fragment,
            std::string_view output_name,
            const char* scenario) {
            fastxlsx::detail::PackageEditor editor =
                fastxlsx::detail::PackageEditor::open(source.path);
            const std::size_t initial_plan_size = editor.edit_plan().size();
            const std::size_t initial_note_count = editor.edit_plan().notes().size();
            const std::size_t initial_package_entry_count =
                editor.edit_plan().package_entries().size();
            const std::size_t initial_removed_package_entry_count =
                editor.edit_plan().removed_package_entries().size();
            const std::size_t initial_relationship_target_audit_count =
                editor.edit_plan().relationship_target_audits().size();
            const std::size_t initial_worksheet_relationship_audit_count =
                editor.edit_plan().worksheet_relationship_reference_audits().size();
            const std::size_t initial_worksheet_payload_audit_count =
                editor.edit_plan().worksheet_payload_dependency_audits().size();

            bool failed = false;
            try {
                editor.replace_worksheet_part_chunks(worksheet_part, {std::move(chunk)},
                    {}, std::string(scenario));
            } catch (const std::exception& error) {
                failed = true;
                check_contains(error.what(),
                    "worksheet staged replacement has invalid staged chunks",
                    "invalid worksheet staged chunk should fail before worksheet scanning");
                check_contains(error.what(), "staged package-entry chunk 0",
                    "invalid worksheet staged chunk should identify the failing chunk");
                check_contains(error.what(), expected_error_fragment,
                    "invalid worksheet staged chunk should preserve chunk validation detail");
            }
            check(failed, "PackageEditor should reject invalid worksheet staged chunks");
            check(editor.edit_plan().size() == initial_plan_size,
                "invalid worksheet staged chunk failure should not mutate edit-plan parts");
            check(editor.edit_plan().notes().size() == initial_note_count,
                "invalid worksheet staged chunk failure should not add notes");
            check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
                "invalid worksheet staged chunk failure should not add package-entry audits");
            check(editor.edit_plan().removed_package_entries().size()
                    == initial_removed_package_entry_count,
                "invalid worksheet staged chunk failure should not add removed package-entry audits");
            check(editor.edit_plan().relationship_target_audits().size()
                    == initial_relationship_target_audit_count,
                "invalid worksheet staged chunk failure should not add relationship target audits");
            check(editor.edit_plan().worksheet_relationship_reference_audits().size()
                    == initial_worksheet_relationship_audit_count,
                "invalid worksheet staged chunk failure should not add worksheet relationship audits");
            check(editor.edit_plan().worksheet_payload_dependency_audits().size()
                    == initial_worksheet_payload_audit_count,
                "invalid worksheet staged chunk failure should not add worksheet payload audits");
            check(editor.edit_plan().removed_parts().empty(),
                "invalid worksheet staged chunk failure should not record removed parts");
            check(!editor.edit_plan().full_calculation_on_load(),
                "invalid worksheet staged chunk failure should not request recalculation");
            check(editor.edit_plan().calc_chain_action()
                    == fastxlsx::detail::CalcChainAction::Preserve,
                "invalid worksheet staged chunk failure should not change calcChain policy");

            check_manifest_write_mode(editor, opaque_part,
                fastxlsx::detail::PartWriteMode::CopyOriginal,
                "invalid worksheet staged chunk failure should keep opaque copy-original");
            check_manifest_write_mode(editor, workbook_part,
                fastxlsx::detail::PartWriteMode::CopyOriginal,
                "invalid worksheet staged chunk failure should keep workbook copy-original");
            check_manifest_write_mode(editor, worksheet_part,
                fastxlsx::detail::PartWriteMode::CopyOriginal,
                "invalid worksheet staged chunk failure should keep worksheet copy-original");

            const fastxlsx::detail::PackageEditorOutputPlan failed_plan =
                editor.planned_output();
            check_output_entry_plan(failed_plan.entries, worksheet_part.zip_path(),
                fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
                "invalid worksheet staged chunk failure should preserve worksheet source entry");
            check_output_entry_plan(failed_plan.entries, workbook_part.zip_path(),
                fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
                "invalid worksheet staged chunk failure should preserve workbook source entry");
            check_output_entry_staged_replacement_chunks(failed_plan.entries,
                worksheet_part.zip_path(), false,
                "invalid worksheet staged chunk failure should not stage worksheet chunks");
            check_output_entry_materialized_replacement(failed_plan.entries,
                worksheet_part.zip_path(), false,
                "invalid worksheet staged chunk failure should not mark materialized worksheet replacement");
            check(failed_plan.worksheet_relationship_reference_audits.empty(),
                "invalid worksheet staged chunk output plan should not add worksheet relationship audits");
            check(failed_plan.worksheet_payload_dependency_audits.empty(),
                "invalid worksheet staged chunk output plan should not add worksheet payload audits");

            const std::filesystem::path output = output_path(std::string(output_name));
            editor.save_as(output);
            const fastxlsx::detail::PackageReader output_reader =
                fastxlsx::detail::PackageReader::open(output);
            check_entry_bytes(output_reader, opaque_part.zip_path(), source.unknown);
            check_entry_bytes(output_reader, workbook_part.zip_path(), source.workbook);
            check_entry_bytes(output_reader, worksheet_part.zip_path(), source.worksheet);
        };

    fastxlsx::detail::PackageEntryChunk mixed_chunk =
        fastxlsx::detail::PackageEntryChunk::memory("<worksheet/>");
    mixed_chunk.path =
        output_path("fastxlsx-package-editor-invalid-worksheet-staged-chunks-mixed.bin");
    expect_invalid_chunk(std::move(mixed_chunk), "cannot mix memory and file sources",
        "fastxlsx-package-editor-invalid-worksheet-staged-chunks-mixed-output.xlsx",
        "mixed memory/file worksheet staged chunk");

    const std::filesystem::path missing_file_path =
        output_path("fastxlsx-package-editor-invalid-worksheet-staged-chunks-missing.xml");
    std::error_code ignored;
    std::filesystem::remove(missing_file_path, ignored);
    fastxlsx::detail::PackageEntryChunk missing_file_chunk =
        fastxlsx::detail::PackageEntryChunk::file(missing_file_path);
    expect_invalid_chunk(std::move(missing_file_chunk),
        "failed to measure staged package-entry chunk file",
        "fastxlsx-package-editor-invalid-worksheet-staged-chunks-missing-output.xlsx",
        "missing file worksheet staged chunk");
}

void test_package_editor_rejects_materialized_stream_rewrite_part_without_state_changes()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-materialized-stream-rewrite-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-materialized-stream-rewrite-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName opaque_part("/custom/opaque.bin");

    const std::size_t initial_plan_size = editor.edit_plan().size();
    bool failed = false;
    try {
        editor.replace_part(opaque_part, "materialized bytes",
            fastxlsx::detail::PartWriteMode::StreamRewrite,
            "materialized stream rewrite should fail");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(),
            "materialized replace_part cannot use stream-rewrite write mode",
            "materialized stream-rewrite replacement should report the staged-only boundary");
        check_contains(error.what(), "replace_part_chunks",
            "materialized stream-rewrite replacement should point to staged chunks");
    }
    check(failed,
        "PackageEditor should reject materialized replace_part StreamRewrite");
    check(editor.edit_plan().size() == initial_plan_size,
        "materialized StreamRewrite failure should not mutate the edit plan");
    check_manifest_write_mode(editor, opaque_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "materialized StreamRewrite failure should leave target copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan failed_plan = editor.planned_output();
    check_output_entry_plan(failed_plan.entries, opaque_part.zip_path(),
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "materialized StreamRewrite failure should keep output copy-original");
    check_output_entry_staged_replacement_chunks(failed_plan.entries, opaque_part.zip_path(),
        false,
        "materialized StreamRewrite failure should not queue staged chunks");
    check_output_entry_materialized_replacement(failed_plan.entries, opaque_part.zip_path(),
        false,
        "materialized StreamRewrite failure should not queue materialized replacement bytes");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader);
    check_entry_bytes(output_reader, opaque_part.zip_path(), source.unknown);
}

void test_package_editor_generic_staged_chunks_route_worksheet_targets()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-generic-worksheet-staged-chunks-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-generic-worksheet-staged-chunks-output.xlsx");
    const std::filesystem::path body_path =
        output_path("fastxlsx-package-editor-generic-worksheet-staged-chunks-body.xml");
    const std::string worksheet_prefix =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheetData>)";
    const std::string worksheet_body =
        R"(<row r="2"><c r="A2"><f>A1+1</f></c></row>)";
    const std::string worksheet_suffix =
        R"(</sheetData><drawing r:id="rIdMissing"/></worksheet>)";
    const std::string replacement_worksheet =
        worksheet_prefix + worksheet_body + worksheet_suffix;
    write_binary_file(body_path, worksheet_body);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    editor.replace_part_chunks(worksheet_part,
        {
            fastxlsx::detail::PackageEntryChunk::memory(worksheet_prefix),
            fastxlsx::detail::PackageEntryChunk::file(body_path),
            fastxlsx::detail::PackageEntryChunk::memory(worksheet_suffix),
        },
        "generic caller supplied worksheet chunks");

    check(editor.edit_plan().full_calculation_on_load(),
        "generic staged chunks targeting a worksheet should use worksheet calc policy");
    check(has_note_containing(editor.edit_plan().notes(),
              {"generic staged package part chunk replacement targeting a worksheet part",
                  "worksheet-aware validation"}),
        "generic worksheet chunks should report worksheet-aware routing");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet staged chunk replacement validates worksheet root/events",
                  "one chunk-source audit reader"}),
        "generic worksheet chunks should use combined chunk-source worksheet audit");
    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "generic worksheet chunks should record worksheet edit-plan entry");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "generic worksheet chunks should keep worksheet stream rewrite mode");
    check(worksheet_plan->reason.find("staged stream rewrite chunks") != std::string::npos,
        "generic worksheet chunks should expose worksheet staged rewrite reason");

    const auto output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, worksheet_part.zip_path(),
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "generic worksheet chunks should appear as worksheet stream rewrite");
    check_output_entry_plan(output_plan.entries, workbook_part.zip_path(),
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "generic worksheet chunks should still rewrite workbook calc metadata");
    check(has_note_containing(output_plan.notes,
              {"contains formulas", "calcChain policy"}),
        "generic worksheet chunks should audit formulas from staged chunks");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_entry_bytes(output_reader, worksheet_part.zip_path(), replacement_worksheet);
    check_contains(output_reader.read_entry(workbook_part.zip_path()), "fullCalcOnLoad=\"1\"",
        "generic worksheet chunks should request full workbook recalculation");
    check_entry_bytes(output_reader, "custom/opaque.bin", source.unknown);

    fastxlsx::detail::PackageEditor invalid_chunks_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    bool invalid_chunks_failed = false;
    try {
        invalid_chunks_editor.replace_part_chunks(
            worksheet_part,
            {
                fastxlsx::detail::PackageEntryChunk::memory("<!DOCTYPE worksheet><worksheet/>"),
            },
            "invalid generic worksheet chunks");
    } catch (const std::exception&) {
        invalid_chunks_failed = true;
    }
    check(invalid_chunks_failed, "invalid generic staged worksheet chunks should fail");
    check(!invalid_chunks_editor.edit_plan().full_calculation_on_load(),
        "invalid generic staged worksheet chunks should not request recalculation");
    check(invalid_chunks_editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "invalid generic staged worksheet chunks should not change worksheet edit-plan state");
    check_manifest_write_mode(invalid_chunks_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "invalid generic staged worksheet chunks should not change manifest state");
}

void test_package_editor_replaces_worksheet_with_staged_chunks()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-worksheet-staged-chunks-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-worksheet-staged-chunks-output.xlsx");
    const std::filesystem::path body_path =
        output_path("fastxlsx-package-editor-worksheet-staged-chunks-body.xml");
    const std::string worksheet_prefix =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheetData>)";
    const std::string worksheet_body =
        R"(<row r="2"><c r="A2"><f>A1+1</f></c></row>)";
    const std::string worksheet_suffix =
        R"(</sheetData><drawing r:id="rIdMissing"/></worksheet>)";
    const std::string replacement_worksheet =
        worksheet_prefix + worksheet_body + worksheet_suffix;
    write_binary_file(body_path, worksheet_body);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    editor.replace_worksheet_part_chunks(worksheet_part,
        {
            fastxlsx::detail::PackageEntryChunk::memory(worksheet_prefix),
            fastxlsx::detail::PackageEntryChunk::file(body_path),
            fastxlsx::detail::PackageEntryChunk::memory(worksheet_suffix),
        });

    check(editor.edit_plan().full_calculation_on_load(),
        "staged worksheet chunks should keep worksheet replacement calc policy");
    check(has_note_containing(editor.edit_plan().notes(),
              {"staged chunk replacement", "one chunk-source audit reader"}),
        "staged worksheet chunks should report combined chunk-source validation/audit");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"materialized worksheet XML"}),
        "staged worksheet chunks should not retain the legacy materialized-audit note");
    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "staged worksheet chunks should record worksheet edit-plan entry");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "staged worksheet chunks should keep worksheet stream rewrite mode");
    check(worksheet_plan->reason.find("staged stream rewrite chunks") != std::string::npos,
        "staged worksheet chunks should expose staged rewrite reason");
    const auto output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, worksheet_part.zip_path(),
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "staged worksheet chunks should appear as worksheet stream rewrite");
    check_output_entry_plan(output_plan.entries, workbook_part.zip_path(),
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "staged worksheet chunks should still rewrite workbook calc metadata");
    check(has_note_containing(output_plan.notes,
              {"worksheet staged chunk replacement validates worksheet root/events",
                  "one chunk-source audit reader"}),
        "staged worksheet chunks should expose combined validation/audit chunk-source handoff");
    check(has_note_containing(output_plan.notes,
              {"contains formulas", "calcChain policy"}),
        "staged worksheet chunks should audit formulas from the staged chunks");
    check(has_note_containing(output_plan.notes,
              {"worksheet drawing relationship metadata", "linked parts require caller review"}),
        "staged worksheet chunks should audit relationship-bearing metadata from the combined audit reader");
    check(has_note_containing(output_plan.notes,
              {"relationship id rIdMissing", "<drawing>", "repair worksheet .rels"}),
        "staged worksheet chunks should audit relationship ids from the combined audit reader");
    check(!output_plan.worksheet_payload_dependency_audits.empty(),
        "staged worksheet chunks should keep structured payload dependency audits");
    check(!output_plan.worksheet_relationship_reference_audits.empty(),
        "staged worksheet chunks should keep structured relationship-id audits");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_entry_bytes(output_reader, worksheet_part.zip_path(), replacement_worksheet);
    check_contains(output_reader.read_entry(workbook_part.zip_path()), "fullCalcOnLoad=\"1\"",
        "staged worksheet chunks should request full workbook recalculation");
    check_entry_bytes(output_reader, "custom/opaque.bin", source.unknown);
    check_entry_bytes(output_reader, "[Content_Types].xml", source.content_types);
    check_entry_bytes(output_reader, "_rels/.rels", source.package_relationships);
    check_entry_bytes(output_reader, "xl/_rels/workbook.xml.rels", source.workbook_relationships);

    fastxlsx::detail::PackageEditor empty_chunks_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    bool empty_chunks_failed = false;
    try {
        empty_chunks_editor.replace_worksheet_part_chunks(worksheet_part, {});
    } catch (const std::exception&) {
        empty_chunks_failed = true;
    }
    check(empty_chunks_failed, "empty staged worksheet chunks should fail");
    check(!empty_chunks_editor.edit_plan().full_calculation_on_load(),
        "empty staged worksheet chunks should not request recalculation");
    check(empty_chunks_editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "empty staged worksheet chunks should not change worksheet edit-plan state");
    check_manifest_write_mode(empty_chunks_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "empty staged worksheet chunks should not change manifest state");

    fastxlsx::detail::PackageEditor invalid_chunks_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    bool invalid_chunks_failed = false;
    try {
        invalid_chunks_editor.replace_worksheet_part_chunks(
            worksheet_part,
            {
                fastxlsx::detail::PackageEntryChunk::memory("<!DOCTYPE worksheet><worksheet/>"),
            });
    } catch (const std::exception&) {
        invalid_chunks_failed = true;
    }
    check(invalid_chunks_failed, "invalid staged worksheet chunks should fail");
    check(!invalid_chunks_editor.edit_plan().full_calculation_on_load(),
        "invalid staged worksheet chunks should not request recalculation");
    check(invalid_chunks_editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "invalid staged worksheet chunks should not change worksheet edit-plan state");
    check_manifest_write_mode(invalid_chunks_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "invalid staged worksheet chunks should not change manifest state");

    fastxlsx::detail::PackageEditor invalid_event_chunks_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const std::size_t invalid_event_initial_plan_size =
        invalid_event_chunks_editor.edit_plan().size();
    const std::size_t invalid_event_initial_note_count =
        invalid_event_chunks_editor.edit_plan().notes().size();
    bool invalid_event_chunks_failed = false;
    try {
        invalid_event_chunks_editor.replace_worksheet_part_chunks(
            worksheet_part,
            {
                fastxlsx::detail::PackageEntryChunk::memory(
                    R"(<worksheet><row r="1"/></worksheet>)"),
            });
    } catch (const std::exception& error) {
        invalid_event_chunks_failed = true;
        check_contains(error.what(), "row outside sheetData",
            "event-validated staged worksheet chunks should reject row outside sheetData");
    }
    check(invalid_event_chunks_failed,
        "staged worksheet chunks should fail when event validation rejects the worksheet");
    check(invalid_event_chunks_editor.edit_plan().size() == invalid_event_initial_plan_size,
        "event-invalid staged worksheet chunks should not change edit-plan parts");
    check(invalid_event_chunks_editor.edit_plan().notes().size() == invalid_event_initial_note_count,
        "event-invalid staged worksheet chunks should not add notes");
    check(!invalid_event_chunks_editor.edit_plan().full_calculation_on_load(),
        "event-invalid staged worksheet chunks should not request recalculation");
    check(invalid_event_chunks_editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "event-invalid staged worksheet chunks should not change worksheet edit-plan state");
    check_manifest_write_mode(invalid_event_chunks_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "event-invalid staged worksheet chunks should not change manifest state");
}

void test_package_editor_replaces_worksheet_by_name_with_staged_chunks()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-worksheet-by-name-staged-chunks-source.xlsx");
    SourcePackage source_with_namespaced_catalog = source;
    source_with_namespaced_catalog.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    fastxlsx::detail::write_package(source_with_namespaced_catalog.path,
        {
            {"[Content_Types].xml", source_with_namespaced_catalog.content_types},
            {"_rels/.rels", source_with_namespaced_catalog.package_relationships},
            {"xl/workbook.xml", source_with_namespaced_catalog.workbook},
            {"xl/_rels/workbook.xml.rels", source_with_namespaced_catalog.workbook_relationships},
            {"docProps/core.xml", source_with_namespaced_catalog.core_properties},
            {"xl/worksheets/sheet1.xml", source_with_namespaced_catalog.worksheet},
            {"custom/opaque.bin", source_with_namespaced_catalog.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-worksheet-by-name-staged-chunks-output.xlsx");
    const std::filesystem::path body_path =
        output_path("fastxlsx-package-editor-worksheet-by-name-staged-chunks-body.xml");
    const std::string worksheet_prefix = "<worksheet><sheetData>";
    const std::string worksheet_body =
        R"(<row r="4"><c r="B4"><f>A1+3</f></c></row>)";
    const std::string worksheet_suffix = "</sheetData></worksheet>";
    const std::string replacement_worksheet =
        worksheet_prefix + worksheet_body + worksheet_suffix;
    write_binary_file(body_path, worksheet_body);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source_with_namespaced_catalog.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    editor.replace_worksheet_part_chunks_by_name("Sheet1",
        {
            fastxlsx::detail::PackageEntryChunk::memory(worksheet_prefix),
            fastxlsx::detail::PackageEntryChunk::file(body_path),
            fastxlsx::detail::PackageEntryChunk::memory(worksheet_suffix),
        });

    check(editor.edit_plan().full_calculation_on_load(),
        "by-name staged worksheet chunks should use worksheet replacement calc policy");
    check(has_note_containing(editor.edit_plan().notes(),
              {"by-name worksheet staged chunk replacement", "provided chunks"}),
        "by-name staged worksheet chunks should report by-name staged handoff");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet staged chunk replacement validates worksheet root/events",
                  "one chunk-source audit reader"}),
        "by-name staged worksheet chunks should reuse combined chunk-source validation/audit");
    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "by-name staged worksheet chunks should resolve worksheet edit-plan entry");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "by-name staged worksheet chunks should keep worksheet stream rewrite mode");
    check(worksheet_plan->reason.find("staged stream rewrite chunks") != std::string::npos,
        "by-name staged worksheet chunks should expose staged rewrite reason");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, worksheet_part.zip_path(),
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "by-name staged worksheet chunks output plan should stream-rewrite worksheet");
    check_output_entry_plan(output_plan.entries, workbook_part.zip_path(),
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "by-name staged worksheet chunks output plan should rewrite workbook calc metadata");
    check(has_note_containing(output_plan.notes,
              {"contains formulas", "calcChain policy"}),
        "by-name staged worksheet chunks should audit formulas from staged chunks");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("Sheet1") == worksheet_part,
        "by-name staged worksheet chunks output should keep sheet catalog readable");
    check_entry_bytes(output_reader, worksheet_part.zip_path(), replacement_worksheet);
    check_contains(output_reader.read_entry(workbook_part.zip_path()), "fullCalcOnLoad=\"1\"",
        "by-name staged worksheet chunks should request full workbook recalculation");
    check_entry_bytes(output_reader, "custom/opaque.bin", source_with_namespaced_catalog.unknown);

    fastxlsx::detail::PackageEditor missing_name_editor =
        fastxlsx::detail::PackageEditor::open(source_with_namespaced_catalog.path);
    bool missing_name_failed = false;
    try {
        missing_name_editor.replace_worksheet_part_chunks_by_name("Missing",
            {
                fastxlsx::detail::PackageEntryChunk::memory(worksheet_prefix),
                fastxlsx::detail::PackageEntryChunk::file(body_path),
                fastxlsx::detail::PackageEntryChunk::memory(worksheet_suffix),
            });
    } catch (const std::exception&) {
        missing_name_failed = true;
    }
    check(missing_name_failed,
        "missing sheet name should fail before by-name staged worksheet chunk state changes");
    check(!missing_name_editor.edit_plan().full_calculation_on_load(),
        "missing by-name staged worksheet chunks should not request recalculation");
    check_manifest_write_mode(missing_name_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "missing by-name staged worksheet chunks should not change worksheet manifest state");
}

void test_package_editor_replaces_worksheet_by_planned_name_with_staged_chunks()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-worksheet-planned-name-staged-chunks-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-worksheet-planned-name-staged-chunks-output.xlsx");
    const std::filesystem::path body_path =
        output_path("fastxlsx-package-editor-worksheet-planned-name-staged-chunks-body.xml");
    const std::string worksheet_prefix = "<worksheet><sheetData>";
    const std::string worksheet_body =
        R"(<row r="6"><c r="C6"><v>66</v></c></row>)";
    const std::string worksheet_suffix = "</sheetData></worksheet>";
    const std::string replacement_worksheet =
        worksheet_prefix + worksheet_body + worksheet_suffix;
    write_binary_file(body_path, worksheet_body);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    const std::string replacement_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="ChunkRenamed" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, replacement_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "ordinary workbook replacement before by-name staged chunks");

    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    const std::size_t queued_package_entry_count =
        editor.edit_plan().package_entries().size();
    bool old_name_failed = false;
    try {
        editor.replace_worksheet_part_chunks_by_name("Sheet1",
            {
                fastxlsx::detail::PackageEntryChunk::memory(worksheet_prefix),
                fastxlsx::detail::PackageEntryChunk::file(body_path),
                fastxlsx::detail::PackageEntryChunk::memory(worksheet_suffix),
            });
    } catch (const std::exception& error) {
        old_name_failed = true;
        check_contains(error.what(), "workbook sheet name is not present",
            "planned by-name staged chunks should reject old source sheet name");
    }
    check(old_name_failed,
        "planned by-name staged chunks should use planned workbook sheet names");
    check(editor.edit_plan().size() == queued_plan_size,
        "planned old-name staged chunk failure should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "planned old-name staged chunk failure should not append notes");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "planned old-name staged chunk failure should preserve package-entry audit count");
    check(!editor.edit_plan().full_calculation_on_load(),
        "planned old-name staged chunk failure should not request recalculation");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "planned old-name staged chunk failure should keep workbook rewrite");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "planned old-name staged chunk failure should leave worksheet copy-original");

    editor.replace_worksheet_part_chunks_by_name("ChunkRenamed",
        {
            fastxlsx::detail::PackageEntryChunk::memory(worksheet_prefix),
            fastxlsx::detail::PackageEntryChunk::file(body_path),
            fastxlsx::detail::PackageEntryChunk::memory(worksheet_suffix),
        });

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "planned by-name staged chunks should resolve renamed worksheet part");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "planned by-name staged chunks should stream-rewrite worksheet");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr
            && workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "planned by-name staged chunks should keep workbook local-DOM rewrite");
    check(editor.edit_plan().full_calculation_on_load(),
        "planned by-name staged chunks should request workbook recalculation");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, workbook_part.zip_path(),
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "planned by-name staged chunks output plan should expose workbook rewrite");
    check_output_entry_plan(output_plan.entries, worksheet_part.zip_path(),
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "planned by-name staged chunks output plan should expose worksheet stream rewrite");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("ChunkRenamed") == worksheet_part,
        "planned by-name staged chunks output should expose renamed sheet catalog");
    check_entry_bytes(output_reader, worksheet_part.zip_path(), replacement_worksheet);
    const std::string output_workbook = output_reader.read_entry(workbook_part.zip_path());
    check_contains(output_workbook, R"(name="ChunkRenamed")",
        "planned by-name staged chunks output should keep planned sheet name");
    check_contains(output_workbook, R"(fullCalcOnLoad="1")",
        "planned by-name staged chunks output should request workbook recalculation");
}

void test_package_editor_replaces_worksheet_from_chunk_source()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-worksheet-chunk-source-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-worksheet-chunk-source-output.xlsx");
    const std::string worksheet_prefix = "<worksheet><sheetData>";
    const std::string worksheet_body =
        R"(<row r="8"><c r="D8"><f>A1+7</f></c></row>)";
    const std::string worksheet_suffix = "</sheetData></worksheet>";
    const std::string replacement_worksheet =
        worksheet_prefix + worksheet_body + worksheet_suffix;

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    editor.replace_worksheet_part_from_chunk_source(worksheet_part,
        make_test_chunk_source({worksheet_prefix, worksheet_body, worksheet_suffix}));

    check(editor.edit_plan().full_calculation_on_load(),
        "chunk-source worksheet replacement should request full calculation");
    check(has_note_containing(editor.edit_plan().notes(),
              {"pull-based chunk source", "file-backed staged chunk"}),
        "chunk-source worksheet replacement should expose file-backed staged handoff");
    check(has_note_containing(editor.edit_plan().notes(),
              {"target/workbook/calc policy preflight", "pull-based chunk source"}),
        "chunk-source worksheet replacement should document preflight before consuming chunks");
    check(has_note_containing(editor.edit_plan().notes(),
              {"writes the staged worksheet chunk in one caller chunk-source pass",
                  "without reopening that staged chunk"}),
        "chunk-source worksheet replacement should fuse staging with validation/audit");
    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "chunk-source worksheet replacement should record worksheet edit-plan entry");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "chunk-source worksheet replacement should stream-rewrite worksheet");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, worksheet_part.zip_path(),
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "chunk-source worksheet replacement output plan should stream-rewrite worksheet");
    check_output_entry_staged_replacement_chunks(output_plan.entries,
        worksheet_part.zip_path(), true,
        "chunk-source worksheet replacement output plan should expose staged chunks instead of string data");
    check_output_entry_plan(output_plan.entries, workbook_part.zip_path(),
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "chunk-source worksheet replacement output plan should rewrite workbook calc metadata");
    check_output_entry_staged_replacement_chunks(output_plan.entries,
        workbook_part.zip_path(), false,
        "workbook calc metadata rewrite remains small XML and should not be marked as staged chunks");
    check(has_note_containing(output_plan.notes,
              {"contains formulas", "calcChain policy"}),
        "chunk-source worksheet replacement should audit formulas from streamed chunks");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_entry_bytes(output_reader, worksheet_part.zip_path(), replacement_worksheet);
    check_contains(output_reader.read_entry(workbook_part.zip_path()), "fullCalcOnLoad=\"1\"",
        "chunk-source worksheet replacement should request workbook recalculation in output");
    check_entry_bytes(output_reader, "custom/opaque.bin", source.unknown);

    fastxlsx::detail::PackageEditor invalid_source_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    bool invalid_source_failed = false;
    try {
        invalid_source_editor.replace_worksheet_part_from_chunk_source(
            worksheet_part,
            make_test_chunk_source({"<!DOCTYPE worksheet><worksheet/>"}));
    } catch (const std::exception&) {
        invalid_source_failed = true;
    }
    check(invalid_source_failed, "invalid worksheet chunk source should fail");
    check(!invalid_source_editor.edit_plan().full_calculation_on_load(),
        "invalid worksheet chunk source should not request recalculation");
    check_manifest_write_mode(invalid_source_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "invalid worksheet chunk source should not change worksheet manifest state");

    fastxlsx::detail::PackageEditor throwing_source_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const std::size_t throwing_source_initial_plan_size =
        throwing_source_editor.edit_plan().size();
    const std::size_t throwing_source_initial_note_count =
        throwing_source_editor.edit_plan().notes().size();
    const std::vector<std::filesystem::path> throwing_source_temp_files_before =
        package_editor_temp_files();
    int throwing_source_reads = 0;
    bool throwing_source_failed = false;
    try {
        throwing_source_editor.replace_worksheet_part_from_chunk_source(
            worksheet_part,
            [&](std::string& chunk) {
                ++throwing_source_reads;
                if (throwing_source_reads == 1) {
                    chunk = "<worksheet><sheetData>";
                    return true;
                }
                throw std::runtime_error("caller worksheet stream stopped");
            });
    } catch (const std::exception& error) {
        throwing_source_failed = true;
        check_contains(error.what(),
            "failed while reading planned worksheet replacement chunk source",
            "throwing worksheet chunk source should name the staging boundary");
        check_contains(error.what(), "caller worksheet stream stopped",
            "throwing worksheet chunk source should preserve the caller failure");
    }
    check(throwing_source_failed,
        "throwing chunk-source worksheet replacement should fail");
    check(throwing_source_reads == 2,
        "throwing chunk-source worksheet replacement should stop at the throwing read");
    check(throwing_source_editor.edit_plan().size() == throwing_source_initial_plan_size,
        "throwing chunk-source worksheet replacement should not change edit-plan size");
    check(throwing_source_editor.edit_plan().notes().size()
            == throwing_source_initial_note_count,
        "throwing chunk-source worksheet replacement should not add notes");
    check(!throwing_source_editor.edit_plan().full_calculation_on_load(),
        "throwing chunk-source worksheet replacement should not request recalculation");
    check_manifest_write_mode(throwing_source_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "throwing chunk-source worksheet replacement should not change worksheet manifest state");
    check_no_new_package_editor_temp_files(throwing_source_temp_files_before,
        "throwing chunk-source worksheet replacement should not leak staged temp files");

    fastxlsx::detail::PackageEditor missing_target_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    int missing_target_reads = 0;
    bool missing_target_failed = false;
    try {
        missing_target_editor.replace_worksheet_part_from_chunk_source(
            fastxlsx::detail::PartName("/xl/worksheets/missing.xml"),
            [&](std::string& chunk) {
                ++missing_target_reads;
                chunk = "<worksheet/>";
                return true;
            });
    } catch (const std::exception& error) {
        missing_target_failed = true;
        check_contains(error.what(), "worksheet replacement target is not present",
            "missing target chunk-source failure should name the worksheet target");
        check_contains(error.what(), "worksheet part '/xl/worksheets/missing.xml'",
            "missing target chunk-source failure should include the requested worksheet part");
        check_contains(error.what(), "ZIP entry 'xl/worksheets/missing.xml'",
            "missing target chunk-source failure should include the requested worksheet entry");
    }
    check(missing_target_failed,
        "missing target chunk-source worksheet replacement should fail");
    check(missing_target_reads == 0,
        "missing target chunk-source worksheet replacement should fail before consuming input");
    check(!missing_target_editor.edit_plan().full_calculation_on_load(),
        "missing target chunk-source failure should not request recalculation");

    fastxlsx::detail::PackageEditor rebuild_policy_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    fastxlsx::detail::ReferencePolicy rebuild_policy;
    rebuild_policy.calc_chain_action = fastxlsx::detail::CalcChainAction::Rebuild;
    int rebuild_policy_reads = 0;
    bool rebuild_policy_failed = false;
    try {
        rebuild_policy_editor.replace_worksheet_part_from_chunk_source(
            worksheet_part,
            [&](std::string& chunk) {
                ++rebuild_policy_reads;
                chunk = "<worksheet/>";
                return true;
            },
            rebuild_policy);
    } catch (const std::exception& error) {
        rebuild_policy_failed = true;
        check_contains(error.what(), "calcChain rebuild is not implemented",
            "rebuild policy chunk-source failure should name calcChain rebuild");
    }
    check(rebuild_policy_failed,
        "rebuild policy chunk-source worksheet replacement should fail");
    check(rebuild_policy_reads == 0,
        "rebuild policy chunk-source worksheet replacement should fail before consuming input");
    check(!rebuild_policy_editor.edit_plan().full_calculation_on_load(),
        "rebuild policy chunk-source failure should not request recalculation");
}

void test_package_editor_replaces_worksheet_by_name_from_chunk_source()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-worksheet-by-name-chunk-source-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-worksheet-by-name-chunk-source-output.xlsx");
    const std::string worksheet_prefix = "<worksheet><sheetData>";
    const std::string worksheet_body =
        R"(<row r="11"><c r="E11"><v>111</v></c></row>)";
    const std::string worksheet_suffix = "</sheetData></worksheet>";
    const std::string replacement_worksheet =
        worksheet_prefix + worksheet_body + worksheet_suffix;

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const std::string replacement_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="ChunkSourceRenamed" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, replacement_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "ordinary workbook replacement before by-name chunk-source worksheet replacement");

    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    bool old_name_failed = false;
    try {
        editor.replace_worksheet_part_from_chunk_source_by_name("Sheet1",
            make_test_chunk_source({worksheet_prefix, worksheet_body, worksheet_suffix}));
    } catch (const std::exception& error) {
        old_name_failed = true;
        check_contains(error.what(), "workbook sheet name is not present",
            "planned by-name chunk-source replacement should reject old source sheet name");
    }
    check(old_name_failed,
        "planned by-name chunk-source replacement should use planned workbook sheet names");
    check(editor.edit_plan().size() == queued_plan_size,
        "old-name chunk-source failure should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "old-name chunk-source failure should not append notes");
    check(!editor.edit_plan().full_calculation_on_load(),
        "old-name chunk-source failure should not request recalculation");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "old-name chunk-source failure should leave worksheet copy-original");

    editor.replace_worksheet_part_from_chunk_source_by_name("ChunkSourceRenamed",
        make_test_chunk_source({worksheet_prefix, worksheet_body, worksheet_suffix}));

    check(has_note_containing(editor.edit_plan().notes(),
              {"by-name worksheet chunk-source replacement", "planned/source workbook catalog"}),
        "by-name chunk-source replacement should expose catalog-based handoff");
    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr
            && worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "by-name chunk-source replacement should stream-rewrite worksheet");
    check(editor.edit_plan().full_calculation_on_load(),
        "by-name chunk-source replacement should request full calculation");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("ChunkSourceRenamed") == worksheet_part,
        "by-name chunk-source output should expose planned sheet catalog");
    check_entry_bytes(output_reader, worksheet_part.zip_path(), replacement_worksheet);
    const std::string output_workbook = output_reader.read_entry(workbook_part.zip_path());
    check_contains(output_workbook, R"(name="ChunkSourceRenamed")",
        "by-name chunk-source output should keep planned sheet name");
    check_contains(output_workbook, R"(fullCalcOnLoad="1")",
        "by-name chunk-source output should request workbook recalculation");
}

void test_package_editor_repeated_part_replacement_updates_final_state()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-repeated-replacement-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-repeated-replacement-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const std::size_t initial_plan_size = editor.edit_plan().size();

    const std::string first_workbook =
        R"(<workbook><sheets><sheet name="First" sheetId="1" r:id="rId1"/></sheets></workbook>)";
    const std::string final_workbook =
        R"(<workbook><sheets><sheet name="Final" sheetId="1" r:id="rId1"/></sheets></workbook>)";
    editor.replace_part(workbook_part, first_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "first workbook local-DOM rewrite");
    editor.replace_part(workbook_part, final_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "final workbook local-DOM rewrite");

    check(editor.edit_plan().size() == initial_plan_size,
        "repeated replacement should upsert the existing edit-plan part entry");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "repeated replacement should keep workbook in the edit plan");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "repeated replacement should keep the latest write mode in the edit plan");
    check(workbook_plan->reason.find("final workbook local-DOM rewrite") != std::string::npos,
        "repeated replacement should keep the latest reason in the edit plan");
    const auto* workbook_manifest_part = editor.manifest().find_part(workbook_part);
    check(workbook_manifest_part != nullptr,
        "repeated replacement should keep workbook in the manifest");
    check(workbook_manifest_part->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "repeated replacement should mirror the latest write mode into the manifest");
    check(workbook_manifest_part->dirty && !workbook_manifest_part->preserve_original
            && !workbook_manifest_part->generated,
        "repeated replacement should keep workbook manifest dirty but not generated");
    check(editor.edit_plan().package_entries().size() == 1,
        "repeated replacement should upsert preserved source relationships audit");
    const auto* workbook_relationships_plan =
        editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels");
    check(workbook_relationships_plan != nullptr,
        "repeated replacement should audit preserved workbook relationships");
    check(workbook_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "preserved workbook relationships audit should stay copy-original");
    check(workbook_relationships_plan->reason.find("/xl/workbook.xml") != std::string::npos,
        "preserved workbook relationships audit should name the owner part");
    check(workbook_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "preserved workbook relationships audit should keep structured source-relationships role");
    check(workbook_relationships_plan->owner_part == workbook_part.value(),
        "preserved workbook relationships audit should keep structured owner part");
    check(editor.edit_plan().removed_parts().empty(),
        "repeated replacement should not record removed parts");
    check(editor.edit_plan().removed_package_entries().empty(),
        "repeated replacement should not record removed package entries");
    check(!editor.edit_plan().full_calculation_on_load(),
        "ordinary repeated replacement should not request full calculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "ordinary repeated replacement should leave calcChain action unchanged");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/workbook.xml") == final_workbook,
        "repeated replacement output should write the final replacement bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "repeated replacement output should preserve workbook relationships bytes");
    check(output_reader.read_entry("docProps/core.xml") == source.core_properties,
        "repeated replacement output should preserve core properties bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "repeated replacement output should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "repeated replacement output should preserve unknown bytes");
}

void test_package_editor_replacement_audits_preserved_root_relationships()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-editor-root-rels-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-root-rels-output.xlsx");

    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(</Types>)";
    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"/>)";
    const std::string root_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXml" Target="custom/opaque.bin"/>)"
        R"(</Relationships>)";
    const std::string unknown = "root-owned opaque bytes";

    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"root.xml", "<root/>"},
            {"_rels/root.xml.rels", root_relationships},
            {"custom/opaque.bin", unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source_path);
    const fastxlsx::detail::PartName root_part("/root.xml");
    const std::string replacement_root = R"(<root updated="1"/>)";
    replace_part_with_memory_chunks(editor, root_part, replacement_root,
        "test root part rewrite");

    const auto* root_plan = editor.edit_plan().find_part(root_part);
    check(root_plan != nullptr,
        "root replacement should remain visible in the edit plan");
    check(root_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "root replacement should be staged stream-rewrite");
    const auto* root_relationships_plan =
        editor.edit_plan().find_package_entry("_rels/root.xml.rels");
    check(root_relationships_plan != nullptr,
        "root replacement should audit preserved root source relationships");
    check(root_relationships_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "preserved root source relationships should be copy-original");
    check(root_relationships_plan->reason.find("/root.xml") != std::string::npos,
        "root relationships audit should name the owner part");
    check(root_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "root relationships audit should keep structured source-relationships role");
    check(root_relationships_plan->owner_part == root_part.value(),
        "root relationships audit should keep structured owner part");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("root.xml") == replacement_root,
        "root replacement should write replacement bytes");
    check(output_reader.read_entry("_rels/root.xml.rels") == root_relationships,
        "root replacement should preserve root relationship bytes");
    check(output_reader.read_entry("custom/opaque.bin") == unknown,
        "root replacement should preserve unknown linked bytes");
    const auto* output_root_relationships = output_reader.relationships_for(root_part);
    check(output_root_relationships != nullptr,
        "root replacement output should keep root source relationships readable");
    check(output_root_relationships->find_by_id("rId1") != nullptr,
        "root replacement output should keep root source relationship id");
    check(output_root_relationships->find_by_id("rId1")->target == "custom/opaque.bin",
        "root replacement output root source relationship target mismatch");
    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    const auto* output_graph_root_relationships =
        output_graph.relationships_for(root_part);
    check(output_graph_root_relationships != nullptr,
        "root replacement output relationship graph should include root source relationships");
    check(output_graph_root_relationships->find_by_id("rId1") != nullptr,
        "root replacement output relationship graph should keep root source relationship id");
    check(output_reader.part_index().find_part(
              fastxlsx::detail::PartName("/_rels/root.xml.rels")) == nullptr,
        "root relationships entry should remain metadata-only after replacement");
}

void test_package_editor_sets_document_properties_and_adds_missing_metadata_parts()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-docprops-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-docprops-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName app_part("/docProps/app.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    check(editor.manifest().find_part(app_part) == nullptr,
        "source package should start without extended properties");

    fastxlsx::DocumentProperties properties;
    properties.creator = "Patch <Author>";
    properties.last_modified_by = "Patch & Reviewer";
    properties.title = "Existing metadata";
    properties.description = "Generated by PackageEditor";
    properties.application = "FastXLSX Patch";
    properties.app_version = "4.0";
    editor.set_document_properties(properties);

    const auto* core_plan = editor.edit_plan().find_part(core_part);
    check(core_plan != nullptr,
        "document properties rewrite should record core properties plan");
    check(core_plan->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "core properties should be generated small XML");
    const auto* app_plan = editor.edit_plan().find_part(app_part);
    check(app_plan != nullptr,
        "document properties rewrite should record extended properties plan");
    check(app_plan->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "extended properties should be generated small XML");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "document properties rewrite should keep unknown parts copy-original");
    const auto* content_types_plan =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_plan != nullptr,
        "document properties rewrite should record content types entry rewrite");
    check(content_types_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "document properties content types entry should be local-DOM-rewrite");
    check(content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "document properties content types entry should keep structured audit role");
    check(content_types_plan->owner_part.empty(),
        "document properties content types entry should not carry source owner");
    const auto* package_relationships_plan =
        editor.edit_plan().find_package_entry("_rels/.rels");
    check(package_relationships_plan != nullptr,
        "document properties rewrite should record package relationships entry rewrite");
    check(package_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "document properties package relationships entry should be local-DOM-rewrite");
    check(package_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::PackageRelationships,
        "document properties package relationships entry should keep structured audit role");
    check(package_relationships_plan->owner_part.empty(),
        "package relationships entry should not carry source owner");
    const auto* core_manifest_part = editor.manifest().find_part(core_part);
    check(core_manifest_part != nullptr,
        "document properties rewrite should keep core properties in manifest");
    check(core_manifest_part->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml
            && core_manifest_part->generated && core_manifest_part->dirty
            && !core_manifest_part->preserve_original,
        "document properties rewrite should mark core properties generated");
    const auto* app_manifest_part = editor.manifest().find_part(app_part);
    check(app_manifest_part != nullptr,
        "document properties rewrite should add extended properties to manifest");
    check(app_manifest_part->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml
            && app_manifest_part->generated && app_manifest_part->dirty
            && !app_manifest_part->preserve_original,
        "document properties rewrite should mark extended properties generated");

    const std::vector<fastxlsx::detail::PackageEditorOutputEntryPlan> output_plan =
        editor.planned_output_entries();
    check_output_entry_plan(output_plan, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "document properties output plan should rewrite content types");
    check_output_entry_part_context(output_plan, "[Content_Types].xml", false, "",
        "document properties output plan should keep content types as metadata entry");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "document properties output plan should classify content types metadata");
    check_output_entry_plan(output_plan, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "document properties output plan should rewrite package relationships");
    check_output_entry_part_context(output_plan, "_rels/.rels", false, "",
        "document properties output plan should keep package relationships as metadata entry");
    const auto* output_package_relationships_plan =
        find_output_entry_plan(output_plan, "_rels/.rels");
    check(output_package_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::PackageRelationships,
        "document properties output plan should classify package relationships metadata");
    check_output_entry_plan(output_plan, "docProps/core.xml",
        fastxlsx::detail::PartWriteMode::GenerateSmallXml, true, true, false, false,
        "document properties output plan should regenerate core properties");
    check_output_entry_part_context(output_plan, "docProps/core.xml", true,
        "/docProps/core.xml",
        "document properties output plan should classify core properties as package part");
    check_output_entry_materialized_replacement(output_plan, "docProps/core.xml", true,
        "document properties output plan should expose core properties as materialized package-part replacement");
    check_output_entry_materialized_replacement_reason(output_plan, "docProps/core.xml",
        "generated small-XML package part",
        "document properties output plan should explain core properties package-part materialization");
    check_output_entry_plan(output_plan, "docProps/app.xml",
        fastxlsx::detail::PartWriteMode::GenerateSmallXml, false, true, false, false,
        "document properties output plan should append generated app properties");
    check_output_entry_part_context(output_plan, "docProps/app.xml", true,
        "/docProps/app.xml",
        "document properties output plan should classify app properties as package part");
    check_output_entry_materialized_replacement(output_plan, "docProps/app.xml", true,
        "document properties output plan should expose app properties as materialized package-part replacement");
    check_output_entry_materialized_replacement_reason(output_plan, "docProps/app.xml",
        "generated small-XML package part",
        "document properties output plan should explain app properties package-part materialization");
    check_output_entry_plan(output_plan, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "document properties output plan should preserve unknown entry");
    check_output_entry_part_context(output_plan, "custom/opaque.bin", true,
        "/custom/opaque.bin",
        "document properties output plan should classify unknown entry as package part");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("docProps/app.xml") != entries.end(),
        "document properties rewrite should add missing app properties entry");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string core_xml = output_reader.read_entry("docProps/core.xml");
    check_contains(core_xml, "<dc:creator>Patch &lt;Author&gt;</dc:creator>",
        "core properties creator should be generated and escaped");
    check_contains(core_xml, "<cp:lastModifiedBy>Patch &amp; Reviewer</cp:lastModifiedBy>",
        "core properties lastModifiedBy should be generated and escaped");
    check_contains(core_xml, "<dc:title>Existing metadata</dc:title>",
        "core properties title should be generated");
    check_contains(core_xml, "<dc:description>Generated by PackageEditor</dc:description>",
        "core properties description should be generated");

    const std::string app_xml = output_reader.read_entry("docProps/app.xml");
    check_contains(app_xml, "<Application>FastXLSX Patch</Application>",
        "extended properties application should be generated");
    check_contains(app_xml, "<AppVersion>4.0</AppVersion>",
        "extended properties app version should be generated");

    const std::string content_types = output_reader.read_entry("[Content_Types].xml");
    check_contains(content_types, "/docProps/core.xml",
        "document properties rewrite should keep core content type override");
    check_contains(content_types, "/docProps/app.xml",
        "document properties rewrite should add app content type override");
    check_contains(content_types,
        "application/vnd.openxmlformats-officedocument.extended-properties+xml",
        "document properties rewrite should add app content type");

    const std::string package_relationships = output_reader.read_entry("_rels/.rels");
    check_contains(package_relationships, "Target=\"docProps/core.xml\"",
        "document properties rewrite should keep core package relationship");
    check_contains(package_relationships, "Target=\"docProps/app.xml\"",
        "document properties rewrite should add app package relationship");
    check_contains(package_relationships, "relationships/extended-properties",
        "document properties rewrite should add app package relationship type");
    const auto* parsed_app_relationship =
        output_reader.package_relationships().find_by_id("rId3");
    check(parsed_app_relationship != nullptr,
        "document properties rewrite should allocate a new package relationship id");
    check(parsed_app_relationship->target == "docProps/app.xml",
        "document properties rewrite app relationship target mismatch");

    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "document properties rewrite should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "document properties rewrite should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "document properties rewrite should preserve unknown bytes");
}

void test_package_editor_document_properties_preserves_custom_properties_part()
{
    SourcePackage source =
        write_source_package("fastxlsx-package-editor-docprops-custom-source.xlsx");
    source.content_types = R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/docProps/core.xml" ContentType="application/vnd.openxmlformats-package.core-properties+xml"/>)"
        R"(<Override PartName="/docProps/custom.xml" ContentType="application/vnd.openxmlformats-officedocument.custom-properties+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(</Types>)";
    source.package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/>)"
        R"(<Relationship Id="rIdCustomProperties" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/custom-properties" Target="docProps/custom.xml"/>)"
        R"(</Relationships>)";
    const std::string custom_properties =
        R"(<Properties xmlns="http://schemas.openxmlformats.org/officeDocument/2006/custom-properties" xmlns:vt="http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes">)"
        R"(<property fmtid="{D5CDD505-2E9C-101B-9397-08002B2CF9AE}" pid="2" name="FastXLSXMarker"><vt:lpwstr>preserve custom property</vt:lpwstr></property>)"
        R"(</Properties>)";

    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"docProps/core.xml", source.core_properties},
            {"docProps/custom.xml", custom_properties},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"custom/opaque.bin", source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-docprops-custom-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName app_part("/docProps/app.xml");
    const fastxlsx::detail::PartName custom_part("/docProps/custom.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    fastxlsx::DocumentProperties properties;
    properties.creator = "Core Rewriter";
    properties.application = "Extended Rewriter";
    editor.set_document_properties(properties);

    const auto* custom_plan = editor.edit_plan().find_part(custom_part);
    check(custom_plan != nullptr,
        "custom properties part should remain visible in document properties edit plan");
    check(custom_plan != nullptr
            && custom_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom properties part should remain copy-original during core/app rewrite");
    const auto* core_plan = editor.edit_plan().find_part(core_part);
    check(core_plan != nullptr,
        "core properties should remain visible with custom properties present");
    check(core_plan != nullptr
            && core_plan->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "core properties should still be generated small XML with custom properties present");
    const auto* app_plan = editor.edit_plan().find_part(app_part);
    check(app_plan != nullptr,
        "extended properties should remain visible with custom properties present");
    check(app_plan != nullptr
            && app_plan->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "extended properties should still be generated small XML with custom properties present");
    const auto* unknown_plan = editor.edit_plan().find_part(unknown_part);
    check(unknown_plan != nullptr,
        "unknown part should remain visible with custom properties present");
    check(unknown_plan != nullptr
            && unknown_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom docprops rewrite should keep unrelated unknown part copy-original");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") != nullptr,
        "custom docprops rewrite should audit content types rewrite for missing app props");
    check(editor.edit_plan().find_package_entry("_rels/.rels") != nullptr,
        "custom docprops rewrite should audit package relationships rewrite for missing app props");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(editor.planned_output_entries().size() == output_plan.entries.size(),
        "custom docprops output plan should match entry preview size");
    check(output_plan.relationship_target_audits.empty(),
        "custom docprops output plan should not invent relationship target audits");
    check(output_plan.removed_parts.empty(),
        "custom docprops output plan should not record removed parts");
    check(output_plan.removed_package_entries.empty(),
        "custom docprops output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "custom docprops output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "custom docprops output plan should keep content types as metadata entry");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan != nullptr
            && output_content_types_plan->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "custom docprops output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "custom docprops output plan should rewrite package relationships");
    check_output_entry_part_context(output_plan.entries, "_rels/.rels", false, "",
        "custom docprops output plan should keep package relationships as metadata entry");
    const auto* output_package_relationships_plan =
        find_output_entry_plan(output_plan.entries, "_rels/.rels");
    check(output_package_relationships_plan != nullptr
            && output_package_relationships_plan->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::PackageRelationships,
        "custom docprops output plan should classify package relationships metadata");
    check_output_entry_plan(output_plan.entries, "docProps/core.xml",
        fastxlsx::detail::PartWriteMode::GenerateSmallXml, true, true, false, false,
        "custom docprops output plan should regenerate core properties");
    check_output_entry_part_context(output_plan.entries, "docProps/core.xml", true,
        core_part.value(),
        "custom docprops output plan should classify core properties as package part");
    check_output_entry_plan(output_plan.entries, "docProps/app.xml",
        fastxlsx::detail::PartWriteMode::GenerateSmallXml, false, true, false, false,
        "custom docprops output plan should append generated app properties");
    check_output_entry_part_context(output_plan.entries, "docProps/app.xml", true,
        app_part.value(),
        "custom docprops output plan should classify app properties as package part");
    check_output_entry_plan(output_plan.entries, "docProps/custom.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom docprops output plan should preserve custom properties part");
    check_output_entry_part_context(output_plan.entries, "docProps/custom.xml", true,
        custom_part.value(),
        "custom docprops output plan should classify custom properties as package part");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom docprops output plan should preserve unrelated unknown part");
    check_output_entry_part_context(output_plan.entries, "custom/opaque.bin", true,
        unknown_part.value(),
        "custom docprops output plan should classify unknown entry as package part");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("docProps/custom.xml") == custom_properties,
        "document properties rewrite should preserve custom properties bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "custom docprops rewrite should preserve unknown bytes");

    const std::string content_types = output_reader.read_entry("[Content_Types].xml");
    check_contains(content_types, "/docProps/custom.xml",
        "custom docprops rewrite should preserve custom properties content type override");
    check_contains(content_types,
        "application/vnd.openxmlformats-officedocument.custom-properties+xml",
        "custom docprops rewrite should preserve custom properties content type");
    check_contains(content_types, "/docProps/app.xml",
        "custom docprops rewrite should still add app properties content type");

    const std::string package_relationships = output_reader.read_entry("_rels/.rels");
    check_contains(package_relationships, "Target=\"docProps/custom.xml\"",
        "custom docprops rewrite should preserve custom properties package relationship");
    check_contains(package_relationships, "relationships/custom-properties",
        "custom docprops rewrite should preserve custom properties relationship type");
    check_contains(package_relationships, "Target=\"docProps/app.xml\"",
        "custom docprops rewrite should still add app package relationship");

    const auto* custom_relationship =
        output_reader.package_relationships().find_by_id("rIdCustomProperties");
    check(custom_relationship != nullptr,
        "custom docprops rewrite should keep parsed custom properties relationship id");
    check(custom_relationship != nullptr
            && custom_relationship->target == "docProps/custom.xml",
        "custom docprops rewrite should keep parsed custom properties relationship target");
    check(custom_relationship != nullptr
            && custom_relationship->target_mode
                == fastxlsx::detail::Relationship::TargetMode::Internal,
        "custom docprops rewrite should keep custom properties relationship internal");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.package_relationships().find_by_id("rIdCustomProperties") != nullptr,
        "relationship graph should keep package custom properties relationship");
    const auto* custom_content_type = output_reader.content_types().override_for(custom_part);
    check(custom_content_type != nullptr,
        "custom docprops rewrite should keep custom properties override registered");
}

void test_package_editor_document_properties_adds_missing_core_and_app_parts()
{
    SourcePackage source =
        write_source_package("fastxlsx-package-editor-docprops-missing-source.xlsx");
    source.content_types = R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(</Types>)";
    source.package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";

    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"custom/opaque.bin", source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-docprops-missing-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName app_part("/docProps/app.xml");

    check(editor.manifest().find_part(core_part) == nullptr,
        "missing-docprops source should start without core properties");
    check(editor.manifest().find_part(app_part) == nullptr,
        "missing-docprops source should start without extended properties");

    fastxlsx::DocumentProperties properties;
    properties.creator = "Generated Core";
    properties.application = "Generated App";
    editor.set_document_properties(properties);

    const auto* core_plan = editor.edit_plan().find_part(core_part);
    check(core_plan != nullptr,
        "missing-docprops rewrite should record generated core properties");
    check(core_plan->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "missing core properties should be generated small XML");
    const auto* app_plan = editor.edit_plan().find_part(app_part);
    check(app_plan != nullptr,
        "missing-docprops rewrite should record generated extended properties");
    check(app_plan->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "missing extended properties should be generated small XML");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") != nullptr,
        "missing-docprops rewrite should audit content types rewrite");
    check(editor.edit_plan().find_package_entry("_rels/.rels") != nullptr,
        "missing-docprops rewrite should audit package relationships rewrite");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("docProps/core.xml") != entries.end(),
        "missing-docprops output should add core properties entry");
    check(entries.find("docProps/app.xml") != entries.end(),
        "missing-docprops output should add app properties entry");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string core_xml = output_reader.read_entry("docProps/core.xml");
    check_contains(core_xml, "<dc:creator>Generated Core</dc:creator>",
        "missing-docprops output should generate core properties XML");
    const std::string app_xml = output_reader.read_entry("docProps/app.xml");
    check_contains(app_xml, "<Application>Generated App</Application>",
        "missing-docprops output should generate extended properties XML");

    const std::string content_types = output_reader.read_entry("[Content_Types].xml");
    check_contains(content_types, "/docProps/core.xml",
        "missing-docprops output should add core content type override");
    check_contains(content_types, "/docProps/app.xml",
        "missing-docprops output should add app content type override");

    const std::string package_relationships = output_reader.read_entry("_rels/.rels");
    check_contains(package_relationships, "Target=\"xl/workbook.xml\"",
        "missing-docprops output should preserve officeDocument package relationship");
    check_contains(package_relationships, "Target=\"docProps/core.xml\"",
        "missing-docprops output should add core package relationship");
    check_contains(package_relationships, "Target=\"docProps/app.xml\"",
        "missing-docprops output should add app package relationship");
    check(output_reader.package_relationships().find_by_id("rId2") != nullptr,
        "missing-docprops output should allocate core package relationship id");
    check(output_reader.package_relationships().find_by_id("rId3") != nullptr,
        "missing-docprops output should allocate app package relationship id");

    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "missing-docprops rewrite should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "missing-docprops rewrite should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "missing-docprops rewrite should preserve unknown bytes");
}

void test_package_editor_part_replacement_overrides_generated_document_properties()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-docprops-override-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-docprops-override-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName app_part("/docProps/app.xml");

    fastxlsx::DocumentProperties properties;
    properties.creator = "Generated Creator";
    properties.application = "Generated App";
    editor.set_document_properties(properties);

    const std::string final_core =
        R"(<cp:coreProperties><dc:creator>Final Core</dc:creator></cp:coreProperties>)";
    const std::string final_app =
        R"(<Properties><Application>Final App</Application></Properties>)";
    editor.replace_part(core_part, final_core,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "final core properties replacement");
    editor.replace_part(app_part, final_app,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "final extended properties replacement");

    const auto* core_plan = editor.edit_plan().find_part(core_part);
    check(core_plan != nullptr,
        "docprops override should keep core properties in the edit plan");
    check(core_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "docprops override should replace generated core write mode");
    check(core_plan->reason.find("final core properties replacement") != std::string::npos,
        "docprops override should keep final core replacement reason");
    const auto* app_plan = editor.edit_plan().find_part(app_part);
    check(app_plan != nullptr,
        "docprops override should keep app properties in the edit plan");
    check(app_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "docprops override should replace generated app write mode");
    check(app_plan->reason.find("final extended properties replacement") != std::string::npos,
        "docprops override should keep final app replacement reason");

    const auto* core_manifest_part = editor.manifest().find_part(core_part);
    check(core_manifest_part != nullptr,
        "docprops override should keep core properties in the manifest");
    check(core_manifest_part->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite
            && core_manifest_part->dirty && !core_manifest_part->generated
            && !core_manifest_part->preserve_original,
        "docprops override should mark core properties as ordinary rewrite");
    const auto* app_manifest_part = editor.manifest().find_part(app_part);
    check(app_manifest_part != nullptr,
        "docprops override should keep app properties in the manifest");
    check(app_manifest_part->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite
            && app_manifest_part->dirty && !app_manifest_part->generated
            && !app_manifest_part->preserve_original,
        "docprops override should mark app properties as ordinary rewrite");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") != nullptr,
        "docprops override should preserve content types audit for added app part");
    check(editor.edit_plan().find_package_entry("_rels/.rels") != nullptr,
        "docprops override should preserve package relationships audit for added app part");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("docProps/app.xml") != entries.end(),
        "docprops override output should still add missing app entry");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("docProps/core.xml") == final_core,
        "docprops override output should write final core replacement bytes");
    check(output_reader.read_entry("docProps/app.xml") == final_app,
        "docprops override output should write final app replacement bytes");
    const std::string content_types = output_reader.read_entry("[Content_Types].xml");
    check_contains(content_types, "/docProps/app.xml",
        "docprops override output should keep app content type override");
    const std::string package_relationships = output_reader.read_entry("_rels/.rels");
    check_contains(package_relationships, "Target=\"docProps/app.xml\"",
        "docprops override output should keep app package relationship");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "docprops override output should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "docprops override output should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "docprops override output should preserve unknown bytes");
}

void test_package_editor_document_properties_override_prior_part_replacement()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-docprops-helper-override-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-docprops-helper-override-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName app_part("/docProps/app.xml");

    const std::string prior_core =
        R"(<cp:coreProperties><dc:creator>Prior Core</dc:creator></cp:coreProperties>)";
    editor.replace_part(core_part, prior_core,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "prior ordinary core properties replacement");

    fastxlsx::DocumentProperties properties;
    properties.creator = "Helper Creator";
    properties.title = "Helper Title";
    properties.application = "Helper App";
    properties.app_version = "7.1";
    editor.set_document_properties(properties);

    const auto* core_plan = editor.edit_plan().find_part(core_part);
    check(core_plan != nullptr,
        "docprops helper override should keep core properties in the edit plan");
    check(core_plan->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "docprops helper override should replace prior ordinary core write mode");
    check(core_plan->reason.find("core document properties generated small XML")
            != std::string::npos,
        "docprops helper override should keep helper core reason");
    const auto* app_plan = editor.edit_plan().find_part(app_part);
    check(app_plan != nullptr,
        "docprops helper override should add app properties to the edit plan");
    check(app_plan->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "docprops helper override should keep app write mode generated");

    const auto* core_manifest_part = editor.manifest().find_part(core_part);
    check(core_manifest_part != nullptr,
        "docprops helper override should keep core properties in the manifest");
    check(core_manifest_part->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml
            && core_manifest_part->dirty && core_manifest_part->generated
            && !core_manifest_part->preserve_original,
        "docprops helper override should mark core properties as generated");
    const auto* app_manifest_part = editor.manifest().find_part(app_part);
    check(app_manifest_part != nullptr,
        "docprops helper override should add app properties to the manifest");
    check(app_manifest_part->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml
            && app_manifest_part->dirty && app_manifest_part->generated
            && !app_manifest_part->preserve_original,
        "docprops helper override should mark app properties as generated");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") != nullptr,
        "docprops helper override should audit content types for added app part");
    check(editor.edit_plan().find_package_entry("_rels/.rels") != nullptr,
        "docprops helper override should audit package relationships for added app part");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("docProps/app.xml") != entries.end(),
        "docprops helper override output should still add app properties");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string core_xml = output_reader.read_entry("docProps/core.xml");
    check_contains(core_xml, "<dc:creator>Helper Creator</dc:creator>",
        "docprops helper override output should write generated core properties");
    check_contains(core_xml, "<dc:title>Helper Title</dc:title>",
        "docprops helper override output should write generated core title");
    check_not_contains(core_xml, "Prior Core",
        "docprops helper override output should not write stale ordinary core bytes");
    const std::string app_xml = output_reader.read_entry("docProps/app.xml");
    check_contains(app_xml, "<Application>Helper App</Application>",
        "docprops helper override output should write generated app properties");
    check_contains(app_xml, "<AppVersion>7.1</AppVersion>",
        "docprops helper override output should write generated app version");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "docprops helper override output should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "docprops helper override output should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "docprops helper override output should preserve unknown bytes");
}

void test_package_editor_document_properties_override_prior_part_removal()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-docprops-helper-removal-source.xlsx");
    const std::string core_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdCore1" Type="http://example.com/fastxlsx/core-note" Target="https://example.com/core-note" TargetMode="External"/>)"
        R"(</Relationships>)";
    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"docProps/core.xml", source.core_properties},
            {"docProps/_rels/core.xml.rels", core_relationships},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"custom/opaque.bin", source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-docprops-helper-removal-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName app_part("/docProps/app.xml");

    editor.remove_part(core_part, "temporary core properties removal");
    check(editor.edit_plan().find_removed_part(core_part) != nullptr,
        "docprops helper removal setup should record removed core properties");
    check(editor.manifest().find_part(core_part) == nullptr,
        "docprops helper removal setup should remove core properties from the manifest");
    check(editor.edit_plan().find_removed_package_entry("docProps/_rels/core.xml.rels")
            != nullptr,
        "docprops helper removal setup should omit core owner relationships");

    fastxlsx::DocumentProperties properties;
    properties.creator = "Restored Creator";
    properties.title = "Restored Title";
    properties.application = "Restored App";
    editor.set_document_properties(properties);

    check(editor.edit_plan().find_removed_part(core_part) == nullptr,
        "docprops helper should clear stale removed core properties audit");
    const auto* core_plan = editor.edit_plan().find_part(core_part);
    check(core_plan != nullptr,
        "docprops helper should restore core properties to the edit plan");
    check(core_plan->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "docprops helper should restore removed core as generated small XML");
    const auto* app_plan = editor.edit_plan().find_part(app_part);
    check(app_plan != nullptr,
        "docprops helper should add extended properties after prior removal");
    check(app_plan->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "docprops helper should keep extended properties generated");
    const auto* core_manifest_part = editor.manifest().find_part(core_part);
    check(core_manifest_part != nullptr,
        "docprops helper should restore core properties to the manifest");
    check(core_manifest_part->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml
            && core_manifest_part->dirty && core_manifest_part->generated
            && !core_manifest_part->preserve_original,
        "docprops helper should mark restored core properties generated");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") != nullptr,
        "docprops helper after removal should keep content types audit");
    check(editor.edit_plan().find_package_entry("_rels/.rels") != nullptr,
        "docprops helper after removal should keep package relationships audit");
    const auto* removed_core_relationships =
        editor.edit_plan().find_removed_package_entry("docProps/_rels/core.xml.rels");
    check(removed_core_relationships != nullptr,
        "docprops helper should not restore prior removed core owner relationships");
    check(removed_core_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "docprops helper should keep removed core owner relationships as source-owned audit");
    check(removed_core_relationships->owner_part == core_part.value(),
        "docprops helper should keep removed core owner part in the audit");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("docProps/core.xml") != entries.end(),
        "docprops helper after removal output should restore core properties entry");
    check(entries.find("docProps/app.xml") != entries.end(),
        "docprops helper after removal output should add app properties entry");
    check(entries.find("docProps/_rels/core.xml.rels") == entries.end(),
        "docprops helper after removal output should not restore removed core owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string core_xml = output_reader.read_entry("docProps/core.xml");
    check_contains(core_xml, "<dc:creator>Restored Creator</dc:creator>",
        "docprops helper after removal output should write generated core XML");
    check_contains(core_xml, "<dc:title>Restored Title</dc:title>",
        "docprops helper after removal output should write generated title");
    check_not_contains(core_xml, "Original",
        "docprops helper after removal output should not keep stale core bytes");
    const std::string app_xml = output_reader.read_entry("docProps/app.xml");
    check_contains(app_xml, "<Application>Restored App</Application>",
        "docprops helper after removal output should write generated app XML");

    const std::string content_types = output_reader.read_entry("[Content_Types].xml");
    check_contains(content_types, "/docProps/core.xml",
        "docprops helper after removal output should restore core content type override");
    check_contains(content_types, "/docProps/app.xml",
        "docprops helper after removal output should add app content type override");
    const std::string package_relationships = output_reader.read_entry("_rels/.rels");
    check_contains(package_relationships, "Target=\"docProps/core.xml\"",
        "docprops helper after removal output should keep core package relationship");
    check_contains(package_relationships, "Target=\"docProps/app.xml\"",
        "docprops helper after removal output should add app package relationship");
    check(output_reader.package_relationships().find_by_id("rId2") != nullptr,
        "docprops helper after removal output should keep core relationship readable");
    check(output_reader.package_relationships().find_by_id("rId3") != nullptr,
        "docprops helper after removal output should add app relationship readable");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "docprops helper after removal output should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "docprops helper after removal output should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "docprops helper after removal output should preserve unknown bytes");
}

void test_package_editor_document_properties_failure_preserves_state()
{
    SourcePackage source =
        write_source_package("fastxlsx-package-editor-docprops-conflict-source.xlsx");
    source.package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/wrong-core.xml"/>)"
        R"(</Relationships>)";
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

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-docprops-conflict-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName app_part("/docProps/app.xml");
    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();

    bool failed = false;
    try {
        editor.set_document_properties({});
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed,
        "document properties rewrite should reject conflicting package relationships");
    check(editor.edit_plan().size() == initial_plan_size,
        "document properties failure should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "document properties failure should not change edit plan notes");
    check(editor.edit_plan().find_part(core_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "document properties failure should leave core properties copy-original");
    check_manifest_write_mode(editor, core_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "document properties failure should leave core manifest copy-original");
    check(editor.edit_plan().find_part(app_part) == nullptr,
        "document properties failure should not add app properties to the edit plan");
    check(editor.manifest().find_part(app_part) == nullptr,
        "document properties failure should not add app properties to the manifest");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("docProps/app.xml") == entries.end(),
        "document properties failure output should not add app properties");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("docProps/core.xml") == source.core_properties,
        "document properties failure output should preserve core properties bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "document properties failure output should preserve package relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "document properties failure output should preserve content types bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "document properties failure output should preserve unknown bytes");
}

void test_package_editor_document_properties_app_relationship_failure_preserves_state()
{
    SourcePackage source =
        write_source_package("fastxlsx-package-editor-docprops-app-conflict-source.xlsx");
    source.package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/>)"
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties" Target="docProps/wrong-app.xml"/>)"
        R"(</Relationships>)";
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

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-docprops-app-conflict-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName app_part("/docProps/app.xml");
    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();

    bool failed = false;
    try {
        editor.set_document_properties({});
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed,
        "document properties rewrite should reject conflicting extended package relationships");
    check(editor.edit_plan().size() == initial_plan_size,
        "extended relationship conflict should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "extended relationship conflict should not change edit plan notes");
    check(editor.edit_plan().find_part(core_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "extended relationship conflict should leave core properties copy-original");
    check_manifest_write_mode(editor, core_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "extended relationship conflict should leave core manifest copy-original");
    check(editor.edit_plan().find_part(app_part) == nullptr,
        "extended relationship conflict should not add app properties to the edit plan");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "extended relationship conflict should not audit content types rewrite");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "extended relationship conflict should not audit package relationships rewrite");
    check(editor.manifest().find_part(app_part) == nullptr,
        "extended relationship conflict should not add app properties to the manifest");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("docProps/app.xml") == entries.end(),
        "extended relationship conflict output should not add app properties");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("docProps/core.xml") == source.core_properties,
        "extended relationship conflict output should preserve core properties bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "extended relationship conflict output should preserve package relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "extended relationship conflict output should preserve content types bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "extended relationship conflict output should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "extended relationship conflict output should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "extended relationship conflict output should preserve unknown bytes");
}

void test_package_editor_combines_document_properties_and_worksheet_rewrite()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-combined-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-combined-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName app_part("/docProps/app.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    fastxlsx::DocumentProperties properties;
    properties.creator = "Patch Author";
    properties.last_modified_by = "Patch Reviewer";
    properties.title = "Combined patch";
    properties.application = "FastXLSX Patch";
    properties.app_version = "4.1";
    editor.set_document_properties(properties);

    const std::string replacement_sheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>123</v></c></row></sheetData></worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_sheet);

    check(editor.edit_plan().find_part(core_part)->write_mode
            == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "combined patch should keep core properties generated");
    check(editor.edit_plan().find_part(app_part)->write_mode
            == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "combined patch should keep app properties generated");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "combined patch should stream-rewrite worksheet");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "combined patch should local-DOM-rewrite workbook metadata");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "combined patch should record calcChain removal");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "combined patch should keep unknown part copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("docProps/core.xml") != entries.end(),
        "combined patch should write core properties");
    check(entries.find("docProps/app.xml") != entries.end(),
        "combined patch should write app properties");
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "combined patch should omit stale calcChain.xml");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_sheet,
        "combined patch should write replacement worksheet XML");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "combined patch should preserve unknown entry bytes");

    const std::string core_xml = output_reader.read_entry("docProps/core.xml");
    check_contains(core_xml, "<dc:creator>Patch Author</dc:creator>",
        "combined patch should write core creator");
    check_contains(core_xml, "<dc:title>Combined patch</dc:title>",
        "combined patch should write core title");
    const std::string app_xml = output_reader.read_entry("docProps/app.xml");
    check_contains(app_xml, "<Application>FastXLSX Patch</Application>",
        "combined patch should write app properties");

    const std::string content_types = output_reader.read_entry("[Content_Types].xml");
    check_contains(content_types, "/docProps/core.xml",
        "combined patch should include core content type");
    check_contains(content_types, "/docProps/app.xml",
        "combined patch should include app content type");
    check_not_contains(content_types, "calcChain+xml",
        "combined patch content types should remove calcChain");
    check(output_reader.content_types().override_for(core_part) != nullptr,
        "combined patch parsed content types should include core properties");
    check(output_reader.content_types().override_for(app_part) != nullptr,
        "combined patch parsed content types should include app properties");
    check(output_reader.content_types().override_for(calc_chain_part) == nullptr,
        "combined patch parsed content types should omit calcChain");

    const std::string package_relationships = output_reader.read_entry("_rels/.rels");
    check_contains(package_relationships, "Target=\"docProps/core.xml\"",
        "combined patch should include core package relationship");
    check_contains(package_relationships, "Target=\"docProps/app.xml\"",
        "combined patch should include app package relationship");
    check(output_reader.package_relationships().find_by_id("rId1") != nullptr,
        "combined patch should keep office document package relationship");
    check(output_reader.package_relationships().find_by_id("rId2") != nullptr,
        "combined patch should add core package relationship id");
    check(output_reader.package_relationships().find_by_id("rId3") != nullptr,
        "combined patch should add app package relationship id");

    const std::string workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_contains(workbook_relationships, "relationships/worksheet",
        "combined patch should keep worksheet workbook relationship");
    check_not_contains(workbook_relationships, "relationships/calcChain",
        "combined patch should remove calcChain workbook relationship");

    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "combined patch should request full calculation on load");
}

void test_package_editor_replaces_worksheet_cells_by_name_with_file_backed_transformer_handoff()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-cell-replacement-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-cell-replacement-output.xlsx");
    const std::vector<std::filesystem::path> temp_files_before =
        package_editor_temp_files();

    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const std::array replacements {
        worksheet_cell_replacement(
            "A1", R"(<c r="A1" t="inlineStr"><is><t>patched</t></is></c>)"),
    };

    {
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);

        editor.replace_worksheet_cells_by_name("Sheet1", replacements);

        const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
        check(worksheet_plan != nullptr,
            "cell replacement handoff should keep worksheet in the edit plan");
        check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
            "cell replacement handoff should expose staged stream rewrite mode");
        check(worksheet_plan->reason.find("file-backed stream rewrite")
                != std::string::npos,
            "cell replacement handoff should describe file-backed staged transformer output");
        check_manifest_write_mode(editor, worksheet_part,
            fastxlsx::detail::PartWriteMode::StreamRewrite,
            "cell replacement handoff manifest should mirror staged stream rewrite mode");
        check(editor.edit_plan().full_calculation_on_load(),
            "cell replacement handoff should request full calculation on load");
        check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
            "cell replacement handoff should remove stale calcChain");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
        check(output_plan.full_calculation_on_load,
            "cell replacement output plan should expose full calculation request");
        check(has_note_containing(output_plan.notes, {"temporary file-backed package-entry chunk"}),
            "cell replacement output plan should expose file-backed chunk handoff note");
        check(has_note_containing(output_plan.notes,
                  {"PackageReader ZIP-entry chunk source", "source worksheet XML"}),
            "cell replacement output plan should expose direct source-entry chunk source");
        check(has_note_containing(output_plan.notes,
                  {"source package worksheet XML", "transformer chunk-source adapter"}),
            "cell replacement output plan should expose source chunk transformer input");
        check(has_note_containing(output_plan.notes,
                  {"dependency and dimension analysis", "transformer chunk-source adapter"}),
            "cell replacement output plan should expose chunked dependency/dimension analysis");
        check(has_note_containing(output_plan.notes,
                  {"relationship-id audit", "transformer chunk-source adapter"}),
            "cell replacement output plan should expose chunked relationship-id audit");
        check(has_note_containing(output_plan.notes,
                  {"root validation", "event-reader chunk-source validator"}),
            "cell replacement output plan should expose chunk-source root validation");
        check(has_note_containing(output_plan.notes, {"refreshed worksheet dimension"}),
            "cell replacement output plan should expose dimension refresh note");
        check(has_note_containing(output_plan.notes,
                  {"one prevalidated non-owning replacement lookup plan",
                      "dependency/dimension analysis pass",
                      "dimension-refreshed output pass",
                      "without reparsing replacement cell payloads",
                      "rebuilding selector lookup"}),
            "cell replacement output plan should expose replacement lookup plan reuse");
        check(has_note_containing(output_plan.notes,
                  {"explicit replacement payload chunks",
                      "rather than raw string fields",
                      "bounded single-cell XML limit"}),
            "cell replacement output plan should expose explicit payload-chunk boundary");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
            fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
            "cell replacement output plan should stream-rewrite worksheet chunks");
        check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
            fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
            "cell replacement output plan should local-rewrite workbook metadata");
        check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
            "cell replacement output plan should omit stale calcChain");

        editor.save_as(output);
    }

    check_no_new_package_editor_temp_files(temp_files_before,
        "cell replacement should clean PackageEditor-owned temporary XML files");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string expected_worksheet =
        R"(<worksheet><dimension ref="A1"/><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>patched</t></is></c></row></sheetData></worksheet>)";
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == expected_worksheet,
        "cell replacement handoff should write transformed worksheet XML");
    check(output_reader.find_entry("xl/calcChain.xml") == nullptr,
        "cell replacement handoff output should omit calcChain payload");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "cell replacement handoff should preserve unknown bytes");
    check_contains(output_reader.read_entry("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "cell replacement handoff output should request full calculation");
    check_not_contains(output_reader.read_entry("[Content_Types].xml"), "calcChain+xml",
        "cell replacement handoff content types should remove calcChain");
    const auto* output_worksheet = output_reader.part_index().find_part(worksheet_part);
    check(output_worksheet != nullptr
            && output_worksheet->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "output reader should ingest the rewritten worksheet as a normal source part");
    check(output_reader.part_index().find_part(workbook_part) != nullptr,
        "output reader should retain workbook part");
}

void test_package_editor_replaces_worksheet_cells_with_chunked_payload()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-cell-replacement-chunked-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-cell-replacement-chunked-output.xlsx");
    const std::vector<std::filesystem::path> temp_files_before =
        package_editor_temp_files();

    const std::array<std::string_view, 3> replacement_chunks {
        R"(<c r="A1" t="inlineStr">)",
        R"(<is><t>chunked payload</t></is>)",
        R"(</c>)",
    };
    const std::array replacements {
        chunked_worksheet_cell_replacement("A1", replacement_chunks),
    };

    {
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);

        editor.replace_worksheet_cells_by_name("Sheet1", replacements);
        const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
        check(has_note_containing(output_plan.notes,
                  {"replacement payload chunks",
                      "bounded single-cell XML limit",
                      "not streamed cell payload sources"}),
            "chunked cell replacement output plan should expose bounded payload chunk boundary");

        editor.save_as(output);
    }

    check_no_new_package_editor_temp_files(temp_files_before,
        "chunked cell replacement should clean PackageEditor-owned temporary XML files");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string expected_worksheet =
        R"(<worksheet><dimension ref="A1"/><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>chunked payload</t></is></c></row></sheetData></worksheet>)";
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == expected_worksheet,
        "chunked cell replacement should replay payload chunks into transformed worksheet XML");
    check(output_reader.find_entry("xl/calcChain.xml") == nullptr,
        "chunked cell replacement should omit stale calcChain payload");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "chunked cell replacement should preserve unknown bytes");
}

void test_package_editor_contextualizes_current_worksheet_source_read_failure_without_state_changes()
{
    const CalcSourcePackage source =
        write_calc_source_package(
            "fastxlsx-package-editor-cell-replacement-source-read-failure.xlsx");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_payload_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t initial_relationship_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const bool initial_full_calculation =
        editor.edit_plan().full_calculation_on_load();
    const std::vector<std::filesystem::path> temp_files_before =
        package_editor_temp_files();

    std::string corrupted_source_bytes = fastxlsx::test::read_file(source.path);
    corrupt_first_occurrence(corrupted_source_bytes, "SUM(B1:C1)");
    write_binary_file(source.path, corrupted_source_bytes);

    const std::array replacements {
        worksheet_cell_replacement("A1", R"(<c r="A1"><v>7</v></c>)"),
    };

    bool failed = false;
    try {
        editor.replace_worksheet_cells(worksheet_part, replacements);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(),
            "current worksheet input for worksheet cell replacement analysis",
            "source worksheet read failure should identify the analysis input boundary");
        check_contains(error.what(), "source worksheet entry 'xl/worksheets/sheet1.xml'",
            "source worksheet read failure should identify the current input source entry");
        check_contains(error.what(), "worksheet part '/xl/worksheets/sheet1.xml'",
            "source worksheet read failure should identify the worksheet part");
        check_contains(error.what(), "ZIP entry 'xl/worksheets/sheet1.xml'",
            "source worksheet read failure should identify the worksheet ZIP entry");
        check_contains(error.what(),
            std::string("after emitting 1 current-input chunk and ")
                + std::to_string(source.worksheet.size()) + " bytes",
            "source worksheet read failure should report emitted current-input progress");
        check_contains(error.what(), "current-input read attempt 2",
            "source worksheet read failure should report the failing read attempt");
        check_contains(error.what(),
            std::string("last chunk ") + std::to_string(source.worksheet.size()) + " bytes",
            "source worksheet read failure should report the last emitted chunk size");
        check_contains(error.what(), "CRC mismatch",
            "source worksheet read failure should preserve the underlying ZIP error");
        check_contains(error.what(), "ZIP entry 'xl/worksheets/sheet1.xml' CRC mismatch",
            "source worksheet read failure should identify the corrupt worksheet entry");
        check_contains(error.what(), "expected ",
            "source worksheet read failure should report expected CRC");
        check_contains(error.what(), "actual ",
            "source worksheet read failure should report actual CRC");
    }

    check(failed, "cell replacement should fail when source worksheet chunk read fails");
    check(editor.edit_plan().size() == initial_plan_size,
        "source worksheet read failure should not mutate edit-plan parts");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "source worksheet read failure should not append edit-plan notes");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == initial_payload_audit_count,
        "source worksheet read failure should not append payload audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_relationship_audit_count,
        "source worksheet read failure should not append relationship audits");
    check(editor.edit_plan().full_calculation_on_load() == initial_full_calculation,
        "source worksheet read failure should not change calc policy");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source worksheet read failure should leave worksheet manifest copy-original");
    check_no_new_package_editor_temp_files(temp_files_before,
        "source worksheet read failure should not leak PackageEditor temp files");

    const CalcSourcePackage planned_name_source =
        write_calc_source_package(
            "fastxlsx-package-editor-cell-replacement-planned-name-source-read-failure.xlsx");
    fastxlsx::detail::PackageEditor planned_name_editor =
        fastxlsx::detail::PackageEditor::open(planned_name_source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const std::string planned_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Renamed" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    planned_name_editor.replace_part(workbook_part, planned_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "ordinary workbook replacement before planned-name cell source-read failure");

    const std::size_t planned_name_plan_size =
        planned_name_editor.edit_plan().size();
    const std::size_t planned_name_note_count =
        planned_name_editor.edit_plan().notes().size();
    const std::size_t planned_name_package_entry_count =
        planned_name_editor.edit_plan().package_entries().size();
    const std::size_t planned_name_removed_package_entry_count =
        planned_name_editor.edit_plan().removed_package_entries().size();
    const std::size_t planned_name_payload_audit_count =
        planned_name_editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t planned_name_relationship_audit_count =
        planned_name_editor.edit_plan().worksheet_relationship_reference_audits().size();
    const bool planned_name_full_calculation =
        planned_name_editor.edit_plan().full_calculation_on_load();
    const fastxlsx::detail::CalcChainAction planned_name_calc_chain_action =
        planned_name_editor.edit_plan().calc_chain_action();
    const std::vector<std::filesystem::path> planned_name_temp_files_before =
        package_editor_temp_files();

    std::string planned_name_corrupted_source_bytes =
        fastxlsx::test::read_file(planned_name_source.path);
    corrupt_first_occurrence(planned_name_corrupted_source_bytes, "SUM(B1:C1)");
    write_binary_file(planned_name_source.path, planned_name_corrupted_source_bytes);

    failed = false;
    try {
        planned_name_editor.replace_worksheet_cells_by_name("Renamed", replacements);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(),
            "by-name worksheet cell replacement for sheet 'Renamed'",
            "planned-name cell source read failure should identify the planned sheet name");
        check_contains(error.what(),
            "resolved to worksheet part '/xl/worksheets/sheet1.xml'",
            "planned-name cell source read failure should show the resolved worksheet part");
        check_contains(error.what(),
            "current worksheet input for worksheet cell replacement analysis",
            "planned-name cell source read failure should keep the analysis input boundary");
        check_contains(error.what(), "source worksheet entry 'xl/worksheets/sheet1.xml'",
            "planned-name cell source read failure should identify the source worksheet entry");
        check_contains(error.what(), "ZIP entry 'xl/worksheets/sheet1.xml'",
            "planned-name cell source read failure should identify the ZIP entry");
        check_contains(error.what(),
            std::string("after emitting 1 current-input chunk and ")
                + std::to_string(planned_name_source.worksheet.size()) + " bytes",
            "planned-name cell source read failure should report emitted current-input progress");
        check_contains(error.what(), "current-input read attempt 2",
            "planned-name cell source read failure should report the failing read attempt");
        check_contains(error.what(),
            std::string("last chunk ") + std::to_string(planned_name_source.worksheet.size())
                + " bytes",
            "planned-name cell source read failure should report the last emitted chunk size");
        check_contains(error.what(), "CRC mismatch",
            "planned-name cell source read failure should preserve the underlying ZIP error");
        check_contains(error.what(), "expected ",
            "planned-name cell source read failure should report expected CRC");
        check_contains(error.what(), "actual ",
            "planned-name cell source read failure should report actual CRC");
        check_not_contains(error.what(), "replacement payload",
            "planned-name cell source read failure should not be mislabeled as replacement payload input");
    }

    check(failed,
        "planned-name by-name cell replacement should fail when source worksheet read fails");
    check(planned_name_editor.edit_plan().size() == planned_name_plan_size,
        "planned-name cell source read failure should preserve queued edit-plan size");
    check(planned_name_editor.edit_plan().notes().size() == planned_name_note_count,
        "planned-name cell source read failure should not append notes");
    check(planned_name_editor.edit_plan().package_entries().size()
            == planned_name_package_entry_count,
        "planned-name cell source read failure should not add package-entry audits");
    check(planned_name_editor.edit_plan().removed_package_entries().size()
            == planned_name_removed_package_entry_count,
        "planned-name cell source read failure should not add removed package-entry audits");
    check(planned_name_editor.edit_plan().worksheet_payload_dependency_audits().size()
            == planned_name_payload_audit_count,
        "planned-name cell source read failure should not append payload audits");
    check(planned_name_editor.edit_plan().worksheet_relationship_reference_audits().size()
            == planned_name_relationship_audit_count,
        "planned-name cell source read failure should not append relationship audits");
    check(planned_name_editor.edit_plan().full_calculation_on_load()
            == planned_name_full_calculation,
        "planned-name cell source read failure should not change calc policy");
    check(planned_name_editor.edit_plan().calc_chain_action()
            == planned_name_calc_chain_action,
        "planned-name cell source read failure should not change calcChain policy");
    check_manifest_write_mode(planned_name_editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "planned-name cell source read failure should keep workbook local-DOM-rewrite");
    check_manifest_write_mode(planned_name_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "planned-name cell source read failure should leave worksheet manifest copy-original");
    check_manifest_write_mode(planned_name_editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "planned-name cell source read failure should leave calcChain manifest copy-original");
    check_no_new_package_editor_temp_files(planned_name_temp_files_before,
        "planned-name cell source read failure should not leak PackageEditor temp files");
}

void test_package_editor_contextualizes_missing_current_worksheet_entry_without_state_changes()
{
    const SourcePackage source =
        write_missing_worksheet_entry_source_package(
            "fastxlsx-package-editor-cell-replacement-missing-source-entry-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-cell-replacement-missing-source-entry-output.xlsx");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t initial_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const std::size_t initial_payload_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t initial_relationship_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const bool initial_full_calculation =
        editor.edit_plan().full_calculation_on_load();
    const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
        editor.edit_plan().calc_chain_action();
    const std::vector<std::filesystem::path> temp_files_before =
        package_editor_temp_files();
    const std::array replacements {
        worksheet_cell_replacement("A1", R"(<c r="A1"><v>7</v></c>)"),
    };

    bool failed = false;
    try {
        editor.replace_worksheet_cells(worksheet_part, replacements);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "worksheet cell replacement",
            "missing current worksheet entry failure should identify the operation");
        check_contains(error.what(), "worksheet part '/xl/worksheets/sheet1.xml'",
            "missing current worksheet entry failure should identify the worksheet part");
        check_contains(error.what(), "ZIP entry 'xl/worksheets/sheet1.xml'",
            "missing current worksheet entry failure should identify the worksheet ZIP entry");
        check_not_contains(error.what(), "sheetData replacement XML",
            "missing current worksheet entry failure should not be mislabeled as replacement payload input");
    }

    check(failed,
        "cell replacement should fail when the source worksheet entry is absent");
    check(editor.edit_plan().size() == initial_plan_size,
        "missing current worksheet entry failure should not mutate edit-plan parts");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "missing current worksheet entry failure should not append edit-plan notes");
    check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
        "missing current worksheet entry failure should not add package-entry audits");
    check(editor.edit_plan().removed_package_entries().size()
            == initial_removed_package_entry_count,
        "missing current worksheet entry failure should not add removed package-entry audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == initial_payload_audit_count,
        "missing current worksheet entry failure should not append payload audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_relationship_audit_count,
        "missing current worksheet entry failure should not append relationship audits");
    check(editor.edit_plan().removed_parts().empty(),
        "missing current worksheet entry failure should not record removed parts");
    check(editor.edit_plan().full_calculation_on_load() == initial_full_calculation,
        "missing current worksheet entry failure should not change calc policy");
    check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
        "missing current worksheet entry failure should not change calcChain policy");
    const auto* missing_entry_manifest_part = editor.manifest().find_part(worksheet_part);
    check(missing_entry_manifest_part == nullptr
            || missing_entry_manifest_part->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "missing current worksheet entry failure should not change worksheet manifest state");
    check_no_new_package_editor_temp_files(temp_files_before,
        "missing current worksheet entry failure should not leak PackageEditor temp files");

    editor.save_as(output);
    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.find_entry("xl/worksheets/sheet1.xml") == nullptr,
        "missing current worksheet entry failure output should not invent worksheet XML");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "missing current worksheet entry failure output should preserve workbook bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "missing current worksheet entry failure output should preserve unknown bytes");
}

void test_package_editor_rejects_malformed_current_worksheet_events_without_state_changes()
{
    struct MalformedWorksheetCase {
        std::string_view name;
        std::string_view worksheet_xml;
        std::string_view expected_error;
    };

    const std::array cases {
        MalformedWorksheetCase {
            "mismatched-value-boundary",
            R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</f></c></row></sheetData></worksheet>)",
            "mismatched cell value boundary",
        },
        MalformedWorksheetCase {
            "nested-cell",
            R"(<worksheet><sheetData><row r="1"><c r="A1"><c r="B1"/></c></row></sheetData></worksheet>)",
            "invalid cell boundary",
        },
    };

    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const std::array replacements {
        worksheet_cell_replacement("A1", R"(<c r="A1"><v>7</v></c>)"),
    };

    for (const MalformedWorksheetCase& test_case : cases) {
        CalcSourcePackage source =
            write_calc_source_package("fastxlsx-package-editor-cell-replacement-"
                + std::string(test_case.name) + "-source.xlsx");
        source.worksheet = std::string(test_case.worksheet_xml);
        rewrite_calc_source_package(source);

        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);
        const std::size_t initial_plan_size = editor.edit_plan().size();
        const std::size_t initial_note_count = editor.edit_plan().notes().size();
        const std::size_t initial_package_entry_count =
            editor.edit_plan().package_entries().size();
        const std::size_t initial_removed_package_entry_count =
            editor.edit_plan().removed_package_entries().size();
        const std::size_t initial_payload_audit_count =
            editor.edit_plan().worksheet_payload_dependency_audits().size();
        const std::size_t initial_relationship_audit_count =
            editor.edit_plan().worksheet_relationship_reference_audits().size();
        const bool initial_full_calculation =
            editor.edit_plan().full_calculation_on_load();
        const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
            editor.edit_plan().calc_chain_action();
        const std::vector<std::filesystem::path> temp_files_before =
            package_editor_temp_files();

        bool failed = false;
        try {
            editor.replace_worksheet_cells(worksheet_part, replacements);
        } catch (const std::exception& error) {
            failed = true;
            check_contains(error.what(),
                "current worksheet input for worksheet cell replacement analysis",
                "malformed source worksheet should identify the analysis input boundary");
            check_contains(error.what(), test_case.expected_error,
                "malformed source worksheet should preserve event-reader diagnostics");
        }

        check(failed,
            "PackageEditor should reject malformed source worksheet events");
        check(editor.edit_plan().size() == initial_plan_size,
            "malformed source worksheet failure should not mutate edit-plan parts");
        check(editor.edit_plan().notes().size() == initial_note_count,
            "malformed source worksheet failure should not append edit-plan notes");
        check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
            "malformed source worksheet failure should not add package-entry audits");
        check(editor.edit_plan().removed_package_entries().size()
                == initial_removed_package_entry_count,
            "malformed source worksheet failure should not add removed package-entry audits");
        check(editor.edit_plan().worksheet_payload_dependency_audits().size()
                == initial_payload_audit_count,
            "malformed source worksheet failure should not append payload audits");
        check(editor.edit_plan().worksheet_relationship_reference_audits().size()
                == initial_relationship_audit_count,
            "malformed source worksheet failure should not append relationship audits");
        check(editor.edit_plan().removed_parts().empty(),
            "malformed source worksheet failure should not record removed parts");
        check(editor.edit_plan().full_calculation_on_load() == initial_full_calculation,
            "malformed source worksheet failure should not change calc policy");
        check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
            "malformed source worksheet failure should not change calcChain policy");
        check(editor.manifest().find_part(calc_chain_part) != nullptr,
            "malformed source worksheet failure should keep calcChain in the manifest");
        check_manifest_write_mode(editor, worksheet_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "malformed source worksheet failure should leave worksheet manifest copy-original");
        check_no_new_package_editor_temp_files(temp_files_before,
            "malformed source worksheet failure should not leak PackageEditor temp files");
    }
}

void test_package_editor_contextualizes_sheet_data_current_worksheet_source_read_failure_without_state_changes()
{
    const CalcSourcePackage source =
        write_calc_source_package(
            "fastxlsx-package-editor-sheetdata-source-read-failure.xlsx");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t initial_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const std::size_t initial_payload_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t initial_relationship_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const bool initial_full_calculation =
        editor.edit_plan().full_calculation_on_load();
    const std::vector<std::filesystem::path> temp_files_before =
        package_editor_temp_files();

    std::string corrupted_source_bytes = fastxlsx::test::read_file(source.path);
    corrupt_first_occurrence(corrupted_source_bytes, "SUM(B1:C1)");
    write_binary_file(source.path, corrupted_source_bytes);

    bool failed = false;
    try {
        replace_worksheet_sheet_data_from_single_chunk_source(editor, worksheet_part,
            R"(<sheetData><row r="1"><c r="A1"><v>42</v></c></row></sheetData>)");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(),
            "current worksheet input for worksheet sheetData replacement output",
            "sheetData source read failure should identify the output input boundary");
        check_contains(error.what(), "source worksheet entry 'xl/worksheets/sheet1.xml'",
            "sheetData source read failure should identify the current input source entry");
        check_contains(error.what(), "worksheet part '/xl/worksheets/sheet1.xml'",
            "sheetData source read failure should identify the worksheet part");
        check_contains(error.what(), "ZIP entry 'xl/worksheets/sheet1.xml'",
            "sheetData source read failure should identify the worksheet ZIP entry");
        check_contains(error.what(),
            std::string("after emitting 1 current-input chunk and ")
                + std::to_string(source.worksheet.size()) + " bytes",
            "sheetData source read failure should report emitted current-input progress");
        check_contains(error.what(), "current-input read attempt 2",
            "sheetData source read failure should report the failing read attempt");
        check_contains(error.what(),
            std::string("last chunk ") + std::to_string(source.worksheet.size()) + " bytes",
            "sheetData source read failure should report the last emitted chunk size");
        check_contains(error.what(), "CRC mismatch",
            "sheetData source read failure should preserve the underlying ZIP error");
        check_contains(error.what(), "ZIP entry 'xl/worksheets/sheet1.xml' CRC mismatch",
            "sheetData source read failure should identify the corrupt worksheet entry");
        check_contains(error.what(), "expected ",
            "sheetData source read failure should report expected CRC");
        check_contains(error.what(), "actual ",
            "sheetData source read failure should report actual CRC");
        check_not_contains(error.what(), "sheetData replacement XML",
            "sheetData source read failure should not be mislabeled as replacement payload input");
    }

    check(failed, "sheetData replacement should fail when source worksheet chunk read fails");
    check(editor.edit_plan().size() == initial_plan_size,
        "sheetData source read failure should not mutate edit-plan parts");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "sheetData source read failure should not append edit-plan notes");
    check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
        "sheetData source read failure should not add package-entry audits");
    check(editor.edit_plan().removed_package_entries().size()
            == initial_removed_package_entry_count,
        "sheetData source read failure should not add removed package-entry audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == initial_payload_audit_count,
        "sheetData source read failure should not append payload audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_relationship_audit_count,
        "sheetData source read failure should not append relationship audits");
    check(editor.edit_plan().full_calculation_on_load() == initial_full_calculation,
        "sheetData source read failure should not change calc policy");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sheetData source read failure should leave worksheet manifest copy-original");
    check_no_new_package_editor_temp_files(temp_files_before,
        "sheetData source read failure should not leak PackageEditor temp files");

    const CalcSourcePackage by_name_source =
        write_calc_source_package(
            "fastxlsx-package-editor-sheetdata-by-name-source-read-failure.xlsx");
    fastxlsx::detail::PackageEditor by_name_editor =
        fastxlsx::detail::PackageEditor::open(by_name_source.path);
    const std::size_t by_name_initial_plan_size = by_name_editor.edit_plan().size();
    const std::size_t by_name_initial_note_count = by_name_editor.edit_plan().notes().size();
    const std::size_t by_name_initial_package_entry_count =
        by_name_editor.edit_plan().package_entries().size();
    const std::size_t by_name_initial_removed_package_entry_count =
        by_name_editor.edit_plan().removed_package_entries().size();
    const std::size_t by_name_initial_payload_audit_count =
        by_name_editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t by_name_initial_relationship_audit_count =
        by_name_editor.edit_plan().worksheet_relationship_reference_audits().size();
    const bool by_name_initial_full_calculation =
        by_name_editor.edit_plan().full_calculation_on_load();
    const std::vector<std::filesystem::path> by_name_temp_files_before =
        package_editor_temp_files();

    std::string by_name_corrupted_source_bytes =
        fastxlsx::test::read_file(by_name_source.path);
    corrupt_first_occurrence(by_name_corrupted_source_bytes, "SUM(B1:C1)");
    write_binary_file(by_name_source.path, by_name_corrupted_source_bytes);

    failed = false;
    try {
        by_name_editor.replace_worksheet_sheet_data_from_chunk_source_by_name(
            "Sheet1",
            make_test_chunk_source({
                R"(<sheetData><row r="1"><c r="A1"><v>42</v></c></row></sheetData>)",
            }));
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(),
            "current worksheet input for worksheet sheetData replacement output",
            "by-name sheetData source read failure should identify the output input boundary");
        check_contains(error.what(), "source worksheet entry 'xl/worksheets/sheet1.xml'",
            "by-name sheetData source read failure should identify the current input source entry");
        check_contains(error.what(), "worksheet part '/xl/worksheets/sheet1.xml'",
            "by-name sheetData source read failure should identify the worksheet part");
        check_contains(error.what(), "ZIP entry 'xl/worksheets/sheet1.xml'",
            "by-name sheetData source read failure should identify the worksheet ZIP entry");
        check_contains(error.what(),
            std::string("after emitting 1 current-input chunk and ")
                + std::to_string(by_name_source.worksheet.size()) + " bytes",
            "by-name sheetData source read failure should report emitted current-input progress");
        check_contains(error.what(), "current-input read attempt 2",
            "by-name sheetData source read failure should report the failing read attempt");
        check_contains(error.what(),
            std::string("last chunk ") + std::to_string(by_name_source.worksheet.size())
                + " bytes",
            "by-name sheetData source read failure should report the last emitted chunk size");
        check_contains(error.what(), "CRC mismatch",
            "by-name sheetData source read failure should preserve the underlying ZIP error");
        check_contains(error.what(), "ZIP entry 'xl/worksheets/sheet1.xml' CRC mismatch",
            "by-name sheetData source read failure should identify the corrupt worksheet entry");
        check_contains(error.what(), "expected ",
            "by-name sheetData source read failure should report expected CRC");
        check_contains(error.what(), "actual ",
            "by-name sheetData source read failure should report actual CRC");
        check_not_contains(error.what(), "sheetData replacement XML",
            "by-name sheetData source read failure should not be mislabeled as replacement payload input");
    }

    check(failed,
        "by-name sheetData replacement should fail when source worksheet chunk read fails");
    check(by_name_editor.edit_plan().size() == by_name_initial_plan_size,
        "by-name sheetData source read failure should not mutate edit-plan parts");
    check(by_name_editor.edit_plan().notes().size() == by_name_initial_note_count,
        "by-name sheetData source read failure should not append edit-plan notes");
    check(by_name_editor.edit_plan().package_entries().size()
            == by_name_initial_package_entry_count,
        "by-name sheetData source read failure should not add package-entry audits");
    check(by_name_editor.edit_plan().removed_package_entries().size()
            == by_name_initial_removed_package_entry_count,
        "by-name sheetData source read failure should not add removed package-entry audits");
    check(by_name_editor.edit_plan().worksheet_payload_dependency_audits().size()
            == by_name_initial_payload_audit_count,
        "by-name sheetData source read failure should not append payload audits");
    check(by_name_editor.edit_plan().worksheet_relationship_reference_audits().size()
            == by_name_initial_relationship_audit_count,
        "by-name sheetData source read failure should not append relationship audits");
    check(by_name_editor.edit_plan().full_calculation_on_load()
            == by_name_initial_full_calculation,
        "by-name sheetData source read failure should not change calc policy");
    check_manifest_write_mode(by_name_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "by-name sheetData source read failure should leave worksheet manifest copy-original");
    check_no_new_package_editor_temp_files(by_name_temp_files_before,
        "by-name sheetData source read failure should not leak PackageEditor temp files");

    const CalcSourcePackage planned_name_source =
        write_calc_source_package(
            "fastxlsx-package-editor-sheetdata-planned-name-source-read-failure.xlsx");
    fastxlsx::detail::PackageEditor planned_name_editor =
        fastxlsx::detail::PackageEditor::open(planned_name_source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const std::string planned_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Renamed" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    planned_name_editor.replace_part(workbook_part, planned_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "ordinary workbook replacement before planned-name source-read failure");

    const std::size_t planned_name_plan_size =
        planned_name_editor.edit_plan().size();
    const std::size_t planned_name_note_count =
        planned_name_editor.edit_plan().notes().size();
    const std::size_t planned_name_package_entry_count =
        planned_name_editor.edit_plan().package_entries().size();
    const std::size_t planned_name_removed_package_entry_count =
        planned_name_editor.edit_plan().removed_package_entries().size();
    const std::size_t planned_name_payload_audit_count =
        planned_name_editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t planned_name_relationship_audit_count =
        planned_name_editor.edit_plan().worksheet_relationship_reference_audits().size();
    const bool planned_name_full_calculation =
        planned_name_editor.edit_plan().full_calculation_on_load();
    const fastxlsx::detail::CalcChainAction planned_name_calc_chain_action =
        planned_name_editor.edit_plan().calc_chain_action();
    const std::vector<std::filesystem::path> planned_name_temp_files_before =
        package_editor_temp_files();

    std::string planned_name_corrupted_source_bytes =
        fastxlsx::test::read_file(planned_name_source.path);
    corrupt_first_occurrence(planned_name_corrupted_source_bytes, "SUM(B1:C1)");
    write_binary_file(planned_name_source.path, planned_name_corrupted_source_bytes);

    failed = false;
    try {
        planned_name_editor.replace_worksheet_sheet_data_from_chunk_source_by_name(
            "Renamed",
            make_test_chunk_source({
                R"(<sheetData><row r="1"><c r="A1"><v>84</v></c></row></sheetData>)",
            }));
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(),
            "by-name sheetData replacement for sheet 'Renamed'",
            "planned-name sheetData source read failure should identify the planned sheet name");
        check_contains(error.what(),
            "resolved to worksheet part '/xl/worksheets/sheet1.xml'",
            "planned-name sheetData source read failure should show the resolved worksheet part");
        check_contains(error.what(),
            "current worksheet input for worksheet sheetData replacement output",
            "planned-name sheetData source read failure should keep the output input boundary");
        check_contains(error.what(), "source worksheet entry 'xl/worksheets/sheet1.xml'",
            "planned-name sheetData source read failure should identify the source worksheet entry");
        check_contains(error.what(), "ZIP entry 'xl/worksheets/sheet1.xml'",
            "planned-name sheetData source read failure should identify the ZIP entry");
        check_contains(error.what(),
            std::string("after emitting 1 current-input chunk and ")
                + std::to_string(planned_name_source.worksheet.size()) + " bytes",
            "planned-name sheetData source read failure should report emitted current-input progress");
        check_contains(error.what(), "current-input read attempt 2",
            "planned-name sheetData source read failure should report the failing read attempt");
        check_contains(error.what(),
            std::string("last chunk ") + std::to_string(planned_name_source.worksheet.size())
                + " bytes",
            "planned-name sheetData source read failure should report the last emitted chunk size");
        check_contains(error.what(), "CRC mismatch",
            "planned-name sheetData source read failure should preserve the underlying ZIP error");
        check_contains(error.what(), "expected ",
            "planned-name sheetData source read failure should report expected CRC");
        check_contains(error.what(), "actual ",
            "planned-name sheetData source read failure should report actual CRC");
        check_not_contains(error.what(), "sheetData replacement XML",
            "planned-name sheetData source read failure should not be mislabeled as replacement payload input");
    }

    check(failed,
        "planned-name by-name sheetData replacement should fail when source worksheet read fails");
    check(planned_name_editor.edit_plan().size() == planned_name_plan_size,
        "planned-name sheetData source read failure should preserve queued edit-plan size");
    check(planned_name_editor.edit_plan().notes().size() == planned_name_note_count,
        "planned-name sheetData source read failure should not append notes");
    check(planned_name_editor.edit_plan().package_entries().size()
            == planned_name_package_entry_count,
        "planned-name sheetData source read failure should not add package-entry audits");
    check(planned_name_editor.edit_plan().removed_package_entries().size()
            == planned_name_removed_package_entry_count,
        "planned-name sheetData source read failure should not add removed package-entry audits");
    check(planned_name_editor.edit_plan().worksheet_payload_dependency_audits().size()
            == planned_name_payload_audit_count,
        "planned-name sheetData source read failure should not append payload audits");
    check(planned_name_editor.edit_plan().worksheet_relationship_reference_audits().size()
            == planned_name_relationship_audit_count,
        "planned-name sheetData source read failure should not append relationship audits");
    check(planned_name_editor.edit_plan().full_calculation_on_load()
            == planned_name_full_calculation,
        "planned-name sheetData source read failure should not change calc policy");
    check(planned_name_editor.edit_plan().calc_chain_action()
            == planned_name_calc_chain_action,
        "planned-name sheetData source read failure should not change calcChain policy");
    check_manifest_write_mode(planned_name_editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "planned-name sheetData source read failure should keep workbook local-DOM-rewrite");
    check_manifest_write_mode(planned_name_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "planned-name sheetData source read failure should leave worksheet manifest copy-original");
    check_manifest_write_mode(planned_name_editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "planned-name sheetData source read failure should leave calcChain manifest copy-original");
    check_no_new_package_editor_temp_files(planned_name_temp_files_before,
        "planned-name sheetData source read failure should not leak PackageEditor temp files");
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
        check(has_note_containing(output_plan.notes, {"temporary file-backed package-entry chunk"}),
            "cell replacement linked output plan should expose file-backed chunk note");
        check(has_note_containing(output_plan.notes,
                  {"PackageReader ZIP-entry chunk source", "source worksheet XML"}),
            "cell replacement linked output plan should expose direct source-entry chunk source");
        check(has_note_containing(output_plan.notes,
                  {"source package worksheet XML", "transformer chunk-source adapter"}),
            "cell replacement linked output plan should expose source chunk transformer input");
        check(has_note_containing(output_plan.notes,
                  {"dependency and dimension analysis", "transformer chunk-source adapter"}),
            "cell replacement linked output plan should expose chunked dependency/dimension analysis");
        check(has_note_containing(output_plan.notes,
                  {"relationship-id audit", "transformer chunk-source adapter"}),
            "cell replacement linked output plan should expose chunked relationship-id audit");
        check(has_note_containing(output_plan.notes,
                  {"root validation", "event-reader chunk-source validator"}),
            "cell replacement linked output plan should expose chunk-source root validation");
        check(has_note_containing(output_plan.notes, {"refreshed worksheet dimension"}),
            "cell replacement linked output plan should expose dimension refresh note");
        check(has_note_containing(output_plan.notes,
                  {"one prevalidated non-owning replacement lookup plan",
                      "dependency/dimension analysis pass",
                      "dimension-refreshed output pass",
                      "without reparsing replacement cell payloads",
                      "rebuilding selector lookup"}),
            "cell replacement linked output plan should expose replacement lookup plan reuse");
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

void test_package_editor_worksheet_cell_replacement_refreshes_stale_dimension()
{
    CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-cell-replacement-dimension-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-cell-replacement-dimension-output.xlsx");
    source.worksheet =
        R"(<worksheet><dimension ref="A1"/><sheetData>)"
        R"(<row r="1"><c r="A1"><v>1</v></c></row>)"
        R"(<row r="3"><c r="C3"><v>3</v></c></row>)"
        R"(</sheetData></worksheet>)";
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
    const std::array replacements {
        worksheet_cell_replacement("A1", R"(<c r="A1"><v>9</v></c>)"),
    };

    editor.replace_worksheet_cells_by_name("Sheet1", replacements);
    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(has_note_containing(output_plan.notes, {"refreshed worksheet dimension"}),
        "dimension refresh handoff should record a review note");
    check_output_entry_staged_replacement_chunks(output_plan.entries,
        "xl/worksheets/sheet1.xml", true,
        "cell replacement output plan should expose dimension-refreshed worksheet staged chunks");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string worksheet_xml = output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:C3"/>)",
        "cell replacement handoff should refresh stale worksheet dimension");
    check_not_contains(worksheet_xml, R"(<dimension ref="A1"/>)",
        "cell replacement handoff should replace stale worksheet dimension");
    check_contains(worksheet_xml, R"(<c r="C3"><v>3</v></c>)",
        "dimension refresh should preserve non-target cell XML");
}

void test_package_editor_worksheet_cell_replacement_uses_planned_worksheet_input()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-cell-replacement-planned-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-cell-replacement-planned-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::string planned_worksheet =
        R"(<worksheet><dimension ref="A1"/><sheetData><row r="1"><c r="A1"><v>11</v></c></row></sheetData></worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, planned_worksheet);

    const std::array replacements {
        worksheet_cell_replacement("A1", R"(<c r="A1"><v>12</v></c>)"),
    };
    editor.replace_worksheet_cells_by_name("Sheet1", replacements);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "planned-input cell replacement should keep worksheet in the edit plan");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "planned-input cell replacement should stream-rewrite worksheet output");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "planned-input cell replacement should still remove stale calcChain");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(has_note_containing(output_plan.notes,
              {"pull-based chunk source", "file-backed staged chunk",
                  "follow-up planned-input transforms"}),
        "planned-input cell replacement should report file-backed planned worksheet handoff");
    check(has_note_containing(output_plan.notes,
        {"writes the staged worksheet chunk in one caller chunk-source pass",
            "without reopening that staged chunk"}),
        "planned-input cell replacement should report fused caller chunk-source staging/audit handoff");
    check(has_note_containing(output_plan.notes,
              {"planned worksheet staged chunks", "without materializing"}),
        "planned-input cell replacement should report non-materialized staged input");
    check(has_note_containing(output_plan.notes,
              {"planned worksheet staged chunks", "transformer chunk-source adapter"}),
        "planned-input cell replacement should expose staged chunk transformer input");
    check(has_note_containing(output_plan.notes,
              {"dependency and dimension analysis", "transformer chunk-source adapter"}),
        "planned-input cell replacement should expose chunked dependency/dimension analysis");
    check(has_note_containing(output_plan.notes,
              {"relationship-id audit", "transformer chunk-source adapter"}),
        "planned-input cell replacement should expose chunked relationship-id audit");
    check(has_note_containing(output_plan.notes,
              {"root validation", "event-reader chunk-source validator"}),
        "planned-input cell replacement should expose chunked root validation");
    check(!has_note_containing(output_plan.notes,
              {"PackageReader ZIP-entry chunk source", "source worksheet XML"}),
        "planned-input cell replacement should not claim source-entry extraction");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "planned-input cell replacement output plan should stream-rewrite worksheet chunks");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string worksheet_xml = output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<c r="A1"><v>12</v></c>)",
        "planned-input cell replacement output should use the replacement cell");
    check_not_contains(worksheet_xml, R"(<v>11</v>)",
        "planned-input cell replacement output should consume the planned source cell");
    check(output_reader.find_entry("xl/calcChain.xml") == nullptr,
        "planned-input cell replacement output should omit stale calcChain");
}

void test_package_editor_ordinary_worksheet_replace_part_rejects_without_state_change()
{
    const CalcSourcePackage source =
        write_calc_source_package(
            "fastxlsx-package-editor-cell-replacement-ordinary-staged-source.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");

    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t initial_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t initial_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();

    const std::string ordinary_worksheet =
        R"(<worksheet><dimension ref="A1"/><sheetData><row r="1"><c r="A1"><v>ordinary</v></c></row><row r="2"><c r="B2"><v>tail</v></c></row></sheetData></worksheet>)";
    bool failed = false;
    try {
        editor.replace_part(worksheet_part,
            ordinary_worksheet,
            fastxlsx::detail::PartWriteMode::StreamRewrite,
            "ordinary worksheet replacement should be rejected");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "ordinary replace_part cannot target worksheet parts",
            "ordinary worksheet replace_part should fail at the worksheet guardrail");
    }
    check(failed, "ordinary worksheet replace_part should fail");

    check(editor.edit_plan().size() == initial_plan_size,
        "ordinary worksheet replace_part should not change edit-plan entries");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "ordinary worksheet replace_part should not add audit notes");
    check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
        "ordinary worksheet replace_part should not add package-entry audit");
    check(editor.edit_plan().removed_parts().size() == initial_removed_part_count,
        "ordinary worksheet replace_part should not remove parts");
    check(editor.edit_plan().removed_package_entries().size()
            == initial_removed_package_entry_count,
        "ordinary worksheet replace_part should not omit package entries");
    check(!editor.edit_plan().full_calculation_on_load(),
        "ordinary worksheet replace_part should not request recalculation");
    check(editor.edit_plan().calc_chain_action()
            == fastxlsx::detail::CalcChainAction::Preserve,
        "ordinary worksheet replace_part should not change calcChain policy");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "ordinary worksheet replace_part should keep worksheet manifest copy-original");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "ordinary worksheet replace_part should keep workbook manifest copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan =
        editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "ordinary worksheet replace_part output plan should not request recalculation");
    check(output_plan.notes.size() == initial_note_count,
        "ordinary worksheet replace_part output plan should not add notes");
    check_output_entry_plan(output_plan.entries, worksheet_part.zip_path(),
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "ordinary worksheet replace_part should leave planned output copy-original");
}

void test_package_editor_empty_ordinary_worksheet_replace_part_fails_without_state_change()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-empty-ordinary-worksheet-source.xlsx");
    const std::vector<std::filesystem::path> temp_files_before =
        package_editor_temp_files();

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");

    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t initial_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t initial_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();

    bool failed = false;
    try {
        editor.replace_part(worksheet_part, {},
            fastxlsx::detail::PartWriteMode::StreamRewrite,
            "empty ordinary worksheet replacement");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "ordinary replace_part cannot target worksheet parts",
            "empty ordinary worksheet replace_part should fail at the worksheet guardrail");
    }
    check(failed, "empty ordinary worksheet replace_part should fail");

    check(editor.edit_plan().size() == initial_plan_size,
        "empty ordinary worksheet replace_part should not change edit-plan entries");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "empty ordinary worksheet replace_part should not add audit notes");
    check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
        "empty ordinary worksheet replace_part should not add package-entry audit");
    check(editor.edit_plan().removed_parts().size() == initial_removed_part_count,
        "empty ordinary worksheet replace_part should not remove parts");
    check(editor.edit_plan().removed_package_entries().size()
            == initial_removed_package_entry_count,
        "empty ordinary worksheet replace_part should not omit package entries");
    check(!editor.edit_plan().full_calculation_on_load(),
        "empty ordinary worksheet replace_part should not request recalculation");
    check(editor.edit_plan().calc_chain_action()
            == fastxlsx::detail::CalcChainAction::Preserve,
        "empty ordinary worksheet replace_part should not change calcChain policy");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "empty ordinary worksheet replace_part should keep worksheet manifest copy-original");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "empty ordinary worksheet replace_part should keep workbook manifest copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan =
        editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "empty ordinary worksheet replace_part output plan should not request recalculation");
    check(output_plan.notes.size() == initial_note_count,
        "empty ordinary worksheet replace_part output plan should not add notes");
    check(output_plan.removed_parts.size() == initial_removed_part_count,
        "empty ordinary worksheet replace_part output plan should not remove parts");
    check(output_plan.removed_package_entries.size()
            == initial_removed_package_entry_count,
        "empty ordinary worksheet replace_part output plan should not omit entries");
    check_output_plan_preserves_source_copy_original(editor, output_plan,
        "empty ordinary worksheet replace_part should leave planned output copy-original");
    check_no_new_package_editor_temp_files(temp_files_before,
        "empty ordinary worksheet replace_part should clean failed staged temp file");
}

void test_package_editor_worksheet_cell_replacement_uses_sheet_data_staged_output()
{
    const CalcSourcePackage source =
        write_calc_source_package(
            "fastxlsx-package-editor-cell-replacement-sheetdata-staged-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-cell-replacement-sheetdata-staged-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="1"><c r="A1"><v>sheetdata-staged</v></c></row><row r="3"><c r="B3"><v>tail</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_from_single_chunk_source(editor, worksheet_part, replacement_sheet_data);
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "sheetData staged-output fixture should first record a local-DOM worksheet rewrite");
    check(has_note_containing(editor.edit_plan().notes(),
              {"bounded local worksheet XML rewrite", "file-backed staged chunk",
                  "follow-up planned-input transforms"}),
        "sheetData staged-output fixture should expose file-backed staged follow-up input");

    const std::array replacements {
        worksheet_cell_replacement(
            "A1", R"(<c r="A1"><v>patched-after-sheetdata</v></c>)"),
    };
    editor.replace_worksheet_cells(worksheet_part, replacements);

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(has_note_containing(output_plan.notes,
              {"planned worksheet staged chunks", "without materializing"}),
        "sheetData follow-up cell replacement should consume planned staged chunks without materializing");
    check(has_note_containing(output_plan.notes,
              {"planned worksheet staged chunks", "transformer chunk-source adapter"}),
        "sheetData follow-up cell replacement should expose staged chunk transformer input");
    check(has_note_containing(output_plan.notes,
              {"planned worksheet staged chunks", "event-reader chunk-source validator"}),
        "sheetData follow-up cell replacement should expose staged chunk root validation");
    check_output_entry_plan(output_plan.entries, worksheet_part.zip_path(),
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "sheetData follow-up cell replacement should stream-rewrite worksheet chunks");
    check_output_entry_plan(output_plan.entries, workbook_part.zip_path(),
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "sheetData follow-up cell replacement should keep workbook metadata rewrite");
    check_output_entry_plan(output_plan.entries, calc_chain_part.zip_path(),
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "sheetData follow-up cell replacement should keep stale calcChain omitted");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string worksheet_xml = output_reader.read_entry(worksheet_part.zip_path());
    check_contains(worksheet_xml, R"(<c r="A1"><v>patched-after-sheetdata</v></c>)",
        "sheetData follow-up cell replacement output should include patched cell");
    check_not_contains(worksheet_xml, "sheetdata-staged",
        "sheetData follow-up cell replacement output should consume old staged target cell");
    check_contains(worksheet_xml, R"(<c r="B3"><v>tail</v></c>)",
        "sheetData follow-up cell replacement output should preserve non-target staged cell");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B3"/>)",
        "sheetData follow-up cell replacement should refresh dimension from staged chunks");
    check(output_reader.find_entry(calc_chain_part.zip_path()) == nullptr,
        "sheetData follow-up cell replacement output should omit stale calcChain payload");
    check_contains(output_reader.read_entry(workbook_part.zip_path()), R"(fullCalcOnLoad="1")",
        "sheetData follow-up cell replacement output should keep fullCalcOnLoad");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "sheetData follow-up cell replacement output should preserve unknown bytes");
}

void test_package_editor_streams_planned_staged_chunks_for_worksheet_cell_replacement()
{
    const CalcSourcePackage source =
        write_calc_source_package(
            "fastxlsx-package-editor-cell-replacement-planned-chunks-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-cell-replacement-planned-chunks-output.xlsx");
    const std::filesystem::path body_path =
        output_path("fastxlsx-package-editor-cell-replacement-planned-chunks-body.xml");

    const std::string worksheet_prefix =
        R"(<worksheet><dimension ref="A1"/><sheetData><row r="1"><c r="A1"><v>old-staged</v></c></row>)";
    std::string worksheet_body;
    std::uint32_t last_row = 1;
    const std::string worksheet_suffix = R"(</sheetData></worksheet>)";
    while (worksheet_prefix.size() + worksheet_body.size() + worksheet_suffix.size()
        <= fastxlsx::detail::package_editor_cell_replacement_event_window_byte_limit
            + 4096U) {
        ++last_row;
        worksheet_body += R"(<row r=")";
        worksheet_body += std::to_string(last_row);
        worksheet_body += R"("><c r="A)";
        worksheet_body += std::to_string(last_row);
        worksheet_body += R"("><v>1</v></c></row>)";
    }
    const std::size_t staged_worksheet_size =
        worksheet_prefix.size() + worksheet_body.size() + worksheet_suffix.size();
    check(staged_worksheet_size
            > fastxlsx::detail::package_editor_cell_replacement_event_window_byte_limit,
        "planned staged chunk fixture should exceed the event-reader window total size");
    write_binary_file(body_path, worksheet_body);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    editor.replace_worksheet_part_chunks(worksheet_part,
        {
            fastxlsx::detail::PackageEntryChunk::memory(worksheet_prefix),
            fastxlsx::detail::PackageEntryChunk::file(body_path),
            fastxlsx::detail::PackageEntryChunk::memory(worksheet_suffix),
        });

    const std::array replacements {
        worksheet_cell_replacement("A1", R"(<c r="A1"><v>patched-staged</v></c>)"),
    };
    editor.replace_worksheet_cells(worksheet_part, replacements);

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(has_note_containing(output_plan.notes,
              {"planned worksheet staged chunks", "without materializing"}),
        "planned staged chunks cell replacement should expose non-materialized staged input");
    check(has_note_containing(output_plan.notes,
              {"planned worksheet staged chunks", "transformer chunk-source adapter"}),
        "planned staged chunks cell replacement should expose chunk-source transformer input");
    check(has_note_containing(output_plan.notes,
              {"planned worksheet staged chunks", "event-reader chunk-source validator"}),
        "planned staged chunks cell replacement should expose chunk-source root validation");
    check(!has_note_containing(output_plan.notes,
              {"planned worksheet staged chunks", "whole worksheet string"}),
        "planned staged chunks cell replacement should not expose old whole-string wording");
    check_output_entry_plan(output_plan.entries, worksheet_part.zip_path(),
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "planned staged chunks cell replacement should stream-rewrite worksheet chunks");
    check_output_entry_plan(output_plan.entries, workbook_part.zip_path(),
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "planned staged chunks cell replacement should local-rewrite workbook metadata");
    check_output_entry_plan(output_plan.entries, calc_chain_part.zip_path(),
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "planned staged chunks cell replacement should omit stale calcChain");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string worksheet_xml = output_reader.read_entry(worksheet_part.zip_path());
    check_contains(worksheet_xml, R"(<c r="A1"><v>patched-staged</v></c>)",
        "planned staged chunks cell replacement output should include replacement cell");
    check_not_contains(worksheet_xml, "old-staged",
        "planned staged chunks cell replacement output should consume old staged target cell");
    check_not_contains(worksheet_xml, "validation-only",
        "planned staged chunks cell replacement output should read staged chunks, not validation XML");
    const std::string expected_dimension =
        R"(<dimension ref="A1:A)" + std::to_string(last_row) + R"("/>)";
    check_contains(worksheet_xml, expected_dimension,
        "planned staged chunks cell replacement output should refresh dimension from chunk-source cells");
    check_contains(worksheet_xml,
        R"(<row r=")" + std::to_string(last_row) + R"("><c r="A)"
            + std::to_string(last_row) + R"("><v>1</v></c></row>)",
        "planned staged chunks cell replacement output should preserve tail staged rows");
    check(output_reader.find_entry(calc_chain_part.zip_path()) == nullptr,
        "planned staged chunks cell replacement output should omit stale calcChain payload");
    check_contains(output_reader.read_entry(workbook_part.zip_path()), R"(fullCalcOnLoad="1")",
        "planned staged chunks cell replacement output should request full calculation");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "planned staged chunks cell replacement output should preserve unknown bytes");
}

void test_package_editor_rejects_changed_planned_staged_chunk_sizes_without_state_changes()
{
    struct ChunkMutationCase {
        std::string_view name;
        std::string_view mutated_body;
    };

    const std::array cases {
        ChunkMutationCase {
            "truncated",
            R"(<row r="2"><c r="A2"><v>x</v></c></row>)",
        },
        ChunkMutationCase {
            "extended",
            R"(<row r="2"><c r="A2"><v>original-staged-body</v></c></row><!--extended-->)",
        },
    };

    for (const ChunkMutationCase& test_case : cases) {
        const std::string source_name =
            "fastxlsx-package-editor-planned-chunk-size-" + std::string(test_case.name)
            + "-source.xlsx";
        const CalcSourcePackage source = write_calc_source_package(source_name);
        const std::filesystem::path body_path =
            output_path("fastxlsx-package-editor-planned-chunk-size-"
                + std::string(test_case.name) + "-body.xml");

        const std::string worksheet_prefix =
            R"(<worksheet><dimension ref="A1:A2"/><sheetData>)"
            R"(<row r="1"><c r="A1"><v>old-staged</v></c></row>)";
        const std::string original_body =
            R"(<row r="2"><c r="A2"><v>original-staged-body</v></c></row>)";
        const std::string worksheet_suffix = R"(</sheetData></worksheet>)";
        const std::uint64_t planned_expected_bytes =
            static_cast<std::uint64_t>(
                worksheet_prefix.size() + original_body.size() + worksheet_suffix.size());
        write_binary_file(body_path, original_body);

        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);
        const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

        editor.replace_worksheet_part_chunks(worksheet_part,
            {
                fastxlsx::detail::PackageEntryChunk::memory(worksheet_prefix),
                fastxlsx::detail::PackageEntryChunk::file(body_path),
                fastxlsx::detail::PackageEntryChunk::memory(worksheet_suffix),
            });

        const std::size_t initial_plan_size = editor.edit_plan().size();
        const std::size_t initial_note_count = editor.edit_plan().notes().size();
        const std::size_t initial_payload_audit_count =
            editor.edit_plan().worksheet_payload_dependency_audits().size();
        const std::size_t initial_relationship_audit_count =
            editor.edit_plan().worksheet_relationship_reference_audits().size();
        const bool initial_full_calculation =
            editor.edit_plan().full_calculation_on_load();
        const std::vector<std::filesystem::path> temp_files_before =
            package_editor_temp_files();

        write_binary_file(body_path, test_case.mutated_body);

        const std::array replacements {
            worksheet_cell_replacement("A1", R"(<c r="A1"><v>patched</v></c>)"),
        };

        bool replacement_failed = false;
        try {
            editor.replace_worksheet_cells(worksheet_part, replacements);
        } catch (const std::exception& error) {
            replacement_failed = true;
            check_contains(error.what(),
                "failed to read current worksheet input for worksheet cell replacement analysis",
                "planned staged chunk size mutation should report the input read boundary");
            check_contains(error.what(),
                "planned worksheet staged chunks for 'xl/worksheets/sheet1.xml'",
                "planned staged chunk size mutation should identify the current input source");
            check_contains(error.what(),
                std::string("expected_bytes=") + std::to_string(planned_expected_bytes),
                "planned staged chunk size mutation should summarize recorded expected bytes");
            const std::size_t emitted_chunk_count =
                test_case.mutated_body.size() < original_body.size() ? 1U : 2U;
            const std::uint64_t emitted_byte_count =
                static_cast<std::uint64_t>(worksheet_prefix.size()
                    + (test_case.mutated_body.size() < original_body.size()
                            ? 0U
                            : original_body.size()));
            check_contains(error.what(),
                std::string("after emitting ") + std::to_string(emitted_chunk_count)
                    + " current-input chunk"
                    + (emitted_chunk_count == 1U ? "" : "s")
                    + " and " + std::to_string(emitted_byte_count)
                    + " bytes",
                "planned staged chunk size mutation should report current-input progress");
            const std::size_t read_attempt =
                test_case.mutated_body.size() < original_body.size() ? 2U : 3U;
            check_contains(error.what(),
                std::string("current-input read attempt ") + std::to_string(read_attempt),
                "planned staged chunk size mutation should report the failing read attempt");
            const std::uint64_t last_emitted_chunk_bytes =
                static_cast<std::uint64_t>(
                    test_case.mutated_body.size() < original_body.size()
                        ? worksheet_prefix.size()
                        : original_body.size());
            check_contains(error.what(),
                std::string("last chunk ") + std::to_string(last_emitted_chunk_bytes)
                    + " bytes",
                "planned staged chunk size mutation should report last emitted chunk size");
            check_contains(error.what(),
                std::string("expected ") + std::to_string(original_body.size()) + " bytes",
                "planned staged chunk size mutation should report expected bytes");
            check_staged_chunk_expected_total(error.what(), planned_expected_bytes,
                "planned staged chunk size mutation should report replay expected total");
            const std::uint64_t remaining_expected_bytes =
                test_case.mutated_body.size() < original_body.size()
                    ? static_cast<std::uint64_t>(
                          original_body.size() + worksheet_suffix.size())
                    : static_cast<std::uint64_t>(worksheet_suffix.size());
            check_staged_chunk_expected_remaining(error.what(), remaining_expected_bytes,
                "planned staged chunk size mutation should report replay remaining bytes");
            if (test_case.mutated_body.size() < original_body.size()) {
                check_contains(error.what(),
                    "staged package-entry chunk file ended before expected bytes",
                    "planned staged chunk size mutation should report short read");
                check_staged_file_chunk_read_progress(error.what(), 1, 0,
                    "planned staged chunk short read should report staged file progress");
                check_contains(error.what(),
                    std::string("actual ") + std::to_string(test_case.mutated_body.size())
                        + " bytes",
                    "planned staged chunk size mutation should report actual bytes");
            } else {
                check_contains(error.what(),
                    "staged package-entry chunk file produced more bytes than expected",
                    "planned staged chunk size mutation should report overrun read");
                check_staged_file_chunk_read_progress(error.what(), 2,
                    static_cast<std::uint64_t>(original_body.size()),
                    "planned staged chunk overrun should report staged file progress");
                check_contains(error.what(),
                    std::string("read at least ") + std::to_string(original_body.size() + 1U)
                        + " bytes",
                    "planned staged chunk size mutation should report lower-bound bytes");
            }
            check_contains(error.what(), "staged package-entry chunk 1",
                "planned staged chunk size mutation should identify the file-backed chunk");
            check_contains(error.what(), body_path.filename().generic_string(),
                "planned staged chunk size mutation should include the file-backed chunk path");
        }

        check(replacement_failed,
            "planned staged chunk size mutation should fail before follow-up transform");
        check(editor.edit_plan().size() == initial_plan_size,
            "planned staged chunk size mutation failure should preserve edit-plan size");
        check(editor.edit_plan().notes().size() == initial_note_count,
            "planned staged chunk size mutation failure should not append notes");
        check(editor.edit_plan().worksheet_payload_dependency_audits().size()
                == initial_payload_audit_count,
            "planned staged chunk size mutation failure should not append payload audits");
        check(editor.edit_plan().worksheet_relationship_reference_audits().size()
                == initial_relationship_audit_count,
            "planned staged chunk size mutation failure should not append relationship audits");
        check(editor.edit_plan().full_calculation_on_load() == initial_full_calculation,
            "planned staged chunk size mutation failure should preserve calc policy");
        check_manifest_write_mode(editor, worksheet_part,
            fastxlsx::detail::PartWriteMode::StreamRewrite,
            "planned staged chunk size mutation failure should keep prior staged worksheet plan");
        check_no_new_package_editor_temp_files(temp_files_before,
            "planned staged chunk size mutation failure should not leak temp files");
    }
}

void test_package_editor_rejects_changed_planned_staged_chunk_crc_without_state_changes()
{
    const CalcSourcePackage source = write_calc_source_package(
        "fastxlsx-package-editor-planned-chunk-crc-source.xlsx");
    const std::filesystem::path body_path =
        output_path("fastxlsx-package-editor-planned-chunk-crc-body.xml");

    const std::string worksheet_prefix =
        R"(<worksheet><dimension ref="A1:A2"/><sheetData>)"
        R"(<row r="1"><c r="A1"><v>old-staged</v></c></row>)";
    const std::string original_body =
        R"(<row r="2"><c r="A2"><v>original-staged-body</v></c></row>)";
    const std::string worksheet_suffix = R"(</sheetData></worksheet>)";
    const std::uint64_t planned_expected_bytes =
        static_cast<std::uint64_t>(
            worksheet_prefix.size() + original_body.size() + worksheet_suffix.size());
    write_binary_file(body_path, original_body);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    editor.replace_worksheet_part_chunks(worksheet_part,
        {
            fastxlsx::detail::PackageEntryChunk::memory(worksheet_prefix),
            fastxlsx::detail::PackageEntryChunk::file(body_path),
            fastxlsx::detail::PackageEntryChunk::memory(worksheet_suffix),
        });

    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_payload_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t initial_relationship_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const bool initial_full_calculation =
        editor.edit_plan().full_calculation_on_load();
    const std::vector<std::filesystem::path> temp_files_before =
        package_editor_temp_files();

    write_binary_file(body_path, same_size_different_payload(original_body));

    const std::array replacements {
        worksheet_cell_replacement("A1", R"(<c r="A1"><v>patched</v></c>)"),
    };

    bool replacement_failed = false;
    try {
        editor.replace_worksheet_cells(worksheet_part, replacements);
    } catch (const std::exception& error) {
        replacement_failed = true;
        check_contains(error.what(),
            "failed to read current worksheet input for worksheet cell replacement analysis",
            "planned staged chunk CRC mutation should report the input read boundary");
        check_contains(error.what(),
            "planned worksheet staged chunks for 'xl/worksheets/sheet1.xml'",
            "planned staged chunk CRC mutation should identify the current input source");
        check_contains(error.what(),
            std::string("expected_bytes=") + std::to_string(planned_expected_bytes),
            "planned staged chunk CRC mutation should summarize recorded expected bytes");
        check_contains(error.what(),
            std::string("after emitting 2 current-input chunks and ")
                + std::to_string(worksheet_prefix.size() + original_body.size()) + " bytes",
            "planned staged chunk CRC mutation should report completed current-input progress");
        check_contains(error.what(), "current-input read attempt 3",
            "planned staged chunk CRC mutation should report the failing read attempt");
        check_contains(error.what(),
            std::string("last chunk ") + std::to_string(original_body.size()) + " bytes",
            "planned staged chunk CRC mutation should report last emitted chunk size");
        check_contains(error.what(),
            "staged package-entry chunk CRC32 changed after validation",
            "planned staged chunk CRC mutation should report the CRC contract");
        check_staged_chunk_expected_total(error.what(), planned_expected_bytes,
            "planned staged chunk CRC mutation should report replay expected total");
        check_staged_chunk_expected_remaining(error.what(),
            static_cast<std::uint64_t>(worksheet_suffix.size()),
            "planned staged chunk CRC mutation should report replay remaining bytes");
        check_staged_file_chunk_read_progress(error.what(), 2,
            static_cast<std::uint64_t>(original_body.size()),
            "planned staged chunk CRC mutation should report staged file progress");
        check_contains(error.what(), "expected ",
            "planned staged chunk CRC mutation should report expected CRC");
        check_contains(error.what(), "actual ",
            "planned staged chunk CRC mutation should report actual CRC");
        check_contains(error.what(), "staged package-entry chunk 1",
            "planned staged chunk CRC mutation should identify the file-backed chunk");
        check_contains(error.what(), body_path.filename().generic_string(),
            "planned staged chunk CRC mutation should include the file-backed chunk path");
    }

    check(replacement_failed,
        "planned staged chunk CRC mutation should fail before follow-up transform");
    check(editor.edit_plan().size() == initial_plan_size,
        "planned staged chunk CRC mutation failure should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "planned staged chunk CRC mutation failure should not append notes");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == initial_payload_audit_count,
        "planned staged chunk CRC mutation failure should not append payload audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_relationship_audit_count,
        "planned staged chunk CRC mutation failure should not append relationship audits");
    check(editor.edit_plan().full_calculation_on_load() == initial_full_calculation,
        "planned staged chunk CRC mutation failure should preserve calc policy");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "planned staged chunk CRC mutation failure should keep prior staged worksheet plan");
    check_no_new_package_editor_temp_files(temp_files_before,
        "planned staged chunk CRC mutation failure should not leak temp files");
}

void test_package_editor_rejects_missing_planned_staged_chunk_file_at_read_boundary()
{
    const CalcSourcePackage source = write_calc_source_package(
        "fastxlsx-package-editor-planned-chunk-missing-file-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-planned-chunk-missing-file-output.xlsx");
    const std::filesystem::path body_path =
        output_path("fastxlsx-package-editor-planned-chunk-missing-file-body.xml");

    const std::string worksheet_prefix =
        R"(<worksheet><dimension ref="A1:A2"/><sheetData>)"
        R"(<row r="1"><c r="A1"><v>old-staged</v></c></row>)";
    const std::string original_body =
        R"(<row r="2"><c r="A2"><v>original-staged-body</v></c></row>)";
    const std::string worksheet_suffix = R"(</sheetData></worksheet>)";
    const std::uint64_t planned_expected_bytes =
        static_cast<std::uint64_t>(
            worksheet_prefix.size() + original_body.size() + worksheet_suffix.size());
    write_binary_file(body_path, original_body);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    editor.replace_worksheet_part_chunks(worksheet_part,
        {
            fastxlsx::detail::PackageEntryChunk::memory(worksheet_prefix),
            fastxlsx::detail::PackageEntryChunk::file(body_path),
            fastxlsx::detail::PackageEntryChunk::memory(worksheet_suffix),
        });

    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_payload_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t initial_relationship_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const bool initial_full_calculation =
        editor.edit_plan().full_calculation_on_load();
    const std::vector<std::filesystem::path> temp_files_before =
        package_editor_temp_files();

    std::error_code remove_error;
    const bool removed = std::filesystem::remove(body_path, remove_error);
    check(removed && !remove_error,
        "planned staged chunk missing-file fixture should delete the staged file");

    const std::array replacements {
        worksheet_cell_replacement("A1", R"(<c r="A1"><v>patched</v></c>)"),
    };

    bool replacement_failed = false;
    try {
        editor.replace_worksheet_cells(worksheet_part, replacements);
    } catch (const std::exception& error) {
        replacement_failed = true;
        check_contains(error.what(),
            "failed to read current worksheet input for worksheet cell replacement analysis",
            "missing planned staged chunk should fail at the current worksheet read boundary");
        check_contains(error.what(),
            "planned worksheet staged chunks for 'xl/worksheets/sheet1.xml'",
            "missing planned staged chunk should identify the current input source");
        check_contains(error.what(),
            std::string("expected_bytes=") + std::to_string(planned_expected_bytes),
            "missing planned staged chunk should summarize recorded expected bytes");
        check_contains(error.what(),
            std::string("after emitting 1 current-input chunk and ")
                + std::to_string(worksheet_prefix.size()) + " bytes",
            "missing planned staged chunk should report current-input progress");
        check_contains(error.what(), "current-input read attempt 2",
            "missing planned staged chunk should report the failing read attempt");
        check_contains(error.what(),
            std::string("last chunk ") + std::to_string(worksheet_prefix.size()) + " bytes",
            "missing planned staged chunk should report last emitted chunk size");
        check_contains(error.what(), "worksheet part '/xl/worksheets/sheet1.xml'",
            "missing planned staged chunk should identify the owning worksheet part");
        check_contains(error.what(), "ZIP entry 'xl/worksheets/sheet1.xml'",
            "missing planned staged chunk should identify the worksheet ZIP entry");
        check_not_contains(error.what(), "failed to initialize current worksheet input",
            "missing planned staged chunk should not fail during reader initialization");
        check_contains(error.what(), "failed to open staged package-entry chunk file",
            "missing planned staged chunk should report the read-time open failure");
        check_staged_file_chunk_read_progress(error.what(), 0, 0,
            "missing planned staged chunk should report staged file progress");
        check_contains(error.what(),
            std::string("expected ") + std::to_string(original_body.size()) + " bytes",
            "missing planned staged chunk should preserve recorded expected-size metadata");
        check_staged_chunk_expected_total(error.what(), planned_expected_bytes,
            "missing planned staged chunk should report replay expected total");
        check_staged_chunk_expected_remaining(error.what(),
            static_cast<std::uint64_t>(original_body.size() + worksheet_suffix.size()),
            "missing planned staged chunk should report replay remaining bytes");
        check_contains(error.what(), "staged package-entry chunk 1",
            "missing planned staged chunk should identify the file-backed chunk");
        check_contains(error.what(), body_path.filename().generic_string(),
            "missing planned staged chunk should include the file-backed chunk path");
    }

    check(replacement_failed,
        "PackageEditor should reject missing planned staged chunk files during follow-up transform");
    check(editor.edit_plan().size() == initial_plan_size,
        "missing planned staged chunk failure should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "missing planned staged chunk failure should not append notes");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == initial_payload_audit_count,
        "missing planned staged chunk failure should not append payload audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_relationship_audit_count,
        "missing planned staged chunk failure should not append relationship audits");
    check(editor.edit_plan().full_calculation_on_load() == initial_full_calculation,
        "missing planned staged chunk failure should preserve calc policy");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "missing planned staged chunk failure should keep prior staged worksheet plan");
    check_no_new_package_editor_temp_files(temp_files_before,
        "missing planned staged chunk failure should not leak PackageEditor temp files");

    write_binary_file(body_path, original_body);
    editor.replace_worksheet_cells(worksheet_part, replacements);
    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string worksheet_xml = output_reader.read_entry(worksheet_part.zip_path());
    check_contains(worksheet_xml, R"(<c r="A1"><v>patched</v></c>)",
        "later safe follow-up transform should still consume the restored staged chunk");
    check_not_contains(worksheet_xml, "old-staged",
        "later safe follow-up transform should replace the old staged target cell");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "later safe follow-up transform should preserve unknown bytes");
}

void test_package_editor_contextualizes_by_name_planned_staged_chunk_read_failures_without_state_changes()
{
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const std::string planned_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Renamed" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    const std::string worksheet_prefix =
        R"(<worksheet><dimension ref="A1:A2"/><sheetData>)"
        R"(<row r="1"><c r="A1"><v>old-staged</v></c></row>)";
    const std::string original_body =
        R"(<row r="2"><c r="A2"><v>original-staged-body</v></c></row>)";
    const std::string worksheet_suffix = R"(</sheetData></worksheet>)";
    const std::uint64_t planned_expected_bytes =
        static_cast<std::uint64_t>(
            worksheet_prefix.size() + original_body.size() + worksheet_suffix.size());
    const std::string planned_chunk_source_description =
        "planned worksheet staged chunks for 'xl/worksheets/sheet1.xml' "
        "(3 chunks; memory=2, file=1, expected_bytes="
        + std::to_string(planned_expected_bytes) + ")";

    {
        const CalcSourcePackage source = write_calc_source_package(
            "fastxlsx-package-editor-by-name-planned-chunk-size-source.xlsx");
        const std::filesystem::path body_path =
            output_path("fastxlsx-package-editor-by-name-planned-chunk-size-body.xml");
        write_binary_file(body_path, original_body);

        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);
        editor.replace_part(workbook_part, planned_workbook,
            fastxlsx::detail::PartWriteMode::LocalDomRewrite,
            "ordinary workbook replacement before by-name planned chunk size failure");
        editor.replace_worksheet_part_chunks(worksheet_part,
            {
                fastxlsx::detail::PackageEntryChunk::memory(worksheet_prefix),
                fastxlsx::detail::PackageEntryChunk::file(body_path),
                fastxlsx::detail::PackageEntryChunk::memory(worksheet_suffix),
            });

        const std::size_t initial_plan_size = editor.edit_plan().size();
        const std::size_t initial_note_count = editor.edit_plan().notes().size();
        const std::size_t initial_package_entry_count =
            editor.edit_plan().package_entries().size();
        const std::size_t initial_removed_package_entry_count =
            editor.edit_plan().removed_package_entries().size();
        const std::size_t initial_payload_audit_count =
            editor.edit_plan().worksheet_payload_dependency_audits().size();
        const std::size_t initial_relationship_audit_count =
            editor.edit_plan().worksheet_relationship_reference_audits().size();
        const bool initial_full_calculation =
            editor.edit_plan().full_calculation_on_load();
        const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
            editor.edit_plan().calc_chain_action();
        const std::vector<std::filesystem::path> temp_files_before =
            package_editor_temp_files();

        write_binary_file(body_path, original_body + "<!--extended-->");

        const std::array replacements {
            worksheet_cell_replacement("A1", R"(<c r="A1"><v>patched</v></c>)"),
        };

        bool failed = false;
        try {
            editor.replace_worksheet_cells_by_name("Renamed", replacements);
        } catch (const std::exception& error) {
            failed = true;
            check_contains(error.what(),
                "by-name worksheet cell replacement for sheet 'Renamed'",
                "by-name planned chunk size failure should identify the planned sheet name");
            check_contains(error.what(),
                "resolved to worksheet part '/xl/worksheets/sheet1.xml'",
                "by-name planned chunk size failure should show the resolved worksheet part");
            check_contains(error.what(),
                "failed to read current worksheet input for worksheet cell replacement analysis",
                "by-name planned chunk size failure should keep the analysis input boundary");
            check_contains(error.what(),
                planned_chunk_source_description,
                "by-name planned chunk size failure should identify the planned chunk source composition");
            check_contains(error.what(),
                std::string("after emitting 2 current-input chunks and ")
                    + std::to_string(worksheet_prefix.size() + original_body.size()) + " bytes",
                "by-name planned chunk size failure should report current-input progress");
            check_contains(error.what(), "current-input read attempt 3",
                "by-name planned chunk size failure should report the failing read attempt");
            check_contains(error.what(),
                std::string("last chunk ") + std::to_string(original_body.size()) + " bytes",
                "by-name planned chunk size failure should report last emitted chunk size");
            check_contains(error.what(), "worksheet part '/xl/worksheets/sheet1.xml'",
                "by-name planned chunk size failure should identify the owning worksheet part");
            check_contains(error.what(), "ZIP entry 'xl/worksheets/sheet1.xml'",
                "by-name planned chunk size failure should identify the worksheet ZIP entry");
            check_contains(error.what(),
                std::string("expected ") + std::to_string(original_body.size()) + " bytes",
                "by-name planned chunk size failure should report expected bytes");
            check_contains(error.what(),
                "staged package-entry chunk file produced more bytes than expected",
                "by-name planned chunk size failure should report overrun read");
            check_contains(error.what(),
                std::string("read at least ") + std::to_string(original_body.size() + 1U)
                    + " bytes",
                "by-name planned chunk size failure should report lower-bound bytes");
            check_contains(error.what(), "staged package-entry chunk 1",
                "by-name planned chunk size failure should identify the file-backed chunk");
            check_contains(error.what(), body_path.filename().generic_string(),
                "by-name planned chunk size failure should include the file-backed chunk path");
        }

        check(failed,
            "by-name cell replacement should reject mutated planned staged chunk size");
        check(editor.edit_plan().size() == initial_plan_size,
            "by-name planned chunk size failure should preserve edit-plan size");
        check(editor.edit_plan().notes().size() == initial_note_count,
            "by-name planned chunk size failure should not append notes");
        check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
            "by-name planned chunk size failure should not add package-entry audits");
        check(editor.edit_plan().removed_package_entries().size()
                == initial_removed_package_entry_count,
            "by-name planned chunk size failure should not add removed package-entry audits");
        check(editor.edit_plan().worksheet_payload_dependency_audits().size()
                == initial_payload_audit_count,
            "by-name planned chunk size failure should not append payload audits");
        check(editor.edit_plan().worksheet_relationship_reference_audits().size()
                == initial_relationship_audit_count,
            "by-name planned chunk size failure should not append relationship audits");
        check(editor.edit_plan().full_calculation_on_load() == initial_full_calculation,
            "by-name planned chunk size failure should preserve calc policy");
        check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
            "by-name planned chunk size failure should preserve calcChain policy");
        check_manifest_write_mode(editor, workbook_part,
            fastxlsx::detail::PartWriteMode::LocalDomRewrite,
            "by-name planned chunk size failure should keep planned workbook rewrite");
        check_manifest_write_mode(editor, worksheet_part,
            fastxlsx::detail::PartWriteMode::StreamRewrite,
            "by-name planned chunk size failure should keep prior staged worksheet plan");
        check_no_new_package_editor_temp_files(temp_files_before,
            "by-name planned chunk size failure should not leak PackageEditor temp files");
    }

    {
        const CalcSourcePackage source = write_calc_source_package(
            "fastxlsx-package-editor-by-name-sheetdata-planned-chunk-crc-source.xlsx");
        const std::filesystem::path body_path =
            output_path("fastxlsx-package-editor-by-name-sheetdata-planned-chunk-crc-body.xml");
        write_binary_file(body_path, original_body);

        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);
        editor.replace_part(workbook_part, planned_workbook,
            fastxlsx::detail::PartWriteMode::LocalDomRewrite,
            "ordinary workbook replacement before by-name sheetData planned chunk CRC failure");
        editor.replace_worksheet_part_chunks(worksheet_part,
            {
                fastxlsx::detail::PackageEntryChunk::memory(worksheet_prefix),
                fastxlsx::detail::PackageEntryChunk::file(body_path),
                fastxlsx::detail::PackageEntryChunk::memory(worksheet_suffix),
            });

        const std::size_t initial_plan_size = editor.edit_plan().size();
        const std::size_t initial_note_count = editor.edit_plan().notes().size();
        const std::size_t initial_package_entry_count =
            editor.edit_plan().package_entries().size();
        const std::size_t initial_removed_package_entry_count =
            editor.edit_plan().removed_package_entries().size();
        const std::size_t initial_payload_audit_count =
            editor.edit_plan().worksheet_payload_dependency_audits().size();
        const std::size_t initial_relationship_audit_count =
            editor.edit_plan().worksheet_relationship_reference_audits().size();
        const bool initial_full_calculation =
            editor.edit_plan().full_calculation_on_load();
        const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
            editor.edit_plan().calc_chain_action();
        const std::vector<std::filesystem::path> temp_files_before =
            package_editor_temp_files();

        write_binary_file(body_path, same_size_different_payload(original_body));

        bool failed = false;
        try {
            replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor,
                "Renamed",
                R"(<sheetData><row r="1"><c r="A1"><v>42</v></c></row></sheetData>)");
        } catch (const std::exception& error) {
            failed = true;
            check_contains(error.what(),
                "by-name sheetData replacement for sheet 'Renamed'",
                "by-name sheetData planned chunk CRC failure should identify the planned sheet name");
            check_contains(error.what(),
                "resolved to worksheet part '/xl/worksheets/sheet1.xml'",
                "by-name sheetData planned chunk CRC failure should show the resolved worksheet part");
            check_contains(error.what(),
                "failed to read current worksheet input for worksheet sheetData replacement output",
                "by-name sheetData planned chunk CRC failure should keep the output input boundary");
            check_contains(error.what(),
                planned_chunk_source_description,
                "by-name sheetData planned chunk CRC failure should identify the planned chunk source composition");
            check_contains(error.what(),
                std::string("after emitting 2 current-input chunks and ")
                    + std::to_string(worksheet_prefix.size() + original_body.size()) + " bytes",
                "by-name sheetData planned chunk CRC failure should report completed current-input progress");
            check_contains(error.what(), "current-input read attempt 3",
                "by-name sheetData planned chunk CRC failure should report the failing read attempt");
            check_contains(error.what(),
                std::string("last chunk ") + std::to_string(original_body.size()) + " bytes",
                "by-name sheetData planned chunk CRC failure should report last emitted chunk size");
            check_contains(error.what(),
                "staged package-entry chunk CRC32 changed after validation",
                "by-name sheetData planned chunk CRC failure should report the CRC contract");
            check_contains(error.what(), "expected ",
                "by-name sheetData planned chunk CRC failure should report expected CRC");
            check_contains(error.what(), "actual ",
                "by-name sheetData planned chunk CRC failure should report actual CRC");
            check_contains(error.what(), "staged package-entry chunk 1",
                "by-name sheetData planned chunk CRC failure should identify the file-backed chunk");
            check_contains(error.what(), body_path.filename().generic_string(),
                "by-name sheetData planned chunk CRC failure should include the file-backed chunk path");
            check_not_contains(error.what(), "sheetData replacement XML",
                "by-name sheetData planned chunk CRC failure should not be mislabeled as replacement payload input");
        }

        check(failed,
            "by-name sheetData replacement should reject mutated planned staged chunk CRC");
        check(editor.edit_plan().size() == initial_plan_size,
            "by-name sheetData planned chunk CRC failure should preserve edit-plan size");
        check(editor.edit_plan().notes().size() == initial_note_count,
            "by-name sheetData planned chunk CRC failure should not append notes");
        check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
            "by-name sheetData planned chunk CRC failure should not add package-entry audits");
        check(editor.edit_plan().removed_package_entries().size()
                == initial_removed_package_entry_count,
            "by-name sheetData planned chunk CRC failure should not add removed package-entry audits");
        check(editor.edit_plan().worksheet_payload_dependency_audits().size()
                == initial_payload_audit_count,
            "by-name sheetData planned chunk CRC failure should not append payload audits");
        check(editor.edit_plan().worksheet_relationship_reference_audits().size()
                == initial_relationship_audit_count,
            "by-name sheetData planned chunk CRC failure should not append relationship audits");
        check(editor.edit_plan().full_calculation_on_load() == initial_full_calculation,
            "by-name sheetData planned chunk CRC failure should preserve calc policy");
        check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
            "by-name sheetData planned chunk CRC failure should preserve calcChain policy");
        check_manifest_write_mode(editor, workbook_part,
            fastxlsx::detail::PartWriteMode::LocalDomRewrite,
            "by-name sheetData planned chunk CRC failure should keep planned workbook rewrite");
        check_manifest_write_mode(editor, worksheet_part,
            fastxlsx::detail::PartWriteMode::StreamRewrite,
            "by-name sheetData planned chunk CRC failure should keep prior staged worksheet plan");
        check_no_new_package_editor_temp_files(temp_files_before,
            "by-name sheetData planned chunk CRC failure should not leak PackageEditor temp files");
    }
}

void test_package_editor_rejects_changed_planned_staged_chunk_sizes_for_sheet_data_without_state_changes()
{
    struct ChunkMutationCase {
        std::string_view name;
        std::string_view mutated_body;
    };

    const std::array cases {
        ChunkMutationCase {
            "truncated",
            R"(<row r="2"><c r="A2"><v>x</v></c></row>)",
        },
        ChunkMutationCase {
            "extended",
            R"(<row r="2"><c r="A2"><v>original-staged-body</v></c></row><!--extended-->)",
        },
    };

    for (const ChunkMutationCase& test_case : cases) {
        const CalcSourcePackage source = write_calc_source_package(
            "fastxlsx-package-editor-sheetdata-planned-chunk-size-"
            + std::string(test_case.name) + "-source.xlsx");
        const std::filesystem::path body_path =
            output_path("fastxlsx-package-editor-sheetdata-planned-chunk-size-"
                + std::string(test_case.name) + "-body.xml");

        const std::string worksheet_prefix =
            R"(<worksheet><dimension ref="A1:A2"/><sheetData>)"
            R"(<row r="1"><c r="A1"><v>old-staged</v></c></row>)";
        const std::string original_body =
            R"(<row r="2"><c r="A2"><v>original-staged-body</v></c></row>)";
        const std::string worksheet_suffix = R"(</sheetData></worksheet>)";
        write_binary_file(body_path, original_body);

        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);
        const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

        editor.replace_worksheet_part_chunks(worksheet_part,
            {
                fastxlsx::detail::PackageEntryChunk::memory(worksheet_prefix),
                fastxlsx::detail::PackageEntryChunk::file(body_path),
                fastxlsx::detail::PackageEntryChunk::memory(worksheet_suffix),
            });

        const std::size_t initial_plan_size = editor.edit_plan().size();
        const std::size_t initial_note_count = editor.edit_plan().notes().size();
        const std::size_t initial_package_entry_count =
            editor.edit_plan().package_entries().size();
        const std::size_t initial_removed_package_entry_count =
            editor.edit_plan().removed_package_entries().size();
        const std::size_t initial_payload_audit_count =
            editor.edit_plan().worksheet_payload_dependency_audits().size();
        const std::size_t initial_relationship_audit_count =
            editor.edit_plan().worksheet_relationship_reference_audits().size();
        const bool initial_full_calculation =
            editor.edit_plan().full_calculation_on_load();
        const std::vector<std::filesystem::path> temp_files_before =
            package_editor_temp_files();

        write_binary_file(body_path, test_case.mutated_body);

        bool failed = false;
        try {
            replace_worksheet_sheet_data_from_single_chunk_source(editor, worksheet_part,
                R"(<sheetData><row r="1"><c r="A1"><v>42</v></c></row></sheetData>)");
        } catch (const std::exception& error) {
            failed = true;
            check_contains(error.what(),
                "failed to read current worksheet input for worksheet "
                "sheetData replacement output",
                "sheetData planned staged chunk mutation should report the "
                "input read boundary");
            const std::size_t emitted_chunk_count =
                test_case.mutated_body.size() < original_body.size() ? 1U : 2U;
            const std::uint64_t emitted_byte_count =
                static_cast<std::uint64_t>(worksheet_prefix.size()
                    + (test_case.mutated_body.size() < original_body.size()
                            ? 0U
                            : original_body.size()));
            check_contains(error.what(),
                std::string("after emitting ") + std::to_string(emitted_chunk_count)
                    + " current-input chunk"
                    + (emitted_chunk_count == 1U ? "" : "s")
                    + " and " + std::to_string(emitted_byte_count)
                    + " bytes",
                "sheetData planned staged chunk mutation should report current-input progress");
            const std::size_t read_attempt =
                test_case.mutated_body.size() < original_body.size() ? 2U : 3U;
            check_contains(error.what(),
                std::string("current-input read attempt ") + std::to_string(read_attempt),
                "sheetData planned staged chunk mutation should report the failing read attempt");
            const std::uint64_t last_emitted_chunk_bytes =
                static_cast<std::uint64_t>(
                    test_case.mutated_body.size() < original_body.size()
                        ? worksheet_prefix.size()
                        : original_body.size());
            check_contains(error.what(),
                std::string("last chunk ") + std::to_string(last_emitted_chunk_bytes)
                    + " bytes",
                "sheetData planned staged chunk mutation should report last emitted chunk size");
            check_contains(error.what(),
                std::string("expected ") + std::to_string(original_body.size()) + " bytes",
                "sheetData planned staged chunk mutation should report expected bytes");
            const std::uint64_t planned_expected_bytes =
                static_cast<std::uint64_t>(
                    worksheet_prefix.size() + original_body.size()
                    + worksheet_suffix.size());
            check_staged_chunk_expected_total(error.what(), planned_expected_bytes,
                "sheetData planned staged chunk mutation should report replay expected total");
            const std::uint64_t remaining_expected_bytes =
                test_case.mutated_body.size() < original_body.size()
                    ? static_cast<std::uint64_t>(
                          original_body.size() + worksheet_suffix.size())
                    : static_cast<std::uint64_t>(worksheet_suffix.size());
            check_staged_chunk_expected_remaining(error.what(), remaining_expected_bytes,
                "sheetData planned staged chunk mutation should report replay remaining bytes");
            if (test_case.mutated_body.size() < original_body.size()) {
                check_contains(error.what(),
                    "staged package-entry chunk file ended before expected bytes",
                    "sheetData planned staged chunk mutation should report short read");
                check_staged_file_chunk_read_progress(error.what(), 1, 0,
                    "sheetData planned staged short read should report staged file progress");
                check_contains(error.what(),
                    std::string("actual ") + std::to_string(test_case.mutated_body.size())
                        + " bytes",
                    "sheetData planned staged chunk mutation should report actual bytes");
            } else {
                check_contains(error.what(),
                    "staged package-entry chunk file produced more bytes than expected",
                    "sheetData planned staged chunk mutation should report overrun read");
                check_staged_file_chunk_read_progress(error.what(), 2,
                    static_cast<std::uint64_t>(original_body.size()),
                    "sheetData planned staged overrun should report staged file progress");
                check_contains(error.what(),
                    std::string("read at least ") + std::to_string(original_body.size() + 1U)
                        + " bytes",
                    "sheetData planned staged chunk mutation should report lower-bound bytes");
            }
            check_contains(error.what(), "staged package-entry chunk 1",
                "sheetData planned staged chunk mutation should identify the file-backed chunk");
            check_contains(error.what(), body_path.filename().generic_string(),
                "sheetData planned staged chunk mutation should include the file-backed chunk path");
        }

        check(failed,
            "sheetData replacement should reject planned staged chunks whose size changed");
        check(editor.edit_plan().size() == initial_plan_size,
            "sheetData planned staged chunk mutation should preserve edit-plan size");
        check(editor.edit_plan().notes().size() == initial_note_count,
            "sheetData planned staged chunk mutation should not append notes");
        check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
            "sheetData planned staged chunk mutation should not add package-entry audits");
        check(editor.edit_plan().removed_package_entries().size()
                == initial_removed_package_entry_count,
            "sheetData planned staged chunk mutation should not add removed package-entry audits");
        check(editor.edit_plan().worksheet_payload_dependency_audits().size()
                == initial_payload_audit_count,
            "sheetData planned staged chunk mutation should not append payload audits");
        check(editor.edit_plan().worksheet_relationship_reference_audits().size()
                == initial_relationship_audit_count,
            "sheetData planned staged chunk mutation should not append relationship audits");
        check(editor.edit_plan().full_calculation_on_load() == initial_full_calculation,
            "sheetData planned staged chunk mutation should preserve calc policy");
        check_manifest_write_mode(editor, worksheet_part,
            fastxlsx::detail::PartWriteMode::StreamRewrite,
            "sheetData planned staged chunk mutation should keep prior staged worksheet plan");
        check_no_new_package_editor_temp_files(temp_files_before,
            "sheetData planned staged chunk mutation should not leak PackageEditor temp files");
    }
}

void test_package_editor_rejects_changed_planned_staged_chunk_crc_for_sheet_data_without_state_changes()
{
    const CalcSourcePackage source = write_calc_source_package(
        "fastxlsx-package-editor-sheetdata-planned-chunk-crc-source.xlsx");
    const std::filesystem::path body_path =
        output_path("fastxlsx-package-editor-sheetdata-planned-chunk-crc-body.xml");

    const std::string worksheet_prefix =
        R"(<worksheet><dimension ref="A1:A2"/><sheetData>)"
        R"(<row r="1"><c r="A1"><v>old-staged</v></c></row>)";
    const std::string original_body =
        R"(<row r="2"><c r="A2"><v>original-staged-body</v></c></row>)";
    const std::string worksheet_suffix = R"(</sheetData></worksheet>)";
    write_binary_file(body_path, original_body);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    editor.replace_worksheet_part_chunks(worksheet_part,
        {
            fastxlsx::detail::PackageEntryChunk::memory(worksheet_prefix),
            fastxlsx::detail::PackageEntryChunk::file(body_path),
            fastxlsx::detail::PackageEntryChunk::memory(worksheet_suffix),
        });

    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t initial_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const std::size_t initial_payload_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t initial_relationship_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const bool initial_full_calculation =
        editor.edit_plan().full_calculation_on_load();
    const std::vector<std::filesystem::path> temp_files_before =
        package_editor_temp_files();

    write_binary_file(body_path, same_size_different_payload(original_body));

    bool failed = false;
    try {
        replace_worksheet_sheet_data_from_single_chunk_source(editor, worksheet_part,
            R"(<sheetData><row r="1"><c r="A1"><v>42</v></c></row></sheetData>)");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(),
            "failed to read current worksheet input for worksheet "
            "sheetData replacement output",
            "sheetData planned staged chunk CRC mutation should report the "
            "input read boundary");
        check_contains(error.what(),
            "planned worksheet staged chunks for 'xl/worksheets/sheet1.xml'",
            "sheetData planned staged chunk CRC mutation should identify the current input source");
        check_contains(error.what(),
            std::string("after emitting 2 current-input chunks and ")
                + std::to_string(worksheet_prefix.size() + original_body.size()) + " bytes",
            "sheetData planned staged chunk CRC mutation should report completed current-input progress");
        check_contains(error.what(), "current-input read attempt 3",
            "sheetData planned staged chunk CRC mutation should report the failing read attempt");
        check_contains(error.what(),
            std::string("last chunk ") + std::to_string(original_body.size()) + " bytes",
            "sheetData planned staged chunk CRC mutation should report last emitted chunk size");
        check_contains(error.what(),
            "staged package-entry chunk CRC32 changed after validation",
            "sheetData planned staged chunk CRC mutation should report the CRC contract");
        const std::uint64_t planned_expected_bytes =
            static_cast<std::uint64_t>(
                worksheet_prefix.size() + original_body.size() + worksheet_suffix.size());
        check_staged_chunk_expected_total(error.what(), planned_expected_bytes,
            "sheetData planned staged chunk CRC mutation should report replay expected total");
        check_staged_chunk_expected_remaining(error.what(),
            static_cast<std::uint64_t>(worksheet_suffix.size()),
            "sheetData planned staged chunk CRC mutation should report replay remaining bytes");
        check_staged_file_chunk_read_progress(error.what(), 2,
            static_cast<std::uint64_t>(original_body.size()),
            "sheetData planned staged CRC mutation should report staged file progress");
        check_contains(error.what(), "expected ",
            "sheetData planned staged chunk CRC mutation should report expected CRC");
        check_contains(error.what(), "actual ",
            "sheetData planned staged chunk CRC mutation should report actual CRC");
        check_contains(error.what(), "staged package-entry chunk 1",
            "sheetData planned staged chunk CRC mutation should identify the file-backed chunk");
        check_contains(error.what(), body_path.filename().generic_string(),
            "sheetData planned staged chunk CRC mutation should include the file-backed chunk path");
    }

    check(failed,
        "sheetData replacement should reject planned staged chunks whose CRC changed");
    check(editor.edit_plan().size() == initial_plan_size,
        "sheetData planned staged chunk CRC mutation should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "sheetData planned staged chunk CRC mutation should not append notes");
    check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
        "sheetData planned staged chunk CRC mutation should not add package-entry audits");
    check(editor.edit_plan().removed_package_entries().size()
            == initial_removed_package_entry_count,
        "sheetData planned staged chunk CRC mutation should not add removed package-entry audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == initial_payload_audit_count,
        "sheetData planned staged chunk CRC mutation should not append payload audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_relationship_audit_count,
        "sheetData planned staged chunk CRC mutation should not append relationship audits");
    check(editor.edit_plan().full_calculation_on_load() == initial_full_calculation,
        "sheetData planned staged chunk CRC mutation should preserve calc policy");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "sheetData planned staged chunk CRC mutation should keep prior staged worksheet plan");
    check_no_new_package_editor_temp_files(temp_files_before,
        "sheetData planned staged chunk CRC mutation should not leak PackageEditor temp files");
}

void test_package_editor_streams_large_source_worksheet_cell_replacement_beyond_event_window_total_size()
{
    CalcSourcePackage source =
        write_calc_source_package(
            "fastxlsx-package-editor-cell-replacement-large-source-input-source.xlsx");
    const std::filesystem::path output =
        output_path(
            "fastxlsx-package-editor-cell-replacement-large-source-input-output.xlsx");
    source.worksheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>old</v></c></row>)";
    std::uint32_t last_row = 1;
    while (source.worksheet.size()
        <= fastxlsx::detail::package_editor_cell_replacement_event_window_byte_limit
            + 4096U) {
        ++last_row;
        source.worksheet += R"(<row r=")";
        source.worksheet += std::to_string(last_row);
        source.worksheet += R"("><c r="A)";
        source.worksheet += std::to_string(last_row);
        source.worksheet += R"("><v>1</v></c></row>)";
    }
    source.worksheet += R"(</sheetData></worksheet>)";
    check(source.worksheet.size()
            > fastxlsx::detail::package_editor_cell_replacement_event_window_byte_limit,
        "large source worksheet fixture should exceed the event-reader window total size");
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
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const std::array replacements {
        worksheet_cell_replacement("A1", R"(<c r="A1"><v>patched</v></c>)"),
    };

    editor.replace_worksheet_cells(worksheet_part, replacements);

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(has_note_containing(output_plan.notes,
              {"without materializing", "source worksheet XML"}),
        "large source cell replacement should expose non-materialized source input");
    check(has_note_containing(output_plan.notes,
              {"transformer chunk-source adapter", "source package worksheet XML"}),
        "large source cell replacement should expose chunk-source transformer input");
    check(has_note_containing(output_plan.notes,
              {"root validation", "event-reader chunk-source validator"}),
        "large source cell replacement should expose chunk-source root validation");
    check(!has_note_containing(output_plan.notes,
              {"source package worksheet XML", "whole worksheet string"}),
        "large source cell replacement should not expose old whole-string wording");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "large source cell replacement should stream-rewrite worksheet chunks");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "large source cell replacement should local-rewrite workbook metadata");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "large source cell replacement should omit stale calcChain");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string worksheet_xml = output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<c r="A1"><v>patched</v></c>)",
        "large source cell replacement output should include replacement cell");
    check_not_contains(worksheet_xml, R"(<v>old</v>)",
        "large source cell replacement output should consume old target cell");
    const std::string expected_dimension =
        R"(<dimension ref="A1:A)" + std::to_string(last_row) + R"("/>)";
    check_contains(worksheet_xml, expected_dimension,
        "large source cell replacement output should refresh dimension from streamed cells");
    check_contains(worksheet_xml,
        R"(<row r=")" + std::to_string(last_row) + R"("><c r="A)"
            + std::to_string(last_row) + R"("><v>1</v></c></row>)",
        "large source cell replacement output should preserve tail source rows");
    check(output_reader.find_entry("xl/calcChain.xml") == nullptr,
        "large source cell replacement output should omit stale calcChain payload");
    check_contains(output_reader.read_entry("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "large source cell replacement output should request full calculation");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "large source cell replacement output should preserve unknown bytes");
}

void test_package_editor_streams_large_planned_worksheet_cell_replacement_beyond_event_window_total_size()
{
    const CalcSourcePackage source =
        write_calc_source_package(
            "fastxlsx-package-editor-cell-replacement-large-planned-input-source.xlsx");
    const std::filesystem::path output =
        output_path(
            "fastxlsx-package-editor-cell-replacement-large-planned-input-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const std::string worksheet_prefix =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>42</v></c></row>)";
    std::string worksheet_body;
    std::uint32_t last_row = 1;
    const std::string worksheet_suffix = R"(</sheetData></worksheet>)";
    while (worksheet_prefix.size() + worksheet_body.size() + worksheet_suffix.size()
        <= fastxlsx::detail::package_editor_cell_replacement_event_window_byte_limit
            + 4096U) {
        ++last_row;
        worksheet_body += R"(<row r=")";
        worksheet_body += std::to_string(last_row);
        worksheet_body += R"("><c r="A)";
        worksheet_body += std::to_string(last_row);
        worksheet_body += R"("><v>1</v></c></row>)";
    }
    const std::string oversized_planned_worksheet =
        worksheet_prefix + worksheet_body + worksheet_suffix;
    check(oversized_planned_worksheet.size()
            > fastxlsx::detail::package_editor_cell_replacement_event_window_byte_limit,
        "large planned worksheet fixture should exceed the event-reader window total size");
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, oversized_planned_worksheet);

    const std::array replacements {
        worksheet_cell_replacement("A1", R"(<c r="A1"><v>43</v></c>)"),
    };
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "large planned cell fixture should start with worksheet stream rewrite");
    check(editor.edit_plan().full_calculation_on_load(),
        "large planned cell fixture should request full calculation before cell replacement");
    check(editor.edit_plan().calc_chain_action()
            == fastxlsx::detail::CalcChainAction::Remove,
        "large planned cell fixture should request calcChain removal before cell replacement");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "large planned cell fixture should remove calcChain before cell replacement");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "large planned cell fixture should mark worksheet stream-rewrite before cell replacement");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "large planned cell fixture should mark workbook metadata rewrite before cell replacement");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "large planned cell fixture should omit calcChain from manifest before cell replacement");

    editor.replace_worksheet_cells(worksheet_part, replacements);

    const fastxlsx::detail::PackageEditorOutputPlan output_plan =
        editor.planned_output();
    check(has_note_containing(output_plan.notes,
              {"pull-based chunk source", "file-backed staged chunk",
                  "follow-up planned-input transforms"}),
        "large planned cell replacement should report file-backed planned worksheet handoff");
    check(has_note_containing(output_plan.notes,
        {"writes the staged worksheet chunk in one caller chunk-source pass",
            "without reopening that staged chunk"}),
        "large planned cell replacement should report fused caller chunk-source staging/audit handoff");
    check(has_note_containing(output_plan.notes,
              {"planned worksheet staged chunks", "without materializing"}),
        "large planned cell replacement should expose non-materialized staged input");
    check(has_note_containing(output_plan.notes,
              {"planned worksheet staged chunks", "transformer chunk-source adapter"}),
        "large planned cell replacement should expose staged chunk transformer input");
    check(has_note_containing(output_plan.notes,
              {"root validation", "event-reader chunk-source validator"}),
        "large planned cell replacement should expose chunk-source root validation");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "large planned cell replacement output plan should stream-rewrite worksheet chunks");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "large planned cell replacement output plan should local-rewrite workbook metadata");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "large planned cell replacement output plan should omit stale calcChain");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string worksheet_xml = output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<c r="A1"><v>43</v></c>)",
        "large planned cell replacement output should include replacement cell");
    check_not_contains(worksheet_xml, R"(<v>42</v>)",
        "large planned cell replacement output should consume old planned target cell");
    const std::string expected_dimension =
        R"(<dimension ref="A1:A)" + std::to_string(last_row) + R"("/>)";
    check_contains(worksheet_xml, expected_dimension,
        "large planned cell replacement output should refresh dimension from staged planned cells");
    check_contains(worksheet_xml,
        R"(<row r=")" + std::to_string(last_row) + R"("><c r="A)"
            + std::to_string(last_row) + R"("><v>1</v></c></row>)",
        "large planned cell replacement output should preserve tail planned rows");
    check(output_reader.find_entry("xl/calcChain.xml") == nullptr,
        "large planned cell replacement output should omit stale calcChain payload");
    check_contains(output_reader.read_entry("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "large planned cell replacement output should request full calculation");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "large planned cell replacement output should preserve unknown bytes");
}

void test_package_editor_worksheet_cell_replacement_missing_target_fails_before_state_change()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-cell-replacement-missing-source.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const std::array replacements {
        worksheet_cell_replacement("C3", R"(<c r="C3"><v>missing</v></c>)"),
    };

    bool failed = false;
    try {
        editor.replace_worksheet_cells_by_name("Sheet1", replacements);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "C3",
            "missing cell replacement error should include caller target");
    }
    check(failed, "missing cell replacement target should fail");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "missing cell replacement should not request recalculation");
    check(output_plan.removed_parts.empty(),
        "missing cell replacement should not remove package parts");
    check_output_plan_preserves_source_copy_original(
        editor, output_plan, "missing cell replacement should not dirty output plan");
}

void test_package_editor_rejects_invalid_cell_replacement_payload_without_state_changes()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-cell-replacement-invalid-payload-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-cell-replacement-invalid-payload-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t initial_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t initial_worksheet_payload_dependency_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t initial_workbook_payload_dependency_audit_count =
        editor.edit_plan().workbook_payload_dependency_audits().size();
    const std::size_t initial_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t initial_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();

    const auto check_no_state_change = [&]() {
        check(editor.edit_plan().size() == initial_plan_size,
            "invalid cell replacement payload should not change edit plan size");
        check(editor.edit_plan().notes().size() == initial_note_count,
            "invalid cell replacement payload should not add audit notes");
        check(editor.edit_plan().relationship_target_audits().size()
                == initial_relationship_target_audit_count,
            "invalid cell replacement payload should not add relationship target audits");
        check(editor.edit_plan().worksheet_relationship_reference_audits().size()
                == initial_worksheet_relationship_reference_audit_count,
            "invalid cell replacement payload should not add worksheet reference audits");
        check(editor.edit_plan().worksheet_payload_dependency_audits().size()
                == initial_worksheet_payload_dependency_audit_count,
            "invalid cell replacement payload should not add worksheet payload audits");
        check(editor.edit_plan().workbook_payload_dependency_audits().size()
                == initial_workbook_payload_dependency_audit_count,
            "invalid cell replacement payload should not add workbook payload audits");
        check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
            "invalid cell replacement payload should not add package-entry audit");
        check(editor.edit_plan().removed_parts().empty(),
            "invalid cell replacement payload should not record removed parts");
        check(editor.edit_plan().removed_package_entries().size()
                == initial_removed_package_entry_count,
            "invalid cell replacement payload should not record removed package entries");
        check(!editor.edit_plan().full_calculation_on_load(),
            "invalid cell replacement payload should not request recalculation");
        check(editor.edit_plan().calc_chain_action()
                == fastxlsx::detail::CalcChainAction::Preserve,
            "invalid cell replacement payload should not change calcChain policy");
        check_manifest_write_mode(editor, worksheet_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "invalid cell replacement payload should keep worksheet copy-original");
        check_manifest_write_mode(editor, workbook_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "invalid cell replacement payload should keep workbook copy-original");
        check_manifest_write_mode(editor, calc_chain_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "invalid cell replacement payload should keep calcChain copy-original");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check(!output_plan.full_calculation_on_load,
            "invalid cell replacement payload output plan should not request recalculation");
        check(output_plan.notes.empty(),
            "invalid cell replacement payload output plan should not add notes");
        check(output_plan.relationship_target_audits.empty(),
            "invalid cell replacement payload output plan should not expose relationship audits");
        check(output_plan.worksheet_relationship_reference_audits.empty(),
            "invalid cell replacement payload output plan should not expose worksheet audits");
        check(output_plan.worksheet_payload_dependency_audits.size()
                == initial_worksheet_payload_dependency_audit_count,
            "invalid cell replacement payload output plan should not add worksheet payload audits");
        check(output_plan.workbook_payload_dependency_audits.size()
                == initial_workbook_payload_dependency_audit_count,
            "invalid cell replacement payload output plan should not add workbook payload audits");
        check_output_plan_preserves_source_copy_original(editor, output_plan,
            "invalid cell replacement payload should leave planned output copy-original");
    };

    struct InvalidPayloadCase {
        std::string_view materialized_replacement_cell_xml;
        std::string_view expected_error;
    };
    const std::array invalid_cases {
        InvalidPayloadCase {
            R"(<row r="1"><c r="A1"><v>2</v></c></row>)",
            "root must be a cell element",
        },
        InvalidPayloadCase {
            R"(<c><v>2</v></c>)",
            "must include an r attribute",
        },
        InvalidPayloadCase {
            R"(<c xmlns:x="urn:test" x:r="A1"><v>2</v></c>)",
            "must include an r attribute",
        },
        InvalidPayloadCase {
            R"(<c r="B1"><v>2</v></c>)",
            "must match its selector",
        },
    };

    for (const InvalidPayloadCase& invalid_case : invalid_cases) {
        bool failed = false;
        try {
            const std::array replacements {
                worksheet_cell_replacement(
                    "A1", invalid_case.materialized_replacement_cell_xml),
            };
            editor.replace_worksheet_cells_by_name("Sheet1", replacements);
        } catch (const std::exception& error) {
            failed = true;
            check_contains(error.what(), "replacement cell XML",
                "invalid cell replacement payload error should name replacement XML");
            check_contains(error.what(), invalid_case.expected_error,
                "invalid cell replacement payload error should explain the preflight failure");
        }
        check(failed, "invalid cell replacement payload should fail before Patch state changes");
        check_no_state_change();
    }

    std::string oversized_payload = R"(<c r="A1"><v>)";
    oversized_payload.append(
        fastxlsx::detail::worksheet_replacement_cell_xml_materialization_byte_limit, 'x');
    oversized_payload += R"(</v></c>)";
    bool oversized_failed = false;
    try {
        const std::array replacements {
            worksheet_cell_replacement("A1", oversized_payload),
        };
        editor.replace_worksheet_cells_by_name("Sheet1", replacements);
    } catch (const std::exception& error) {
        oversized_failed = true;
        check_contains(error.what(), "replacement cell XML",
            "oversized cell replacement payload error should name replacement XML");
        check_contains(error.what(), "single-cell materialized payload limit",
            "oversized cell replacement payload error should name the materialization limit");
    }
    check(oversized_failed,
        "oversized cell replacement payload should fail before Patch state changes");
    check_no_state_change();

    std::string oversized_chunk_prefix = R"(<c r="A1"><v>)";
    std::string oversized_chunk_body(
        fastxlsx::detail::worksheet_replacement_cell_xml_materialization_byte_limit, 'x');
    std::string oversized_chunk_suffix = R"(</v></c>)";
    const std::array<std::string_view, 3> oversized_chunks {
        oversized_chunk_prefix,
        oversized_chunk_body,
        oversized_chunk_suffix,
    };
    bool chunked_oversized_failed = false;
    try {
        const std::array replacements {
            chunked_worksheet_cell_replacement("A1", oversized_chunks),
        };
        editor.replace_worksheet_cells_by_name("Sheet1", replacements);
    } catch (const std::exception& error) {
        chunked_oversized_failed = true;
        check_contains(error.what(), "replacement cell XML",
            "oversized chunked cell replacement payload error should name replacement XML");
        check_contains(error.what(), "single-cell materialized payload limit",
            "oversized chunked cell replacement payload error should name the materialization limit");
    }
    check(chunked_oversized_failed,
        "oversized chunked cell replacement payload should fail before Patch state changes");
    check_no_state_change();

    struct StructurallyInvalidChunkedPayloadCase {
        std::array<std::string_view, 2> chunks;
        std::string_view expected_error;
        const char* message;
    };
    const std::array<StructurallyInvalidChunkedPayloadCase, 2> structurally_invalid_cases {{
        StructurallyInvalidChunkedPayloadCase {
            {R"(<c r="A1">)", "<v"},
            "tag is truncated",
            "truncated child tag",
        },
        StructurallyInvalidChunkedPayloadCase {
            {R"(<c r="A1">)", "<!--"},
            "comment is not closed",
            "unclosed comment",
        },
    }};

    for (const StructurallyInvalidChunkedPayloadCase& invalid_case :
         structurally_invalid_cases) {
        bool structure_failed = false;
        try {
            const std::array replacements {
                chunked_worksheet_cell_replacement("A1", invalid_case.chunks),
            };
            editor.replace_worksheet_cells_by_name("Sheet1", replacements);
        } catch (const std::exception& error) {
            structure_failed = true;
            check_contains(error.what(), "replacement cell XML",
                "invalid chunked cell replacement payload structure error should name XML");
            check_contains(error.what(), invalid_case.expected_error,
                "invalid chunked cell replacement payload structure error should explain failure");
        }
        check(structure_failed, invalid_case.message);
        check_no_state_change();
    }

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "invalid cell replacement payload output should preserve worksheet bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "invalid cell replacement payload output should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "invalid cell replacement payload output should preserve unknown bytes");
}

void test_package_editor_replaces_worksheet_and_removes_stale_calc_chain()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-calc-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-calc-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::string replacement_sheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>42</v></c></row></sheetData></worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_sheet);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "worksheet replacement should remain in the edit plan");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "worksheet replacement should be planned as stream-rewrite");
    const auto* worksheet_manifest_part = editor.manifest().find_part(worksheet_part);
    check(worksheet_manifest_part != nullptr,
        "worksheet replacement should keep worksheet in the manifest");
    check(worksheet_manifest_part->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "worksheet replacement manifest should mirror stream-rewrite mode");
    check(worksheet_manifest_part->dirty && !worksheet_manifest_part->preserve_original
            && !worksheet_manifest_part->generated,
        "worksheet replacement manifest should mark worksheet dirty");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "worksheet replacement should record workbook metadata rewrite");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "workbook calc metadata should be planned as local-DOM-rewrite");
    check(workbook_plan->reason.find("definedNames") != std::string::npos,
        "workbook rewrite plan should preserve definedNames review context");
    const auto* workbook_manifest_part = editor.manifest().find_part(workbook_part);
    check(workbook_manifest_part != nullptr,
        "worksheet replacement should keep workbook in the manifest");
    check(workbook_manifest_part->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "worksheet replacement manifest should mirror workbook metadata rewrite mode");
    check(workbook_manifest_part->dirty && !workbook_manifest_part->preserve_original
            && !workbook_manifest_part->generated,
        "worksheet replacement manifest should mark workbook metadata dirty");
    check(editor.edit_plan().full_calculation_on_load(),
        "worksheet replacement should request full calculation on load");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Remove,
        "worksheet replacement should record calcChain removal policy");
    const auto* removed_calc_chain_plan = editor.edit_plan().find_removed_part(calc_chain_part);
    check(removed_calc_chain_plan != nullptr,
        "worksheet replacement should record removed calcChain part");
    check(removed_calc_chain_plan->reason.find("worksheet rewrite") != std::string::npos,
        "worksheet replacement should retain calcChain removal planning reason");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "worksheet replacement should record content types entry rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "worksheet replacement content types entry should be local-DOM-rewrite");
    const auto* workbook_relationships_entry =
        editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels");
    check(workbook_relationships_entry != nullptr,
        "worksheet replacement should record workbook relationships entry rewrite");
    check(workbook_relationships_entry->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "worksheet replacement workbook relationships entry should be local-DOM-rewrite");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "worksheet replacement should remove calcChain from the output manifest");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load,
        "worksheet replacement output plan should expose full calculation request");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Remove,
        "worksheet replacement output plan should expose calcChain removal policy");
    check(has_note_containing(output_plan.notes, {"calcChain.xml"}),
        "worksheet replacement output plan should carry calcChain audit notes");
    check(output_plan.relationship_target_audits.empty(),
        "worksheet replacement output plan should not invent relationship target audits");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "worksheet replacement output plan should stream-rewrite worksheet");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/sheet1.xml", true,
        "/xl/worksheets/sheet1.xml",
        "worksheet replacement output plan should classify worksheet as package part");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "worksheet replacement output plan should local-DOM-rewrite workbook metadata");
    check_output_entry_part_context(output_plan.entries, "xl/workbook.xml", true,
        "/xl/workbook.xml",
        "worksheet replacement output plan should classify workbook as package part");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "worksheet replacement output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "worksheet replacement output plan should classify content types as metadata entry");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "worksheet replacement output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "worksheet replacement output plan should rewrite workbook relationships");
    check_output_entry_part_context(output_plan.entries, "xl/_rels/workbook.xml.rels", false, "",
        "worksheet replacement output plan should classify workbook relationships as metadata entry");
    const auto* output_workbook_relationships_plan =
        find_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels");
    check(output_workbook_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "worksheet replacement output plan should classify workbook source relationships");
    check(output_workbook_relationships_plan->owner_part == workbook_part.value(),
        "worksheet replacement output plan should keep workbook owner context");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "worksheet replacement output plan should omit stale calcChain");
    check_output_entry_part_context(output_plan.entries, "xl/calcChain.xml", true,
        "/xl/calcChain.xml",
        "worksheet replacement output plan should keep omitted calcChain as package part");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "worksheet replacement output plan should preserve unknown entry");
    check_output_entry_part_context(output_plan.entries, "custom/opaque.bin", true,
        "/custom/opaque.bin",
        "worksheet replacement output plan should classify unknown entry as package part");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "worksheet replacement output should omit stale calcChain.xml");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_sheet,
        "worksheet replacement should write the replacement worksheet XML");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "worksheet replacement should preserve unknown entry bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "worksheet replacement should preserve untouched package relationships bytes");

    const std::string content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(content_types, "calcChain+xml",
        "content types should no longer advertise calcChain.xml");
    check(output_reader.content_types().override_for(calc_chain_part) == nullptr,
        "parsed content types should not include a calcChain override");

    const std::string workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_not_contains(workbook_relationships, "relationships/calcChain",
        "workbook relationships should no longer point at calcChain.xml");
    const auto* parsed_workbook_relationships =
        output_reader.relationships_for(workbook_part);
    check(parsed_workbook_relationships != nullptr,
        "output workbook relationships should remain readable");
    check(parsed_workbook_relationships->find_by_id("rId1") != nullptr,
        "worksheet relationship should remain readable");
    check(parsed_workbook_relationships->find_by_id("rId2") == nullptr,
        "calcChain relationship id should be removed");

    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "workbook XML should request full calculation on load");
    check_not_contains(workbook_xml, R"(fullCalcOnLoad="0")",
        "workbook XML should replace stale fullCalcOnLoad value");
}

void test_package_editor_source_overwrite_rejection_preserves_worksheet_rewrite_plan()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-overwrite-calc-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-overwrite-calc-safe-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::string replacement_sheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>77</v></c></row></sheetData></worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_sheet);

    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    const std::size_t queued_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t queued_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t queued_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();

    bool failed = false;
    try {
        editor.save_as(source.path);
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed,
        "worksheet rewrite source-overwrite guard should reject saving over the source package");

    const std::filesystem::path equivalent_source_path =
        source.path.parent_path() / "." / source.path.filename();
    bool equivalent_failed = false;
    try {
        editor.save_as(equivalent_source_path);
    } catch (const std::exception&) {
        equivalent_failed = true;
    }
    check(equivalent_failed,
        "worksheet rewrite source-overwrite guard should reject path-equivalent source package");

    check(editor.edit_plan().size() == queued_plan_size,
        "worksheet rewrite source-overwrite rejection should not change edit-plan entries");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "worksheet rewrite source-overwrite rejection should not add edit-plan notes");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "worksheet rewrite source-overwrite rejection should not change package-entry audit");
    check(editor.edit_plan().removed_parts().size() == queued_removed_part_count,
        "worksheet rewrite source-overwrite rejection should not change removed-part audit");
    check(editor.edit_plan().removed_package_entries().size()
            == queued_removed_package_entry_count,
        "worksheet rewrite source-overwrite rejection should not change removed package entries");
    check(editor.edit_plan().full_calculation_on_load(),
        "worksheet rewrite source-overwrite rejection should keep fullCalcOnLoad intent");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Remove,
        "worksheet rewrite source-overwrite rejection should keep calcChain remove intent");

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr
            && worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "worksheet rewrite source-overwrite rejection should keep worksheet rewrite active");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr
            && workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "worksheet rewrite source-overwrite rejection should keep workbook metadata rewrite active");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "worksheet rewrite source-overwrite rejection should keep calcChain removal audit");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "worksheet rewrite source-overwrite rejection should keep worksheet manifest rewrite");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "worksheet rewrite source-overwrite rejection should keep workbook manifest rewrite");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "worksheet rewrite source-overwrite rejection should keep calcChain omitted from manifest");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load,
        "worksheet rewrite source-overwrite rejection should keep output fullCalcOnLoad snapshot");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Remove,
        "worksheet rewrite source-overwrite rejection should keep output calcChain snapshot");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "worksheet rewrite source-overwrite rejection should keep planned worksheet rewrite");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "worksheet rewrite source-overwrite rejection should keep planned workbook rewrite");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "worksheet rewrite source-overwrite rejection should keep planned calcChain omission");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "worksheet rewrite source-overwrite rejection should keep unknown part copy-original");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    check(source_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "worksheet rewrite source-overwrite rejection should preserve source worksheet bytes");
    check(source_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "worksheet rewrite source-overwrite rejection should preserve source calcChain bytes");
    check(source_reader.read_entry("custom/opaque.bin") == source.unknown,
        "worksheet rewrite source-overwrite rejection should preserve source unknown bytes");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "later safe worksheet rewrite output should omit stale calcChain.xml");
    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_sheet,
        "later safe worksheet rewrite output should write queued worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "later safe worksheet rewrite output should preserve unknown bytes");
    check_not_contains(output_reader.read_entry("[Content_Types].xml"), "calcChain+xml",
        "later safe worksheet rewrite output should remove calcChain content type");
    check_not_contains(output_reader.read_entry("xl/_rels/workbook.xml.rels"),
        "relationships/calcChain",
        "later safe worksheet rewrite output should remove calcChain workbook relationship");
    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "later safe worksheet rewrite output should keep fullCalcOnLoad");
}

void test_package_editor_cleans_stale_calc_chain_metadata_without_payload()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-editor-calc-metadata-only-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-calc-metadata-only-output.xlsx");

    const std::string content_types = R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/calcChain.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.calcChain+xml"/>)"
        R"(</Types>)";
    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="calcChain.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<calcPr calcId="1" fullCalcOnLoad="0"/>)"
        R"(</workbook>)";
    const std::string worksheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><f>SUM(B1:C1)</f><v>3</v></c></row></sheetData></worksheet>)";
    const std::string unknown = std::string("stale-calc\0unknown", 19);

    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", workbook},
            {"xl/_rels/workbook.xml.rels", workbook_relationships},
            {"xl/worksheets/sheet1.xml", worksheet},
            {"custom/opaque.bin", unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source_path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    check(editor.reader().find_entry("xl/calcChain.xml") == nullptr,
        "stale calcChain fixture should not contain calcChain payload");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "stale calcChain fixture should not register calcChain as a part");
    check(editor.manifest().content_types().override_for(calc_chain_part) != nullptr,
        "stale calcChain fixture should retain the source calcChain content type override");

    const std::string replacement_sheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>55</v></c></row></sheetData></worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_sheet);

    check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
        "metadata-only calcChain cleanup should not invent a removed-part audit");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "metadata-only calcChain cleanup should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "metadata-only calcChain content types rewrite should be local-DOM-rewrite");
    const auto* workbook_relationships_entry =
        editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels");
    check(workbook_relationships_entry != nullptr,
        "metadata-only calcChain cleanup should record workbook relationships rewrite");
    check(workbook_relationships_entry->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "metadata-only calcChain workbook relationships rewrite should be local-DOM-rewrite");
    check(editor.manifest().content_types().override_for(calc_chain_part) == nullptr,
        "metadata-only calcChain cleanup should remove stale manifest content type override");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "metadata-only calcChain cleanup should still not invent calcChain as a part");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "metadata-only calcChain cleanup output should not create calcChain payload");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_sheet,
        "metadata-only calcChain cleanup output should write replacement worksheet XML");
    check(output_reader.read_entry("custom/opaque.bin") == unknown,
        "metadata-only calcChain cleanup output should preserve unknown bytes");
    const std::string output_content_types =
        output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "calcChain+xml",
        "metadata-only calcChain cleanup output should remove stale calcChain content type");
    check(output_reader.content_types().override_for(calc_chain_part) == nullptr,
        "metadata-only calcChain cleanup parsed content types should omit calcChain override");
    const std::string output_workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_not_contains(output_workbook_relationships, "relationships/calcChain",
        "metadata-only calcChain cleanup output should remove stale calcChain relationship");
    const auto* parsed_workbook_relationships =
        output_reader.relationships_for(workbook_part);
    check(parsed_workbook_relationships != nullptr,
        "metadata-only calcChain cleanup workbook relationships should remain readable");
    check(parsed_workbook_relationships->find_by_id("rId1") != nullptr,
        "metadata-only calcChain cleanup should keep worksheet relationship id");
    check(parsed_workbook_relationships->find_by_id("rId2") == nullptr,
        "metadata-only calcChain cleanup should remove stale calcChain relationship id");
    const std::string output_workbook = output_reader.read_entry("xl/workbook.xml");
    check_contains(output_workbook, R"(fullCalcOnLoad="1")",
        "metadata-only calcChain cleanup output should request full calculation");
    check_not_contains(output_workbook, R"(fullCalcOnLoad="0")",
        "metadata-only calcChain cleanup output should replace stale fullCalcOnLoad value");
}

void test_package_editor_worksheet_rewrite_omits_prior_calc_chain_replacement()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-calc-prior-replacement-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-calc-prior-replacement-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::string prior_calc_chain =
        R"(<calcChain><c r="Z99" i="2"/></calcChain>)";
    replace_part_with_memory_chunks(editor, calc_chain_part, prior_calc_chain,
        "prior ordinary calcChain replacement");

    const std::string replacement_sheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>77</v></c></row></sheetData></worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_sheet);

    check(editor.edit_plan().find_part(calc_chain_part) == nullptr,
        "worksheet rewrite should remove prior calcChain replacement from the edit plan");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "worksheet rewrite should record calcChain removal over prior replacement");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "worksheet rewrite should remove calcChain from the manifest over prior replacement");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Remove,
        "worksheet rewrite should keep calcChain removal policy over prior replacement");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "worksheet rewrite output should omit prior calcChain replacement bytes");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_sheet,
        "worksheet rewrite output should write replacement worksheet XML");
    const std::string content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(content_types, "calcChain+xml",
        "worksheet rewrite output should remove calcChain content type over prior replacement");
    const std::string workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_not_contains(workbook_relationships, "relationships/calcChain",
        "worksheet rewrite output should remove calcChain relationship over prior replacement");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "worksheet rewrite output should preserve unknown bytes over prior replacement");
}

void test_package_editor_worksheet_rewrite_preserves_prior_workbook_replacement()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-workbook-prior-replacement-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-workbook-prior-replacement-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::string prior_workbook =
        R"(<workbook><sheets><sheet name="PriorWorkbook" sheetId="1"/></sheets></workbook>)";
    editor.replace_part(workbook_part, prior_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "prior ordinary workbook replacement");

    const std::string replacement_sheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>88</v></c></row></sheetData></worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_sheet);

    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "worksheet rewrite should keep workbook metadata in the edit plan");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "worksheet rewrite should keep workbook metadata as local-DOM-rewrite");
    check(workbook_plan->reason.find("fullCalcOnLoad updated for worksheet rewrite")
            != std::string::npos,
        "worksheet rewrite should keep workbook calc metadata rewrite reason");
    const auto* workbook_relationships_entry =
        editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels");
    check(workbook_relationships_entry != nullptr,
        "worksheet rewrite should keep workbook relationships audit");
    check(workbook_relationships_entry->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "worksheet rewrite should replace prior copy-original workbook rels audit");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "worksheet rewrite should still record calcChain removal");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_sheet,
        "worksheet rewrite output should write replacement worksheet XML");
    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "worksheet rewrite output should request full calculation on load");
    check_contains(workbook_xml, "PriorWorkbook",
        "worksheet rewrite output should preserve prior ordinary workbook bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "worksheet rewrite output should preserve unknown bytes over prior workbook replacement");
}

void test_package_editor_workbook_replacement_after_worksheet_rewrite_keeps_calc_policy()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-workbook-later-replacement-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-workbook-later-replacement-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::string replacement_sheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>99</v></c></row></sheetData></worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_sheet);

    const std::string later_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="LaterWorkbook" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<calcPr calcId="7" fullCalcOnLoad="0"/>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, later_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "ordinary workbook replacement after worksheet rewrite");

    check(editor.edit_plan().full_calculation_on_load(),
        "later workbook replacement should keep worksheet rewrite recalculation request");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Remove,
        "later workbook replacement should keep worksheet rewrite calcChain removal policy");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "later workbook replacement should keep calcChain removed-part audit");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "later workbook replacement should keep workbook in the edit plan");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "later workbook replacement should keep workbook as local-DOM-rewrite");
    check(workbook_plan->reason.find("ordinary workbook replacement after worksheet rewrite")
            != std::string::npos,
        "later workbook replacement should keep ordinary replacement reason");
    const auto* workbook_relationships_entry =
        editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels");
    check(workbook_relationships_entry != nullptr,
        "later workbook replacement should keep workbook relationships audit");
    check(workbook_relationships_entry->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "later workbook replacement should not downgrade rewritten workbook relationships audit");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "later workbook replacement should keep calcChain removed from the manifest");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "later workbook replacement output should still omit calcChain.xml");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_sheet,
        "later workbook replacement output should keep replacement worksheet XML");
    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, "LaterWorkbook",
        "later workbook replacement output should use the later workbook bytes");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "later workbook replacement output should preserve recalculation metadata");
    check_not_contains(workbook_xml, R"(fullCalcOnLoad="0")",
        "later workbook replacement output should not restore stale recalculation metadata");
    const std::string workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_not_contains(workbook_relationships, "relationships/calcChain",
        "later workbook replacement output should keep calcChain relationship removed");
    check_not_contains(output_reader.read_entry("[Content_Types].xml"), "calcChain+xml",
        "later workbook replacement output should keep calcChain content type removed");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "later workbook replacement output should preserve unknown bytes");
}

void test_package_editor_removes_stale_calc_chain_relationship_part()
{
    CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-calc-rels-source.xlsx");
    const std::string calc_chain_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(</Relationships>)";
    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"xl/calcChain.xml", source.calc_chain},
            {"xl/_rels/calcChain.xml.rels", calc_chain_relationships},
            {"custom/opaque.bin", source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-calc-rels-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::string replacement_sheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>44</v></c></row></sheetData></worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_sheet);
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "calcChain relationship-part fixture should still record calcChain removal");
    const auto* removed_calc_chain_relationships_plan =
        editor.edit_plan().find_removed_package_entry("xl/_rels/calcChain.xml.rels");
    check(removed_calc_chain_relationships_plan != nullptr,
        "calcChain relationship-part fixture should record removed calcChain relationships entry");
    check(removed_calc_chain_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "removed calcChain relationships audit should keep structured source-relationships role");
    check(removed_calc_chain_relationships_plan->owner_part == calc_chain_part.value(),
        "removed calcChain relationships audit should keep structured owner part");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "calcChain relationship-part output should omit calcChain.xml");
    check(entries.find("xl/_rels/calcChain.xml.rels") == entries.end(),
        "calcChain relationship-part output should omit stale calcChain relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_sheet,
        "calcChain relationship-part output should write replacement worksheet XML");
    check(output_reader.relationships_for(calc_chain_part) == nullptr,
        "calcChain relationship-part output should not parse relationships for removed calcChain");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "calcChain relationship-part output should preserve unknown bytes");
}

void test_package_editor_replaces_worksheet_and_preserves_calc_chain_when_requested()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-preserve-calc-source.xlsx");
    const std::string calc_chain_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(</Relationships>)";
    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"xl/calcChain.xml", source.calc_chain},
            {"xl/_rels/calcChain.xml.rels", calc_chain_relationships},
            {"custom/opaque.bin", source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-preserve-calc-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    fastxlsx::detail::ReferencePolicy policy;
    policy.calc_chain_action = fastxlsx::detail::CalcChainAction::Preserve;

    const std::string replacement_sheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>7</v></c></row></sheetData></worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_sheet, policy);

    check(editor.edit_plan().full_calculation_on_load(),
        "calcChain preserve policy should still request full calculation by default");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "calcChain preserve policy should remain visible in the edit plan");
    check(editor.edit_plan().removed_parts().empty(),
        "calcChain preserve policy should not record removed parts");
    check(editor.manifest().find_part(calc_chain_part) != nullptr,
        "calcChain preserve policy should keep calcChain in the output manifest");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "calcChain preserve policy should still record workbook metadata rewrite");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "calcChain preserve policy should local-DOM-rewrite workbook metadata");
    const auto* workbook_manifest_part = editor.manifest().find_part(workbook_part);
    check(workbook_manifest_part != nullptr,
        "calcChain preserve policy should keep workbook in the manifest");
    check(workbook_manifest_part->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "calcChain preserve policy manifest should mirror workbook metadata rewrite mode");
    check(workbook_manifest_part->dirty && !workbook_manifest_part->preserve_original
            && !workbook_manifest_part->generated,
        "calcChain preserve policy manifest should mark workbook metadata dirty");
    const auto* calc_chain_relationships_plan =
        editor.edit_plan().find_package_entry("xl/_rels/calcChain.xml.rels");
    check(calc_chain_relationships_plan != nullptr,
        "calcChain preserve policy should audit preserved calcChain relationships");
    check(calc_chain_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "preserved calcChain relationships should be copy-original in package-entry audit");
    check(calc_chain_relationships_plan->reason.find("/xl/calcChain.xml")
            != std::string::npos,
        "preserved calcChain relationships audit should name the owner part");
    check(calc_chain_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "preserved calcChain relationships audit should keep structured source-relationships role");
    check(calc_chain_relationships_plan->owner_part == calc_chain_part.value(),
        "preserved calcChain relationships audit should keep structured owner part");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") != entries.end(),
        "calcChain preserve output should keep calcChain.xml");
    check(entries.find("xl/_rels/calcChain.xml.rels") != entries.end(),
        "calcChain preserve output should keep calcChain relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_sheet,
        "calcChain preserve output should replace worksheet XML");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "calcChain preserve output should preserve calcChain bytes");
    check(output_reader.read_entry("xl/_rels/calcChain.xml.rels")
            == calc_chain_relationships,
        "calcChain preserve output should preserve calcChain relationships bytes");
    check(output_reader.relationships_for(calc_chain_part) != nullptr,
        "calcChain preserve output should keep parsed calcChain relationships");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "calcChain preserve output should preserve content types bytes");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "calcChain preserve output should keep parsed calcChain content type");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "calcChain preserve output should preserve workbook relationships bytes");
    const auto* parsed_workbook_relationships =
        output_reader.relationships_for(workbook_part);
    check(parsed_workbook_relationships != nullptr,
        "calcChain preserve output workbook relationships should remain readable");
    check(parsed_workbook_relationships->find_by_id("rId2") != nullptr,
        "calcChain preserve output should keep calcChain relationship id");

    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "calcChain preserve output should still request full calculation on load");
    check_not_contains(workbook_xml, R"(fullCalcOnLoad="0")",
        "calcChain preserve output should replace stale fullCalcOnLoad value");
}

void test_package_editor_worksheet_rewrite_preserves_prior_calc_chain_replacement_when_requested()
{
    CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-preserve-prior-calc-source.xlsx");
    const std::string calc_chain_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(</Relationships>)";
    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"xl/calcChain.xml", source.calc_chain},
            {"xl/_rels/calcChain.xml.rels", calc_chain_relationships},
            {"custom/opaque.bin", source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-preserve-prior-calc-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::string prior_calc_chain =
        R"(<calcChain><c r="Z99" i="8"/></calcChain>)";
    replace_part_with_memory_chunks(editor, calc_chain_part, prior_calc_chain,
        "prior calcChain replacement before worksheet preserve rewrite");

    fastxlsx::detail::ReferencePolicy policy;
    policy.calc_chain_action = fastxlsx::detail::CalcChainAction::Preserve;

    const std::string replacement_sheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>17</v></c></row></sheetData></worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_sheet, policy);

    check(editor.edit_plan().full_calculation_on_load(),
        "prior preserve worksheet rewrite should request full calculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "prior preserve worksheet rewrite should expose preserve policy");
    check(editor.edit_plan().removed_parts().empty(),
        "prior preserve worksheet rewrite should not record removed parts");
    const auto* calc_chain_plan = editor.edit_plan().find_part(calc_chain_part);
    check(calc_chain_plan != nullptr,
        "prior preserve worksheet rewrite should keep calcChain in the edit plan");
    check(calc_chain_plan->write_mode
            == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "prior preserve worksheet rewrite should keep prior staged calcChain replacement active");
    check(calc_chain_plan->reason.find("prior calcChain replacement") != std::string::npos,
        "prior preserve worksheet rewrite should keep prior calcChain replacement reason");
    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr
            && worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "prior preserve worksheet rewrite should stream-rewrite the worksheet");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr
            && workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "prior preserve worksheet rewrite should local-DOM-rewrite workbook metadata");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "prior preserve worksheet rewrite should mirror worksheet stream-rewrite in manifest");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "prior preserve worksheet rewrite should mirror workbook local-DOM-rewrite in manifest");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "prior preserve worksheet rewrite should keep calcChain stream-rewrite in manifest");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "prior preserve worksheet rewrite should not rewrite content types");

    const auto* workbook_relationships_plan =
        editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels");
    check(workbook_relationships_plan != nullptr
            && workbook_relationships_plan->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "prior preserve worksheet rewrite should audit workbook relationships preservation");
    const auto* calc_chain_relationships_plan =
        editor.edit_plan().find_package_entry("xl/_rels/calcChain.xml.rels");
    check(calc_chain_relationships_plan != nullptr,
        "prior preserve worksheet rewrite should audit calcChain relationships preservation");
    check(calc_chain_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "prior preserve calcChain relationships should be copy-original");
    check(calc_chain_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "prior preserve calcChain relationships audit should keep source-relationships role");
    check(calc_chain_relationships_plan->owner_part == calc_chain_part.value(),
        "prior preserve calcChain relationships audit should keep calcChain owner part");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") != entries.end(),
        "prior preserve worksheet output should keep calcChain.xml");
    check(entries.find("xl/_rels/calcChain.xml.rels") != entries.end(),
        "prior preserve worksheet output should keep calcChain relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_sheet,
        "prior preserve worksheet output should replace worksheet XML");
    check(output_reader.read_entry("xl/calcChain.xml") == prior_calc_chain,
        "prior preserve worksheet output should use queued calcChain replacement bytes");
    check(output_reader.read_entry("xl/_rels/calcChain.xml.rels")
            == calc_chain_relationships,
        "prior preserve worksheet output should preserve calcChain relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "prior preserve worksheet output should preserve content types bytes");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "prior preserve worksheet output should keep parsed calcChain content type");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "prior preserve worksheet output should preserve workbook relationships bytes");
    check(output_reader.relationships_for(calc_chain_part) != nullptr,
        "prior preserve worksheet output should keep parsed calcChain relationships");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "prior preserve worksheet output should preserve unknown bytes");

    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "prior preserve worksheet output should request full calculation on load");
    check_not_contains(workbook_xml, R"(fullCalcOnLoad="0")",
        "prior preserve worksheet output should replace stale fullCalcOnLoad value");
}

void test_package_editor_preserve_calc_chain_does_not_audit_missing_relationship_part()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-calc-preserve-no-rels-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-calc-preserve-no-rels-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    fastxlsx::detail::ReferencePolicy policy;
    policy.calc_chain_action = fastxlsx::detail::CalcChainAction::Preserve;

    const std::string replacement_sheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>77</v></c></row></sheetData></worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_sheet, policy);

    check(editor.edit_plan().find_package_entry("xl/_rels/calcChain.xml.rels") == nullptr,
        "calcChain preserve should not audit a missing calcChain relationships entry");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/calcChain.xml.rels") == nullptr,
        "calcChain preserve should not omit a missing calcChain relationships entry");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "calcChain preserve without owner relationships should still keep calcChain copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") != entries.end(),
        "calcChain preserve without owner relationships should keep calcChain.xml");
    check(entries.find("xl/_rels/calcChain.xml.rels") == entries.end(),
        "calcChain preserve should not create a missing calcChain relationships entry");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_sheet,
        "calcChain preserve without owner relationships should replace worksheet XML");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "calcChain preserve without owner relationships should preserve calcChain bytes");
    check(output_reader.relationships_for(calc_chain_part) == nullptr,
        "calcChain preserve without owner relationships should keep relationships absent");
}

void test_package_editor_request_full_calculation_removes_calc_chain_only()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-workbook-calc-remove-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-workbook-calc-remove-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");
    using WorkbookAuditKind = fastxlsx::detail::WorkbookPayloadDependencyAuditKind;
    using WorkbookAuditScope = fastxlsx::detail::WorkbookPayloadDependencyAuditScope;

    editor.request_full_calculation();

    check(editor.edit_plan().full_calculation_on_load(),
        "workbook calc helper should request full calculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Remove,
        "workbook calc helper should default to calcChain removal");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "workbook calc helper should record workbook metadata rewrite");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "workbook calc helper should plan workbook as local-DOM-rewrite");
    check(workbook_plan->reason.find("definedNames") != std::string::npos,
        "workbook calc helper should retain definedNames review context");
    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr
            && worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook calc helper should leave worksheet copy-original");
    const auto* removed_calc_chain = editor.edit_plan().find_removed_part(calc_chain_part);
    check(removed_calc_chain != nullptr,
        "workbook calc helper should record calcChain removed-part audit");
    check(removed_calc_chain->reason.find("full calculation") != std::string::npos,
        "workbook calc helper should explain calcChain removal reason");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "workbook calc helper should remove calcChain from manifest");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "workbook calc helper should mirror workbook local-DOM-rewrite in manifest");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook calc helper should keep worksheet manifest copy-original");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") != nullptr,
        "workbook calc helper should audit content types rewrite");
    const auto* workbook_relationships_entry =
        editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels");
    check(workbook_relationships_entry != nullptr
            && workbook_relationships_entry->write_mode
                == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "workbook calc helper should audit workbook relationships rewrite");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_materialized_replacement(output_plan.entries,
        workbook_part.zip_path(), true,
        "workbook calc helper output plan should mark workbook as active materialized replacement");
    check_output_entry_materialized_replacement_reason(output_plan.entries,
        workbook_part.zip_path(), "workbook small-XML package part",
        "workbook calc helper output plan should explain workbook small-XML materialization");
    check_output_entry_staged_replacement_chunks(output_plan.entries,
        workbook_part.zip_path(), false,
        "workbook calc helper output plan should not mark workbook as staged chunks");
    check_output_entry_part_context(output_plan.entries,
        workbook_part.zip_path(), true, workbook_part.value(),
        "workbook calc helper output plan should preserve workbook package-part context");
    check_output_entry_materialized_replacement(output_plan.entries,
        "[Content_Types].xml", true,
        "workbook calc helper output plan should mark content types metadata replacement");
    check_output_entry_materialized_replacement_reason(output_plan.entries,
        "[Content_Types].xml", "content types metadata",
        "workbook calc helper output plan should explain content types metadata materialization");
    check_output_entry_part_context(output_plan.entries,
        "[Content_Types].xml", false, "",
        "workbook calc helper output plan should keep content types as metadata entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "workbook calc helper output should omit calcChain.xml");
    check(entries.find("xl/worksheets/sheet1.xml") != entries.end(),
        "workbook calc helper output should keep worksheet entry");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "workbook calc helper output should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "workbook calc helper output should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "workbook calc helper output should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "workbook calc helper output should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "workbook calc helper output should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "workbook calc helper output should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "workbook calc helper output should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "workbook calc helper output should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "workbook calc helper output should preserve VBA bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin") == source.opaque_extension,
        "workbook calc helper output should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "workbook calc helper output should preserve unknown extension relationships bytes");

    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "workbook calc helper output should request full calculation");
    check_contains(workbook_xml,
        R"(<definedNames><definedName name="ReportRange">Sheet1!$A$1:$B$2</definedName></definedNames>)",
        "workbook calc helper output should preserve workbook defined names");

    const std::string content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(content_types, "calcChain+xml",
        "workbook calc helper output should remove calcChain content type");
    check_contains(content_types, R"(Default Extension="png" ContentType="image/png")",
        "workbook calc helper output should preserve PNG default content type");
    check_not_contains(content_types, R"(PartName="/xl/media/image1.png")",
        "workbook calc helper output should not promote PNG media default to override");

    const std::string workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_not_contains(workbook_relationships, "relationships/calcChain",
        "workbook calc helper output should remove calcChain workbook relationship");
    check_contains(workbook_relationships, "relationships/sharedStrings",
        "workbook calc helper output should preserve sharedStrings workbook relationship");
    check_contains(workbook_relationships, "relationships/styles",
        "workbook calc helper output should preserve styles workbook relationship");
    check_contains(workbook_relationships, "relationships/vbaProject",
        "workbook calc helper output should preserve VBA workbook relationship");
}

void test_package_editor_request_full_calculation_uses_prior_workbook_replacement()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-workbook-calc-prior-workbook-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-workbook-calc-prior-workbook-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::string prior_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="QueuedWorkbook" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<definedNames><definedName name="QueuedRange">QueuedWorkbook!$A$1:$A$1</definedName></definedNames>)"
        R"(<calcPr calcId="77" fullCalcOnLoad="0"/>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, prior_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "prior workbook replacement before workbook calc helper");

    editor.request_full_calculation();

    check(editor.edit_plan().full_calculation_on_load(),
        "prior workbook calc helper should request full calculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Remove,
        "prior workbook calc helper should keep calcChain removal policy");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "prior workbook calc helper should keep workbook in the edit plan");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "prior workbook calc helper should keep workbook as local-DOM-rewrite");
    check(workbook_plan->reason.find("workbook metadata helper") != std::string::npos,
        "prior workbook calc helper should take ownership of the workbook rewrite reason");
    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr
            && worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "prior workbook calc helper should leave worksheet copy-original");
    check(editor.edit_plan().find_part(calc_chain_part) == nullptr,
        "prior workbook calc helper should remove active calcChain copy decision");
    const auto* removed_calc_chain = editor.edit_plan().find_removed_part(calc_chain_part);
    check(removed_calc_chain != nullptr,
        "prior workbook calc helper should record calcChain removal");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "prior workbook calc helper should remove calcChain from manifest");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "prior workbook calc helper should mirror workbook local-DOM-rewrite in manifest");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "prior workbook calc helper should keep worksheet manifest copy-original");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr
            && content_types_entry->write_mode
                == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "prior workbook calc helper should audit content types rewrite");
    const auto* workbook_relationships_entry =
        editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels");
    check(workbook_relationships_entry != nullptr
            && workbook_relationships_entry->write_mode
                == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "prior workbook calc helper should rewrite workbook relationships for calcChain removal");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "prior workbook calc helper output should omit calcChain.xml");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "prior workbook calc helper output should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "prior workbook calc helper output should preserve unknown bytes");

    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, "QueuedWorkbook",
        "prior workbook calc helper output should use queued workbook bytes");
    check_contains(workbook_xml, "QueuedRange",
        "prior workbook calc helper output should preserve queued defined names");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "prior workbook calc helper output should request full calculation");
    check_not_contains(workbook_xml, R"(fullCalcOnLoad="0")",
        "prior workbook calc helper output should remove stale fullCalcOnLoad value");
    check_not_contains(workbook_xml, R"(sheet name="Sheet1")",
        "prior workbook calc helper output should not fall back to source workbook bytes");

    const std::string content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(content_types, "calcChain+xml",
        "prior workbook calc helper output should remove calcChain content type");
    const std::string workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_not_contains(workbook_relationships, "relationships/calcChain",
        "prior workbook calc helper output should remove calcChain workbook relationship");
}

void test_package_editor_request_full_calculation_updates_direct_child_calc_pr_only()
{
    CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-workbook-calc-direct-child-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-workbook-calc-direct-child-output.xlsx");
    source.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<extLst><ext uri="{fastxlsx-decoy}"><calcPr calcId="999" fullCalcOnLoad="0"/></ext></extLst>)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<definedNames><definedName name="DirectCalcRange">Sheet1!$A$1:$A$1</definedName></definedNames>)"
        R"(<calcPr calcId="77" fullCalcOnLoad="0"/>)"
        R"(</workbook>)";
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

    editor.request_full_calculation();
    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "direct-child calcPr helper output should omit stale calcChain.xml");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml,
        R"(<extLst><ext uri="{fastxlsx-decoy}"><calcPr calcId="999" fullCalcOnLoad="0"/></ext></extLst>)",
        "workbook calc helper should preserve nested calcPr decoys");
    check_contains(workbook_xml, R"(<calcPr calcId="77" fullCalcOnLoad="1"/>)",
        "workbook calc helper should update only the workbook direct-child calcPr");
    check_not_contains(workbook_xml, R"(<calcPr calcId="77" fullCalcOnLoad="0"/>)",
        "workbook calc helper should not leave the direct-child calcPr stale");
    check_contains(workbook_xml, "DirectCalcRange",
        "workbook calc helper should preserve non-calc workbook metadata");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "direct-child calcPr helper output should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "direct-child calcPr helper output should preserve unknown bytes");
}

void test_package_editor_request_full_calculation_inserts_direct_calc_pr_when_only_nested_exists()
{
    CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-workbook-calc-nested-only-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-workbook-calc-nested-only-output.xlsx");
    source.workbook =
        R"(<x:workbook xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<x:sheets><x:sheet name="Sheet1" sheetId="1" r:id="rId1"/></x:sheets>)"
        R"(<x:extLst><x:ext uri="{fastxlsx-nested-only}"><x:calcPr calcId="999" fullCalcOnLoad="0"/></x:ext></x:extLst>)"
        R"(</x:workbook>)";
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

    editor.request_full_calculation();
    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "nested-only calcPr helper output should omit stale calcChain.xml");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml,
        R"(<x:calcPr calcId="999" fullCalcOnLoad="0"/>)",
        "workbook calc helper should preserve nested prefixed calcPr decoys");
    check_not_contains(workbook_xml, R"(<x:calcPr calcId="999" fullCalcOnLoad="1"/>)",
        "workbook calc helper should not update nested prefixed calcPr decoys");
    check_contains(workbook_xml,
        R"(<x:calcPr calcId="124519" fullCalcOnLoad="1"/></x:workbook>)",
        "workbook calc helper should insert a prefixed direct-child calcPr");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "nested-only calcPr helper output should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "nested-only calcPr helper output should preserve unknown bytes");
}

void test_package_editor_request_full_calculation_omits_prior_calc_chain_replacement()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-workbook-calc-prior-chain-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-workbook-calc-prior-chain-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::string prior_calc_chain =
        R"(<calcChain><c r="Z99" i="9"/></calcChain>)";
    replace_part_with_memory_chunks(editor, calc_chain_part, prior_calc_chain,
        "prior calcChain replacement before workbook calc helper");

    editor.request_full_calculation();

    check(editor.edit_plan().full_calculation_on_load(),
        "prior calcChain helper should request full calculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Remove,
        "prior calcChain helper should keep calcChain removal policy");
    check(editor.edit_plan().find_part(calc_chain_part) == nullptr,
        "prior calcChain helper should remove active calcChain replacement");
    const auto* removed_calc_chain = editor.edit_plan().find_removed_part(calc_chain_part);
    check(removed_calc_chain != nullptr,
        "prior calcChain helper should record calcChain removed-part audit");
    check(removed_calc_chain->reason.find("full calculation") != std::string::npos,
        "prior calcChain helper should explain calcChain removal");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr
            && workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "prior calcChain helper should local-DOM-rewrite workbook metadata");
    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr
            && worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "prior calcChain helper should leave worksheet copy-original");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "prior calcChain helper should remove calcChain from manifest");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "prior calcChain helper should mirror workbook local-DOM-rewrite in manifest");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "prior calcChain helper should keep worksheet manifest copy-original");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") != nullptr,
        "prior calcChain helper should audit content types rewrite");
    check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels") != nullptr,
        "prior calcChain helper should audit workbook relationships rewrite");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/calcChain.xml.rels")
            == nullptr,
        "prior calcChain helper should not invent missing calcChain relationships omission");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "prior calcChain helper output should omit prior calcChain replacement");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "prior calcChain helper output should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "prior calcChain helper output should preserve unknown bytes");

    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "prior calcChain helper output should request full calculation");
    check_not_contains(workbook_xml, R"(fullCalcOnLoad="0")",
        "prior calcChain helper output should remove stale fullCalcOnLoad value");
    const std::string content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(content_types, "calcChain+xml",
        "prior calcChain helper output should remove calcChain content type");
    const std::string workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_not_contains(workbook_relationships, "relationships/calcChain",
        "prior calcChain helper output should remove calcChain workbook relationship");
}

void test_package_editor_request_full_calculation_cleans_metadata_only_calc_chain()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-editor-workbook-calc-metadata-only-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-workbook-calc-metadata-only-output.xlsx");

    const std::string content_types = R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/calcChain.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.calcChain+xml"/>)"
        R"(</Types>)";
    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="calcChain.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<definedNames><definedName name="ReportRange">Sheet1!$A$1:$B$2</definedName></definedNames>)"
        R"(<calcPr calcId="1" fullCalcOnLoad="0"/>)"
        R"(</workbook>)";
    const std::string worksheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><f>SUM(B1:C1)</f><v>3</v></c></row></sheetData></worksheet>)";
    const std::string unknown = std::string("workbook-calc\0metadata-only", 27);

    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", workbook},
            {"xl/_rels/workbook.xml.rels", workbook_relationships},
            {"xl/worksheets/sheet1.xml", worksheet},
            {"custom/opaque.bin", unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source_path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    check(editor.reader().find_entry("xl/calcChain.xml") == nullptr,
        "workbook calc metadata-only fixture should not contain calcChain payload");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "workbook calc metadata-only fixture should not register calcChain as a part");
    check(editor.manifest().content_types().override_for(calc_chain_part) != nullptr,
        "workbook calc metadata-only fixture should retain stale calcChain content type");

    editor.request_full_calculation();

    check(editor.edit_plan().full_calculation_on_load(),
        "workbook calc metadata-only helper should request full calculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Remove,
        "workbook calc metadata-only helper should keep remove policy");
    check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
        "workbook calc metadata-only helper should not invent removed-part audit");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr
            && workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "workbook calc metadata-only helper should local-DOM-rewrite workbook metadata");
    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr
            && worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook calc metadata-only helper should leave worksheet copy-original");
    const auto* content_types_plan =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_plan != nullptr
            && content_types_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "workbook calc metadata-only helper should audit content types rewrite");
    const auto* workbook_relationships_plan =
        editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels");
    check(workbook_relationships_plan != nullptr
            && workbook_relationships_plan->write_mode
                == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "workbook calc metadata-only helper should audit workbook relationships rewrite");
    check(editor.manifest().content_types().override_for(calc_chain_part) == nullptr,
        "workbook calc metadata-only helper should remove stale manifest content type");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "workbook calc metadata-only helper should still not invent calcChain as a part");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "workbook calc metadata-only output should not create calcChain payload");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == worksheet,
        "workbook calc metadata-only output should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == unknown,
        "workbook calc metadata-only output should preserve unknown bytes");
    const std::string output_workbook = output_reader.read_entry("xl/workbook.xml");
    check_contains(output_workbook, R"(fullCalcOnLoad="1")",
        "workbook calc metadata-only output should request full calculation");
    check_not_contains(output_workbook, R"(fullCalcOnLoad="0")",
        "workbook calc metadata-only output should remove stale fullCalcOnLoad value");
    check_contains(output_workbook,
        R"(<definedNames><definedName name="ReportRange">Sheet1!$A$1:$B$2</definedName></definedNames>)",
        "workbook calc metadata-only output should preserve defined names");
    const std::string output_content_types =
        output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "calcChain+xml",
        "workbook calc metadata-only output should remove stale calcChain content type");
    check(output_reader.content_types().override_for(calc_chain_part) == nullptr,
        "workbook calc metadata-only parsed content types should omit calcChain override");
    const std::string output_workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_not_contains(output_workbook_relationships, "relationships/calcChain",
        "workbook calc metadata-only output should remove stale calcChain relationship");
    const auto* parsed_workbook_relationships =
        output_reader.relationships_for(workbook_part);
    check(parsed_workbook_relationships != nullptr,
        "workbook calc metadata-only output should keep workbook relationships readable");
    check(parsed_workbook_relationships->find_by_id("rId1") != nullptr,
        "workbook calc metadata-only output should keep worksheet relationship");
    check(parsed_workbook_relationships->find_by_id("rId2") == nullptr,
        "workbook calc metadata-only output should remove stale calcChain relationship id");
}

void test_package_editor_request_full_calculation_preserves_calc_chain_when_requested()
{
    CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-workbook-calc-preserve-source.xlsx");
    const std::string calc_chain_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(</Relationships>)";
    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"xl/calcChain.xml", source.calc_chain},
            {"xl/_rels/calcChain.xml.rels", calc_chain_relationships},
            {"custom/opaque.bin", source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-workbook-calc-preserve-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    editor.request_full_calculation(fastxlsx::detail::CalcChainAction::Preserve);

    check(editor.edit_plan().full_calculation_on_load(),
        "workbook calc preserve helper should request full calculation");
    check(editor.edit_plan().calc_chain_action()
            == fastxlsx::detail::CalcChainAction::Preserve,
        "workbook calc preserve helper should expose preserve policy");
    check(editor.edit_plan().removed_parts().empty(),
        "workbook calc preserve helper should not record removed parts");
    check(editor.manifest().find_part(calc_chain_part) != nullptr,
        "workbook calc preserve helper should keep calcChain in manifest");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "workbook calc preserve helper should rewrite workbook metadata");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook calc preserve helper should keep calcChain copy-original");

    const auto* workbook_relationships_entry =
        editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels");
    check(workbook_relationships_entry != nullptr
            && workbook_relationships_entry->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook calc preserve helper should audit workbook relationships preservation");
    const auto* calc_chain_relationships_entry =
        editor.edit_plan().find_package_entry("xl/_rels/calcChain.xml.rels");
    check(calc_chain_relationships_entry != nullptr,
        "workbook calc preserve helper should audit calcChain relationships preservation");
    check(calc_chain_relationships_entry->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "preserved calcChain relationships should be copy-original");
    check(calc_chain_relationships_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "preserved calcChain relationships audit should keep source-relationships role");
    check(calc_chain_relationships_entry->owner_part == calc_chain_part.value(),
        "preserved calcChain relationships audit should keep calcChain owner part");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") != entries.end(),
        "workbook calc preserve output should keep calcChain.xml");
    check(entries.find("xl/_rels/calcChain.xml.rels") != entries.end(),
        "workbook calc preserve output should keep calcChain relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "workbook calc preserve output should preserve worksheet bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "workbook calc preserve output should preserve calcChain bytes");
    check(output_reader.read_entry("xl/_rels/calcChain.xml.rels")
            == calc_chain_relationships,
        "workbook calc preserve output should preserve calcChain relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "workbook calc preserve output should preserve content types bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "workbook calc preserve output should preserve workbook relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "workbook calc preserve output should preserve unknown bytes");
    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "workbook calc preserve output should request full calculation");
    check_not_contains(workbook_xml, R"(fullCalcOnLoad="0")",
        "workbook calc preserve output should replace stale fullCalcOnLoad value");
}

void test_package_editor_request_full_calculation_preserves_prior_calc_chain_replacement()
{
    CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-workbook-calc-preserve-prior-chain-source.xlsx");
    const std::string calc_chain_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(</Relationships>)";
    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"xl/calcChain.xml", source.calc_chain},
            {"xl/_rels/calcChain.xml.rels", calc_chain_relationships},
            {"custom/opaque.bin", source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-workbook-calc-preserve-prior-chain-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::string prior_calc_chain =
        R"(<calcChain><c r="Z99" i="9"/></calcChain>)";
    replace_part_with_memory_chunks(editor, calc_chain_part, prior_calc_chain,
        "prior calcChain replacement before workbook calc preserve helper");

    editor.request_full_calculation(fastxlsx::detail::CalcChainAction::Preserve);

    check(editor.edit_plan().full_calculation_on_load(),
        "prior preserve calcChain helper should request full calculation");
    check(editor.edit_plan().calc_chain_action()
            == fastxlsx::detail::CalcChainAction::Preserve,
        "prior preserve calcChain helper should expose preserve policy");
    check(editor.edit_plan().removed_parts().empty(),
        "prior preserve calcChain helper should not record removed parts");
    check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
        "prior preserve calcChain helper should not record removed calcChain");
    const auto* calc_chain_plan = editor.edit_plan().find_part(calc_chain_part);
    check(calc_chain_plan != nullptr,
        "prior preserve calcChain helper should keep calcChain in the edit plan");
    check(calc_chain_plan->write_mode
            == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "prior preserve calcChain helper should keep prior staged calcChain replacement active");
    check(calc_chain_plan->reason.find("prior calcChain replacement") != std::string::npos,
        "prior preserve calcChain helper should keep prior calcChain replacement reason");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr
            && workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "prior preserve calcChain helper should local-DOM-rewrite workbook metadata");
    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr
            && worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "prior preserve calcChain helper should leave worksheet copy-original");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "prior preserve calcChain helper should mirror workbook local-DOM-rewrite in manifest");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "prior preserve calcChain helper should keep calcChain stream-rewrite in manifest");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "prior preserve calcChain helper should not rewrite content types");

    const auto* workbook_relationships_entry =
        editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels");
    check(workbook_relationships_entry != nullptr
            && workbook_relationships_entry->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "prior preserve calcChain helper should audit workbook relationships preservation");
    const auto* calc_chain_relationships_entry =
        editor.edit_plan().find_package_entry("xl/_rels/calcChain.xml.rels");
    check(calc_chain_relationships_entry != nullptr,
        "prior preserve calcChain helper should audit calcChain relationships preservation");
    check(calc_chain_relationships_entry->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "prior preserve calcChain relationships should be copy-original");
    check(calc_chain_relationships_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "prior preserve calcChain relationships audit should keep source-relationships role");
    check(calc_chain_relationships_entry->owner_part == calc_chain_part.value(),
        "prior preserve calcChain relationships audit should keep calcChain owner part");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") != entries.end(),
        "prior preserve calcChain output should keep calcChain.xml");
    check(entries.find("xl/_rels/calcChain.xml.rels") != entries.end(),
        "prior preserve calcChain output should keep calcChain relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "prior preserve calcChain output should preserve worksheet bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == prior_calc_chain,
        "prior preserve calcChain output should use queued calcChain replacement bytes");
    check(output_reader.read_entry("xl/_rels/calcChain.xml.rels")
            == calc_chain_relationships,
        "prior preserve calcChain output should preserve calcChain relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "prior preserve calcChain output should preserve content types bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "prior preserve calcChain output should preserve workbook relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "prior preserve calcChain output should preserve unknown bytes");
    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "prior preserve calcChain output should request full calculation");
    check_not_contains(workbook_xml, R"(fullCalcOnLoad="0")",
        "prior preserve calcChain output should replace stale fullCalcOnLoad value");
}

void test_package_editor_rejects_workbook_calc_rebuild_without_state_changes()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-workbook-calc-rebuild-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-workbook-calc-rebuild-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t initial_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();

    bool failed = false;
    try {
        editor.request_full_calculation(fastxlsx::detail::CalcChainAction::Rebuild);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "rebuild",
            "workbook calc rebuild failure should report unsupported rebuild");
    }
    check(failed, "PackageEditor should reject workbook calcChain rebuild");

    check(editor.edit_plan().size() == initial_plan_size,
        "workbook calc rebuild failure should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "workbook calc rebuild failure should not change edit plan notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "workbook calc rebuild failure should not change relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "workbook calc rebuild failure should not change worksheet relationship reference audits");
    check(editor.edit_plan().removed_parts().empty(),
        "workbook calc rebuild failure should not record removed parts");
    check(editor.edit_plan().package_entries().empty(),
        "workbook calc rebuild failure should not record package-entry audit");
    check(editor.edit_plan().removed_package_entries().empty(),
        "workbook calc rebuild failure should not record removed package-entry audit");
    check(!editor.edit_plan().full_calculation_on_load(),
        "workbook calc rebuild failure should not request full calculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "workbook calc rebuild failure should not change calcChain action");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook calc rebuild failure should leave workbook copy-original");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook calc rebuild failure should leave calcChain copy-original");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "workbook calc rebuild failure output should preserve workbook bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "workbook calc rebuild failure output should preserve calcChain bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "workbook calc rebuild failure output should preserve content types bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "workbook calc rebuild failure output should preserve workbook relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "workbook calc rebuild failure output should preserve unknown bytes");
}

void test_package_editor_rejects_malformed_workbook_calc_metadata_without_state_changes()
{
    CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-workbook-calc-malformed-source.xlsx");
    source.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Broken" sheetId="1" r:id="rId1"/></sheets>)";
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
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-workbook-calc-malformed-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t initial_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();

    bool failed = false;
    try {
        editor.request_full_calculation();
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "workbook XML closing tag",
            "malformed workbook calc failure should report workbook XML preflight");
    }
    check(failed, "PackageEditor should reject malformed workbook calc metadata rewrite");

    check(editor.edit_plan().size() == initial_plan_size,
        "malformed workbook calc failure should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "malformed workbook calc failure should not change edit plan notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "malformed workbook calc failure should not change relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "malformed workbook calc failure should not change worksheet relationship reference audits");
    check(editor.edit_plan().removed_parts().empty(),
        "malformed workbook calc failure should not record removed parts");
    check(editor.edit_plan().package_entries().empty(),
        "malformed workbook calc failure should not record package-entry audit");
    check(editor.edit_plan().removed_package_entries().empty(),
        "malformed workbook calc failure should not record removed package-entry audit");
    check(!editor.edit_plan().full_calculation_on_load(),
        "malformed workbook calc failure should not request full calculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "malformed workbook calc failure should not change calcChain action");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "malformed workbook calc failure should leave workbook copy-original");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "malformed workbook calc failure should leave calcChain copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "malformed workbook calc failure output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "malformed workbook calc failure output plan should preserve calcChain policy");
    check(output_plan.notes.size() == initial_note_count,
        "malformed workbook calc failure output plan should not add audit notes");
    check(output_plan.relationship_target_audits.size()
            == initial_relationship_target_audit_count,
        "malformed workbook calc failure output plan should not add relationship target audits");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == initial_worksheet_relationship_reference_audit_count,
        "malformed workbook calc failure output plan should not add worksheet reference audits");
    check(output_plan.removed_parts.empty(),
        "malformed workbook calc failure output plan should not record removed parts");
    check(output_plan.removed_package_entries.empty(),
        "malformed workbook calc failure output plan should not record omitted metadata entries");
    check_output_plan_preserves_source_copy_original(editor, output_plan,
        "malformed workbook calc failure output plan should keep every source entry copy-original");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "malformed workbook calc failure output plan should classify content types as metadata entry");
    check_output_entry_part_context(output_plan.entries, "_rels/.rels", false, "",
        "malformed workbook calc failure output plan should classify package relationships as metadata entry");
    check_output_entry_part_context(output_plan.entries, "xl/workbook.xml", true,
        workbook_part.value(),
        "malformed workbook calc failure output plan should classify workbook as a package part");
    check_output_entry_part_context(output_plan.entries, "xl/_rels/workbook.xml.rels",
        false, "",
        "malformed workbook calc failure output plan should classify workbook relationships as metadata entry");
    check_output_entry_part_context(output_plan.entries, "xl/calcChain.xml", true,
        calc_chain_part.value(),
        "malformed workbook calc failure output plan should classify calcChain as a package part");
    check_output_entry_part_context(output_plan.entries, "custom/opaque.bin", true,
        "/custom/opaque.bin",
        "malformed workbook calc failure output plan should classify unknown bytes as a package part");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "malformed workbook calc failure output should preserve workbook bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "malformed workbook calc failure output should preserve calcChain bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "malformed workbook calc failure output should preserve content types bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "malformed workbook calc failure output should preserve workbook relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "malformed workbook calc failure output should preserve unknown bytes");
}

void test_package_editor_replaces_workbook_and_preserves_linked_fixture_entries()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-linked-workbook-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-linked-workbook-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string replacement_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Renamed" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<definedNames><definedName name="ReportRange">Renamed!$A$1:$B$2</definedName></definedNames>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, replacement_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "linked fixture workbook local-DOM rewrite");

    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "linked fixture workbook replacement should be present in the edit plan");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "linked fixture workbook replacement should be local-DOM-rewrite");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "linked fixture workbook replacement should mirror write mode into manifest");
    const auto* workbook_relationships_audit =
        editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels");
    check(workbook_relationships_audit != nullptr,
        "linked fixture workbook replacement should audit preserved workbook relationships");
    check(workbook_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture workbook relationships audit should be copy-original");
    check(workbook_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "linked fixture workbook relationships audit should keep source relationship role");
    check(workbook_relationships_audit->owner_part == workbook_part.value(),
        "linked fixture workbook relationships audit should keep owner part");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture worksheet should remain copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture sharedStrings should remain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture unknown extension should remain copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "linked fixture workbook replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "linked fixture workbook replacement output plan should preserve calcChain policy");
    check(output_plan.relationship_target_audits.empty(),
        "linked fixture workbook replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "linked fixture workbook replacement output plan should not remove parts");
    check(output_plan.removed_package_entries.empty(),
        "linked fixture workbook replacement output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "linked fixture workbook replacement output plan should rewrite workbook");
    check_output_entry_part_context(output_plan.entries, "xl/workbook.xml", true,
        workbook_part.value(),
        "linked fixture workbook replacement output plan should classify workbook");
    const auto* output_workbook_plan =
        find_output_entry_plan(output_plan.entries, "xl/workbook.xml");
    check(output_workbook_plan->reason.find("local-DOM rewrite") != std::string::npos,
        "linked fixture workbook replacement output plan should keep replacement reason");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture workbook replacement output plan should preserve workbook relationships");
    check_output_entry_part_context(output_plan.entries, "xl/_rels/workbook.xml.rels",
        false, "",
        "linked fixture workbook replacement output plan should classify workbook relationships as metadata");
    const auto* output_workbook_relationships_plan =
        find_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels");
    check(output_workbook_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "linked fixture workbook replacement output plan should classify workbook relationships metadata");
    check(output_workbook_relationships_plan->owner_part == workbook_part.value(),
        "linked fixture workbook replacement output plan should keep workbook relationships owner");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture workbook replacement output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "linked fixture workbook replacement output plan should classify content types as metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture workbook replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture workbook replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture workbook replacement output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture workbook replacement output plan should preserve drawing");
    check_output_entry_plan(output_plan.entries, "xl/drawings/_rels/drawing1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture workbook replacement output plan should preserve drawing relationships");
    check_output_entry_plan(output_plan.entries, "xl/charts/chart1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture workbook replacement output plan should preserve chart");
    check_output_entry_plan(output_plan.entries, "xl/media/image1.png",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture workbook replacement output plan should preserve media");
    check_output_entry_plan(output_plan.entries, "xl/tables/table1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture workbook replacement output plan should preserve table");
    check_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawing1.vml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture workbook replacement output plan should preserve VML drawing");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing space.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture workbook replacement output plan should preserve percent-decoded drawing");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture workbook replacement output plan should preserve sharedStrings");
    check_output_entry_plan(output_plan.entries, "xl/_rels/sharedStrings.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture workbook replacement output plan should preserve sharedStrings relationships");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture workbook replacement output plan should preserve styles");
    check_output_entry_plan(output_plan.entries, "xl/vbaProject.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture workbook replacement output plan should preserve VBA");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture workbook replacement output plan should preserve calcChain");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture workbook replacement output plan should preserve unknown extension");
    check_output_entry_plan(output_plan.entries,
        "custom/_rels/opaque-extension.bin.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture workbook replacement output plan should preserve unknown extension relationships");
    check_output_entry_part_context(output_plan.entries,
        "custom/_rels/opaque-extension.bin.rels", false, "",
        "linked fixture workbook replacement output plan should classify unknown relationships as metadata");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, workbook_part.zip_path());
    check(output_reader.read_entry("xl/workbook.xml") == replacement_workbook,
        "linked fixture workbook replacement should write replacement bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "linked fixture ordinary workbook replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "linked fixture ordinary workbook replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "linked fixture ordinary workbook replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "linked fixture ordinary workbook replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "linked fixture ordinary workbook replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "linked fixture ordinary workbook replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "linked fixture ordinary workbook replacement should preserve media bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "linked fixture ordinary workbook replacement should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "linked fixture ordinary workbook replacement should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "linked fixture ordinary workbook replacement should preserve VBA bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "linked fixture ordinary workbook replacement should preserve unknown extension bytes");

    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "linked fixture ordinary workbook replacement should keep workbook relationships readable");
    check(workbook_relationships->find_by_id("rId2") != nullptr,
        "linked fixture ordinary workbook replacement should keep VBA relationship");
    check(workbook_relationships->find_by_id("rId3") != nullptr,
        "linked fixture ordinary workbook replacement should keep sharedStrings relationship");
    check(workbook_relationships->find_by_id("rId4") != nullptr,
        "linked fixture ordinary workbook replacement should keep styles relationship");
    check(workbook_relationships->find_by_id("rId5") != nullptr,
        "linked fixture ordinary workbook replacement should keep calcChain relationship");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "linked fixture ordinary workbook replacement should keep calcChain content type");
    check(output_reader.relationships_for(opaque_extension_part) != nullptr,
        "linked fixture ordinary workbook replacement should keep unknown extension relationships readable");
}

#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG

void test_package_editor_replaces_worksheet_cells_from_deflated_source_chunk_source()
{
    const CalcSourcePackage source =
        write_calc_source_package(
            "fastxlsx-package-editor-deflated-cell-replacement-source.xlsx",
            fastxlsx::detail::PackageWriterBackend::MinizipNg);
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-deflated-cell-replacement-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const auto* source_worksheet_entry =
        editor.reader().find_entry(worksheet_part.zip_path());
    check(source_worksheet_entry != nullptr,
        "DEFLATE cell replacement fixture should include worksheet entry");
    check(source_worksheet_entry->compression_method == 8,
        "DEFLATE cell replacement fixture should deflate worksheet entry");

    const std::array replacements {
        worksheet_cell_replacement(
            "A1", R"(<c r="A1" t="inlineStr"><is><t>deflated-patched</t></is></c>)"),
    };
    editor.replace_worksheet_cells_by_name("Sheet1", replacements);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "DEFLATE source cell replacement should keep worksheet in the edit plan");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "DEFLATE source cell replacement should use staged stream rewrite mode");
    check(editor.edit_plan().full_calculation_on_load(),
        "DEFLATE source cell replacement should request full calculation");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "DEFLATE source cell replacement should remove stale calcChain");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(has_note_containing(output_plan.notes,
              {"PackageReader ZIP-entry chunk source", "source worksheet XML"}),
        "DEFLATE source cell replacement should expose PackageReader chunk source note");
    check(has_note_containing(output_plan.notes,
              {"relationship-id audit", "transformer chunk-source adapter"}),
        "DEFLATE source cell replacement should expose chunked relationship audit");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string worksheet_xml =
        output_reader.read_entry(worksheet_part.zip_path());
    check_contains(worksheet_xml, "deflated-patched",
        "DEFLATE source cell replacement output should include replacement cell payload");
    check_not_contains(worksheet_xml, "SUM(B1:C1)",
        "DEFLATE source cell replacement output should remove old target cell payload");
    check(output_reader.find_entry(calc_chain_part.zip_path()) == nullptr,
        "DEFLATE source cell replacement output should omit stale calcChain payload");
    check_contains(output_reader.read_entry(workbook_part.zip_path()), "fullCalcOnLoad=\"1\"",
        "DEFLATE source cell replacement output should request full recalculation");
}

void test_package_editor_replaces_workbook_from_deflated_source_and_preserves_unknown_payloads()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package(
            "fastxlsx-package-editor-deflated-linked-source.xlsx",
            fastxlsx::detail::PackageWriterBackend::MinizipNg);
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-deflated-linked-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const auto* source_workbook_entry =
        editor.reader().find_entry(workbook_part.zip_path());
    check(source_workbook_entry != nullptr,
        "DEFLATE PackageEditor fixture should include workbook entry");
    check(source_workbook_entry->compression_method == 8,
        "DEFLATE PackageEditor fixture should deflate workbook entry");
    const auto* source_opaque_entry =
        editor.reader().find_entry("custom/opaque-extension.bin");
    check(source_opaque_entry != nullptr,
        "DEFLATE PackageEditor fixture should include unknown extension entry");
    check(source_opaque_entry->compression_method == 8,
        "DEFLATE PackageEditor fixture should deflate unknown extension entry");
    const auto* source_opaque_relationships_entry =
        editor.reader().find_entry("custom/_rels/opaque-extension.bin.rels");
    check(source_opaque_relationships_entry != nullptr,
        "DEFLATE PackageEditor fixture should include unknown owner relationships");
    check(source_opaque_relationships_entry->compression_method == 8,
        "DEFLATE PackageEditor fixture should deflate unknown owner relationships");

    const std::string replacement_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Deflated" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<definedNames><definedName name="ReportRange">Deflated!$A$1:$B$2</definedName></definedNames>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, replacement_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "DEFLATE source workbook local-DOM rewrite");

    const auto* opaque_plan = editor.edit_plan().find_part(opaque_extension_part);
    check(opaque_plan != nullptr,
        "DEFLATE source edit plan should keep unknown extension part visible");
    check(opaque_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "DEFLATE source edit plan should keep unknown extension copy-original");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.entries().size() == editor.reader().entries().size(),
        "DEFLATE source output should keep package entry count");
    check(output_reader.read_entry("xl/workbook.xml") == replacement_workbook,
        "DEFLATE source workbook replacement should write replacement bytes");

    for (const fastxlsx::detail::PackageReaderEntry& source_entry :
         editor.reader().entries()) {
        if (source_entry.name == workbook_part.zip_path()) {
            continue;
        }
        check(output_reader.find_entry(source_entry.name) != nullptr,
            "DEFLATE source output should keep copied package entries");
        if (output_reader.read_entry(source_entry.name)
            != editor.reader().read_entry(source_entry.name)) {
            throw TestFailure(
                "PackageEditor changed copied DEFLATE source payload: " + source_entry.name);
        }
    }

    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "DEFLATE source output should preserve unknown extension payload");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "DEFLATE source output should preserve unknown owner relationship payload");

    const auto* opaque_relationships =
        output_reader.relationships_for(opaque_extension_part);
    check(opaque_relationships != nullptr,
        "DEFLATE source output should re-ingest unknown owner relationships");
    const auto* opaque_external =
        opaque_relationships->find_by_id("rIdOpaqueExternal");
    check(opaque_external != nullptr,
        "DEFLATE source output should preserve unknown owner relationship id");
    check(opaque_external->target_mode
            == fastxlsx::detail::Relationship::TargetMode::External,
        "DEFLATE source output should preserve unknown owner external target mode");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(opaque_extension_part) != nullptr,
        "DEFLATE source relationship graph should attach unknown owner relationships");
}

void test_package_editor_replaces_unknown_extension_from_deflated_source_and_preserves_owner_relationships()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package(
            "fastxlsx-package-editor-deflated-opaque-source.xlsx",
            fastxlsx::detail::PackageWriterBackend::MinizipNg);
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-deflated-opaque-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const auto* source_opaque_entry =
        editor.reader().find_entry(opaque_extension_part.zip_path());
    check(source_opaque_entry != nullptr,
        "DEFLATE unknown replacement fixture should include unknown extension entry");
    check(source_opaque_entry->compression_method == 8,
        "DEFLATE unknown replacement fixture should deflate unknown extension entry");
    const auto* source_opaque_relationships_entry =
        editor.reader().find_entry("custom/_rels/opaque-extension.bin.rels");
    check(source_opaque_relationships_entry != nullptr,
        "DEFLATE unknown replacement fixture should include unknown owner relationships");
    check(source_opaque_relationships_entry->compression_method == 8,
        "DEFLATE unknown replacement fixture should deflate unknown owner relationships");

    const std::string replacement_opaque("deflated-source opaque replacement bytes");
    replace_part_with_memory_chunks(editor, opaque_extension_part,
        replacement_opaque, "DEFLATE source unknown extension stream rewrite");

    const auto* opaque_plan = editor.edit_plan().find_part(opaque_extension_part);
    check(opaque_plan != nullptr,
        "DEFLATE unknown replacement should keep target part in edit plan");
    check(opaque_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "DEFLATE unknown replacement should record stream-rewrite mode");
    const auto* opaque_relationships_audit =
        editor.edit_plan().find_package_entry("custom/_rels/opaque-extension.bin.rels");
    check(opaque_relationships_audit != nullptr,
        "DEFLATE unknown replacement should audit preserved owner relationships");
    check(opaque_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "DEFLATE unknown owner relationships audit should be copy-original");
    check(opaque_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "DEFLATE unknown owner relationships audit should keep source relationship role");
    check(opaque_relationships_audit->owner_part == opaque_extension_part.value(),
        "DEFLATE unknown owner relationships audit should keep owner part");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.entries().size() == editor.reader().entries().size(),
        "DEFLATE unknown replacement output should keep package entry count");
    check(output_reader.read_entry("custom/opaque-extension.bin") == replacement_opaque,
        "DEFLATE unknown replacement output should write replacement payload");

    for (const fastxlsx::detail::PackageReaderEntry& source_entry :
         editor.reader().entries()) {
        if (source_entry.name == opaque_extension_part.zip_path()) {
            continue;
        }
        check(output_reader.find_entry(source_entry.name) != nullptr,
            "DEFLATE unknown replacement output should keep copied package entries");
        if (output_reader.read_entry(source_entry.name)
            != editor.reader().read_entry(source_entry.name)) {
            throw TestFailure(
                "PackageEditor changed copied DEFLATE unknown replacement payload: "
                + source_entry.name);
        }
    }

    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "DEFLATE unknown replacement should preserve owner relationship payload");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "DEFLATE unknown replacement should preserve workbook payload");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "DEFLATE unknown replacement should preserve worksheet payload");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "DEFLATE unknown replacement should preserve drawing payload");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "DEFLATE unknown replacement should preserve media payload");

    const auto* opaque_relationships =
        output_reader.relationships_for(opaque_extension_part);
    check(opaque_relationships != nullptr,
        "DEFLATE unknown replacement should re-ingest unknown owner relationships");
    const auto* opaque_external =
        opaque_relationships->find_by_id("rIdOpaqueExternal");
    check(opaque_external != nullptr,
        "DEFLATE unknown replacement should preserve unknown owner relationship id");
    check(opaque_external->target == "https://example.invalid/opaque-extension-audit",
        "DEFLATE unknown replacement should preserve unknown owner relationship target");
    check(opaque_external->target_mode
            == fastxlsx::detail::Relationship::TargetMode::External,
        "DEFLATE unknown replacement should preserve unknown owner external target mode");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(opaque_extension_part) != nullptr,
        "DEFLATE unknown replacement relationship graph should attach owner relationships");
}

void test_package_editor_request_full_calculation_from_deflated_source_preserves_linked_payloads()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package(
            "fastxlsx-package-editor-deflated-calc-source.xlsx",
            fastxlsx::detail::PackageWriterBackend::MinizipNg);
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-deflated-calc-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const auto* source_workbook_entry =
        editor.reader().find_entry(workbook_part.zip_path());
    check(source_workbook_entry != nullptr,
        "DEFLATE calc helper fixture should include workbook entry");
    check(source_workbook_entry->compression_method == 8,
        "DEFLATE calc helper fixture should deflate workbook entry");
    const auto* source_calc_chain_entry =
        editor.reader().find_entry(calc_chain_part.zip_path());
    check(source_calc_chain_entry != nullptr,
        "DEFLATE calc helper fixture should include calcChain entry");
    check(source_calc_chain_entry->compression_method == 8,
        "DEFLATE calc helper fixture should deflate calcChain entry");
    const auto* source_opaque_relationships_entry =
        editor.reader().find_entry("custom/_rels/opaque-extension.bin.rels");
    check(source_opaque_relationships_entry != nullptr,
        "DEFLATE calc helper fixture should include unknown owner relationships");
    check(source_opaque_relationships_entry->compression_method == 8,
        "DEFLATE calc helper fixture should deflate unknown owner relationships");

    editor.request_full_calculation();

    check(editor.edit_plan().full_calculation_on_load(),
        "DEFLATE calc helper should request full calculation");
    check(editor.edit_plan().calc_chain_action()
            == fastxlsx::detail::CalcChainAction::Remove,
        "DEFLATE calc helper should default to calcChain removal");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "DEFLATE calc helper should keep workbook in edit plan");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "DEFLATE calc helper should local-DOM-rewrite workbook metadata");
    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "DEFLATE calc helper should keep worksheet in edit plan");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "DEFLATE calc helper should keep worksheet copy-original");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "DEFLATE calc helper should record calcChain removal");
    const auto* workbook_relationships_plan =
        editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels");
    check(workbook_relationships_plan != nullptr,
        "DEFLATE calc helper should audit workbook relationships rewrite");
    check(workbook_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "DEFLATE calc helper should rewrite workbook relationships metadata");
    const fastxlsx::detail::PackageEditorOutputPlan output_plan =
        editor.planned_output();
    check_output_entry_plan(output_plan.entries, "custom/_rels/opaque-extension.bin.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "DEFLATE calc helper output plan should preserve unknown owner relationships");
    check_output_entry_part_context(output_plan.entries,
        "custom/_rels/opaque-extension.bin.rels", false, "",
        "DEFLATE calc helper output plan should classify unknown owner relationships");

    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "DEFLATE calc helper output should omit stale calcChain.xml");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    for (const fastxlsx::detail::PackageReaderEntry& source_entry :
         editor.reader().entries()) {
        if (source_entry.name == workbook_part.zip_path()
            || source_entry.name == calc_chain_part.zip_path()
            || source_entry.name == "[Content_Types].xml"
            || source_entry.name == "xl/_rels/workbook.xml.rels") {
            continue;
        }
        check(output_reader.find_entry(source_entry.name) != nullptr,
            "DEFLATE calc helper output should keep copied package entries");
        if (output_reader.read_entry(source_entry.name)
            != editor.reader().read_entry(source_entry.name)) {
            throw TestFailure(
                "PackageEditor changed copied DEFLATE calc helper payload: "
                + source_entry.name);
        }
    }

    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "DEFLATE calc helper output should preserve worksheet payload");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "DEFLATE calc helper output should preserve worksheet relationships payload");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "DEFLATE calc helper output should preserve drawing payload");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "DEFLATE calc helper output should preserve drawing relationships payload");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "DEFLATE calc helper output should preserve media payload");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "DEFLATE calc helper output should preserve unknown extension payload");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "DEFLATE calc helper output should preserve unknown owner relationship payload");

    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "DEFLATE calc helper output should request workbook recalculation");
    check_contains(workbook_xml,
        R"(<definedNames><definedName name="ReportRange">Sheet1!$A$1:$B$2</definedName></definedNames>)",
        "DEFLATE calc helper output should preserve workbook defined names");
    const std::string workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_not_contains(workbook_relationships, "relationships/calcChain",
        "DEFLATE calc helper output should remove calcChain workbook relationship");
    check_contains(workbook_relationships, "relationships/sharedStrings",
        "DEFLATE calc helper output should preserve sharedStrings workbook relationship");
    check_contains(workbook_relationships, "relationships/styles",
        "DEFLATE calc helper output should preserve styles workbook relationship");
    check(output_reader.content_types().override_for(calc_chain_part) == nullptr,
        "DEFLATE calc helper output should remove calcChain content type");

    const auto* opaque_relationships =
        output_reader.relationships_for(opaque_extension_part);
    check(opaque_relationships != nullptr,
        "DEFLATE calc helper output should re-ingest unknown owner relationships");
    check(opaque_relationships->find_by_id("rIdOpaqueExternal") != nullptr,
        "DEFLATE calc helper output should preserve unknown owner relationship id");
    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(opaque_extension_part) != nullptr,
        "DEFLATE calc helper relationship graph should attach unknown owner relationships");
}

void test_package_editor_replaces_worksheet_from_deflated_source_and_preserves_linked_payloads()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package(
            "fastxlsx-package-editor-deflated-linked-worksheet-source.xlsx",
            fastxlsx::detail::PackageWriterBackend::MinizipNg);
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-deflated-linked-worksheet-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName vba_part("/xl/vbaProject.bin");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const auto* source_worksheet_entry =
        editor.reader().find_entry(worksheet_part.zip_path());
    check(source_worksheet_entry != nullptr,
        "DEFLATE worksheet fixture should include worksheet entry");
    check(source_worksheet_entry->compression_method == 8,
        "DEFLATE worksheet fixture should deflate worksheet entry");
    const auto* source_calc_chain_entry =
        editor.reader().find_entry(calc_chain_part.zip_path());
    check(source_calc_chain_entry != nullptr,
        "DEFLATE worksheet fixture should include calcChain entry");
    check(source_calc_chain_entry->compression_method == 8,
        "DEFLATE worksheet fixture should deflate calcChain entry");
    const auto* source_opaque_relationships_entry =
        editor.reader().find_entry("custom/_rels/opaque-extension.bin.rels");
    check(source_opaque_relationships_entry != nullptr,
        "DEFLATE worksheet fixture should include unknown owner relationships");
    check(source_opaque_relationships_entry->compression_method == 8,
        "DEFLATE worksheet fixture should deflate unknown owner relationships");

    const std::string replacement_sheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>401</v></c></row></sheetData>)"
        R"(<hyperlinks><hyperlink ref="A1" r:id="rId2"/></hyperlinks>)"
        R"(<drawing r:id="rId1"/>)"
        R"(<tableParts count="1"><tablePart r:id="rId3"/></tableParts>)"
        R"(</worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_sheet);

    check(editor.edit_plan().full_calculation_on_load(),
        "DEFLATE worksheet rewrite should request full calculation");
    check(editor.edit_plan().calc_chain_action()
            == fastxlsx::detail::CalcChainAction::Remove,
        "DEFLATE worksheet rewrite should keep calcChain removal policy visible");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "DEFLATE worksheet rewrite should record stale calcChain removal");
    const auto* drawing_plan = editor.edit_plan().find_part(drawing_part);
    check(drawing_plan != nullptr,
        "DEFLATE worksheet rewrite should keep linked drawing visible");
    check(drawing_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "DEFLATE worksheet rewrite should keep linked drawing copy-original");
    const auto* opaque_plan = editor.edit_plan().find_part(opaque_extension_part);
    check(opaque_plan != nullptr,
        "DEFLATE worksheet rewrite should keep unknown extension visible");
    check(opaque_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "DEFLATE worksheet rewrite should keep unknown extension copy-original");
    const auto* worksheet_relationships_plan =
        editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels");
    check(worksheet_relationships_plan != nullptr,
        "DEFLATE worksheet rewrite should audit preserved worksheet relationships");
    check(worksheet_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "DEFLATE worksheet relationships audit should remain copy-original");
    const auto* opaque_relationships_plan =
        editor.edit_plan().find_package_entry("custom/_rels/opaque-extension.bin.rels");
    check(opaque_relationships_plan != nullptr,
        "DEFLATE worksheet rewrite should audit preserved unknown owner relationships");
    check(opaque_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "DEFLATE unknown owner relationships audit should remain copy-original");

    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "DEFLATE worksheet rewrite output should omit stale calcChain.xml");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_sheet,
        "DEFLATE worksheet rewrite output should write replacement worksheet XML");
    for (const fastxlsx::detail::PackageReaderEntry& source_entry :
         editor.reader().entries()) {
        if (source_entry.name == worksheet_part.zip_path()
            || source_entry.name == workbook_part.zip_path()
            || source_entry.name == calc_chain_part.zip_path()
            || source_entry.name == "[Content_Types].xml"
            || source_entry.name == "xl/_rels/workbook.xml.rels") {
            continue;
        }
        check(output_reader.find_entry(source_entry.name) != nullptr,
            "DEFLATE worksheet rewrite output should keep copied package entries");
        if (output_reader.read_entry(source_entry.name)
            != editor.reader().read_entry(source_entry.name)) {
            throw TestFailure(
                "PackageEditor changed copied DEFLATE worksheet payload: "
                + source_entry.name);
        }
    }

    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "DEFLATE worksheet rewrite should preserve worksheet relationships payload");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "DEFLATE worksheet rewrite should preserve drawing payload");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "DEFLATE worksheet rewrite should preserve drawing relationships payload");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "DEFLATE worksheet rewrite should preserve media payload");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "DEFLATE worksheet rewrite should preserve sharedStrings payload");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "DEFLATE worksheet rewrite should preserve styles payload");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "DEFLATE worksheet rewrite should preserve VBA payload");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "DEFLATE worksheet rewrite should preserve unknown extension payload");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "DEFLATE worksheet rewrite should preserve unknown owner relationship payload");

    const std::string workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_contains(workbook_relationships, "relationships/sharedStrings",
        "DEFLATE worksheet rewrite should preserve sharedStrings workbook relationship");
    check_contains(workbook_relationships, "relationships/styles",
        "DEFLATE worksheet rewrite should preserve styles workbook relationship");
    check_contains(workbook_relationships, "relationships/vbaProject",
        "DEFLATE worksheet rewrite should preserve VBA workbook relationship");
    check_not_contains(workbook_relationships, "relationships/calcChain",
        "DEFLATE worksheet rewrite should remove calcChain workbook relationship");
    check(output_reader.content_types().override_for(calc_chain_part) == nullptr,
        "DEFLATE worksheet rewrite should remove calcChain content type");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "DEFLATE worksheet rewrite should preserve sharedStrings content type");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "DEFLATE worksheet rewrite should preserve styles content type");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "DEFLATE worksheet rewrite should preserve VBA content type");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "DEFLATE worksheet rewrite should preserve table content type");
    check(output_reader.content_types().default_for("png") != nullptr,
        "DEFLATE worksheet rewrite should preserve PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "DEFLATE worksheet rewrite should not promote PNG media defaults");

    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "DEFLATE worksheet rewrite should request workbook recalculation");
    check_contains(workbook_xml,
        R"(<definedNames><definedName name="ReportRange">Sheet1!$A$1:$B$2</definedName></definedNames>)",
        "DEFLATE worksheet rewrite should preserve workbook defined names");

    const auto* opaque_relationships =
        output_reader.relationships_for(opaque_extension_part);
    check(opaque_relationships != nullptr,
        "DEFLATE worksheet rewrite should re-ingest unknown owner relationships");
    check(opaque_relationships->find_by_id("rIdOpaqueExternal") != nullptr,
        "DEFLATE worksheet rewrite should preserve unknown owner relationship id");
    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(opaque_extension_part) != nullptr,
        "DEFLATE worksheet relationship graph should attach unknown owner relationships");
}

#endif

void test_package_editor_repeated_worksheet_rewrite_upserts_relationship_target_audits()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-repeated-sheet-audit-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-repeated-sheet-audit-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::string first_replacement_sheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>201</v></c></row></sheetData>)"
        R"(<drawing r:id="rId1"/>)"
        R"(<tableParts count="1"><tablePart r:id="rId3"/></tableParts>)"
        R"(</worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, first_replacement_sheet);

    const std::size_t first_note_count = editor.edit_plan().notes().size();
    const std::size_t first_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t first_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    check(first_note_count > 0,
        "first linked worksheet rewrite should record audit notes");
    check(first_relationship_target_audit_count > 0,
        "first linked worksheet rewrite should record relationship target audits");
    check(first_worksheet_relationship_reference_audit_count > 0,
        "first linked worksheet rewrite should record worksheet relationship reference audits");

    const std::string second_replacement_sheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>202</v></c></row></sheetData>)"
        R"(<drawing r:id="rId1"/>)"
        R"(<tableParts count="1"><tablePart r:id="rId3"/></tableParts>)"
        R"(</worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, second_replacement_sheet);

    check(editor.edit_plan().notes().size() == first_note_count,
        "repeated linked worksheet rewrite should not duplicate identical audit notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == first_relationship_target_audit_count,
        "repeated linked worksheet rewrite should upsert relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == first_worksheet_relationship_reference_audit_count,
        "repeated linked worksheet rewrite should upsert worksheet relationship reference audits");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated linked worksheet rewrite should keep worksheet stream-rewrite mode");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "repeated linked worksheet rewrite should keep stale calcChain removed-part audit");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "repeated linked worksheet rewrite should keep calcChain removed from manifest");
    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.worksheet_relationship_reference_audits.size()
            == first_worksheet_relationship_reference_audit_count,
        "repeated linked worksheet rewrite output plan should mirror upserted worksheet relationship reference audits");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == second_replacement_sheet,
        "repeated linked worksheet rewrite should write the latest worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "repeated linked worksheet rewrite should still preserve worksheet relationships bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "repeated linked worksheet rewrite should still preserve unknown extension bytes");
    check(output_reader.find_entry("xl/calcChain.xml") == nullptr,
        "repeated linked worksheet rewrite output should keep stale calcChain omitted");
}

void test_package_editor_preserves_source_relationship_parts_when_replacement_omits_references()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-orphaned-rels-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-orphaned-rels-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");
    const fastxlsx::detail::PartName vba_part("/xl/vbaProject.bin");

    const std::string replacement_sheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>123</v></c></row></sheetData></worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_sheet);

    const auto* drawing_plan = editor.edit_plan().find_part(drawing_part);
    check(drawing_plan != nullptr,
        "omitted-reference worksheet rewrite should still plan drawing copy-original");
    check(drawing_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "omitted-reference worksheet rewrite should preserve source drawing part");
    const auto* table_plan = editor.edit_plan().find_part(table_part);
    check(table_plan != nullptr,
        "omitted-reference worksheet rewrite should still plan table copy-original");
    check(table_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "omitted-reference worksheet rewrite should preserve source table part");
    const fastxlsx::detail::PartName worksheet_relationships_part(
        "/xl/worksheets/_rels/sheet1.xml.rels");
    const fastxlsx::detail::PartName drawing_relationships_part(
        "/xl/drawings/_rels/drawing1.xml.rels");
    const fastxlsx::detail::PartName shared_strings_relationships_part(
        "/xl/_rels/sharedStrings.xml.rels");
    const auto* worksheet_relationships_plan = editor.edit_plan().find_package_entry(
        "xl/worksheets/_rels/sheet1.xml.rels");
    check(worksheet_relationships_plan != nullptr,
        "omitted-reference worksheet rewrite should still plan worksheet relationship audit");
    check(worksheet_relationships_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "omitted-reference worksheet rewrite should preserve worksheet relationships part");
    check(worksheet_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "omitted-reference worksheet rewrite should classify worksheet relationships as source relationships");
    check(worksheet_relationships_plan->owner_part == worksheet_part.value(),
        "omitted-reference worksheet rewrite should keep worksheet relationships owner");
    const auto* drawing_relationships_plan = editor.edit_plan().find_package_entry(
        "xl/drawings/_rels/drawing1.xml.rels");
    check(drawing_relationships_plan != nullptr,
        "omitted-reference worksheet rewrite should still plan drawing relationship audit");
    check(drawing_relationships_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "omitted-reference worksheet rewrite should preserve drawing relationships part");
    check(drawing_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "omitted-reference worksheet rewrite should classify drawing relationships as source relationships");
    check(drawing_relationships_plan->owner_part == drawing_part.value(),
        "omitted-reference worksheet rewrite should keep drawing relationships owner");
    const auto* shared_strings_relationships_plan =
        editor.edit_plan().find_package_entry("xl/_rels/sharedStrings.xml.rels");
    check(shared_strings_relationships_plan != nullptr,
        "omitted-reference worksheet rewrite should still plan sharedStrings relationship audit");
    check(shared_strings_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "omitted-reference worksheet rewrite should preserve sharedStrings relationships part");
    check(shared_strings_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "omitted-reference worksheet rewrite should classify sharedStrings relationships as source relationships");
    check(shared_strings_relationships_plan->owner_part
            == shared_strings_part.value(),
        "omitted-reference worksheet rewrite should keep sharedStrings relationships owner");
    const auto* opaque_extension_relationships_plan =
        editor.edit_plan().find_package_entry("custom/_rels/opaque-extension.bin.rels");
    check(opaque_extension_relationships_plan != nullptr,
        "omitted-reference worksheet rewrite should still plan unknown extension relationship audit");
    check(opaque_extension_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "omitted-reference worksheet rewrite should preserve unknown extension relationships part");
    check(opaque_extension_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "omitted-reference worksheet rewrite should classify unknown extension relationships as source relationships");
    check(opaque_extension_relationships_plan->owner_part == opaque_extension_part.value(),
        "omitted-reference worksheet rewrite should keep unknown extension relationships owner");
    check(!editor.edit_plan().notes().empty(),
        "omitted-reference worksheet rewrite should record relationship preservation notes");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "omitted-reference worksheet rewrite should omit stale calcChain.xml");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_sheet,
        "omitted-reference worksheet rewrite should write the replacement sheet XML");
    check_not_contains(output_reader.read_entry("xl/worksheets/sheet1.xml"), "<drawing",
        "omitted-reference replacement sheet should not contain drawing markup");
    check_not_contains(output_reader.read_entry("xl/worksheets/sheet1.xml"), "<tableParts",
        "omitted-reference replacement sheet should not contain tableParts markup");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "omitted-reference worksheet rewrite should byte-preserve source worksheet relationships");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "omitted-reference worksheet rewrite should preserve drawing XML bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "omitted-reference worksheet rewrite should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "omitted-reference worksheet rewrite should preserve table XML bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "omitted-reference worksheet rewrite should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "omitted-reference worksheet rewrite should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "omitted-reference worksheet rewrite should preserve VBA bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "omitted-reference worksheet rewrite should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "omitted-reference worksheet rewrite should byte-preserve unknown extension relationships");

    const auto* sheet_relationships = output_reader.relationships_for(worksheet_part);
    check(sheet_relationships != nullptr,
        "omitted-reference worksheet rewrite should keep source worksheet relationships readable");
    check(sheet_relationships->find_by_id("rId1") != nullptr,
        "omitted-reference worksheet rewrite should keep source drawing relationship");
    check(sheet_relationships->find_by_id("rId3") != nullptr,
        "omitted-reference worksheet rewrite should keep source table relationship");
    const auto* opaque_extension_relationships = output_reader.relationships_for(
        opaque_extension_part);
    check(opaque_extension_relationships != nullptr,
        "omitted-reference worksheet rewrite should keep unknown extension relationships readable");
    check(opaque_extension_relationships->find_by_id("rIdOpaqueExternal") != nullptr,
        "omitted-reference worksheet rewrite should keep unknown extension external relationship");
    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    const auto* output_worksheet_relationships_plan = find_output_entry_plan(
        output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "omitted-reference worksheet rewrite output should preserve worksheet relationships part");
    check(output_worksheet_relationships_plan != nullptr
            && output_worksheet_relationships_plan->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships
            && output_worksheet_relationships_plan->owner_part
                == worksheet_part.value(),
        "omitted-reference worksheet rewrite output should keep worksheet relationships audit");
    const auto* output_drawing_relationships_plan = find_output_entry_plan(
        output_plan.entries, "xl/drawings/_rels/drawing1.xml.rels");
    check_output_entry_plan(output_plan.entries, "xl/drawings/_rels/drawing1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "omitted-reference worksheet rewrite output should preserve drawing relationships part");
    check(output_drawing_relationships_plan != nullptr
            && output_drawing_relationships_plan->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships
            && output_drawing_relationships_plan->owner_part
                == drawing_part.value(),
        "omitted-reference worksheet rewrite output should keep drawing relationships audit");
    const auto* output_shared_strings_relationships_plan = find_output_entry_plan(
        output_plan.entries, "xl/_rels/sharedStrings.xml.rels");
    check_output_entry_plan(output_plan.entries, "xl/_rels/sharedStrings.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "omitted-reference worksheet rewrite output should preserve sharedStrings relationships part");
    check(output_shared_strings_relationships_plan != nullptr
            && output_shared_strings_relationships_plan->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships
            && output_shared_strings_relationships_plan->owner_part
            == shared_strings_part.value(),
        "omitted-reference worksheet rewrite output should keep sharedStrings relationships audit");
    const auto* output_opaque_extension_relationships_plan = find_output_entry_plan(
        output_plan.entries, "custom/_rels/opaque-extension.bin.rels");
    check_output_entry_plan(output_plan.entries, "custom/_rels/opaque-extension.bin.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "omitted-reference worksheet rewrite output should preserve unknown extension relationships part");
    check(output_opaque_extension_relationships_plan != nullptr
            && output_opaque_extension_relationships_plan->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships
            && output_opaque_extension_relationships_plan->owner_part
                == opaque_extension_part.value(),
        "omitted-reference worksheet rewrite output should keep unknown extension relationships audit");
    check(output_reader.content_types().override_for(calc_chain_part) == nullptr,
        "omitted-reference worksheet rewrite should still remove calcChain override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "omitted-reference worksheet rewrite should preserve table override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "omitted-reference worksheet rewrite should preserve VBA override");
}

void test_package_editor_reference_policy_fail_preserves_state()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-policy-fail-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-policy-fail-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName vba_part("/xl/vbaProject.bin");

    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t initial_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t initial_worksheet_payload_dependency_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t initial_workbook_payload_dependency_audit_count =
        editor.edit_plan().workbook_payload_dependency_audits().size();
    const std::size_t initial_package_entry_count = editor.edit_plan().package_entries().size();
    const std::size_t initial_removed_part_count = editor.edit_plan().removed_parts().size();
    const std::size_t initial_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const bool initial_full_calculation_on_load =
        editor.edit_plan().full_calculation_on_load();
    const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
        editor.edit_plan().calc_chain_action();
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "initial worksheet plan should be copy-original before policy failure");
    check(editor.manifest().find_part(calc_chain_part) != nullptr,
        "initial manifest should include calcChain before policy failure");

    fastxlsx::detail::ReferencePolicy fail_policy;
    fail_policy.unsupported_linked_part_action =
        fastxlsx::detail::ReferencePolicyAction::Fail;

    const std::string replacement_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetPr filterMode="1"/>)"
        R"(<dimension ref="A1:B2"/>)"
        R"(<sheetViews><sheetView workbookViewId="0"/></sheetViews>)"
        R"(<sheetFormatPr defaultRowHeight="15"/>)"
        R"(<cols><col min="1" max="2" width="16" customWidth="1"/></cols>)"
        R"(<sheetData><row r="1"><c r="A1" t="s"><v>0</v></c><c r="B1" s="1"><f>SUM(A1:A1)</f><v>7</v></c></row></sheetData>)"
        R"(<sheetProtection sheet="1"/>)"
        R"(<protectedRanges><protectedRange name="Locked" sqref="A1:B2"/></protectedRanges>)"
        R"(<autoFilter ref="A1:B2"/>)"
        R"(<mergeCells count="1"><mergeCell ref="A1:B1"/></mergeCells>)"
        R"(<dataValidations count="1"><dataValidation type="whole" sqref="A1:B2"><formula1>1</formula1></dataValidation></dataValidations>)"
        R"(<conditionalFormatting sqref="A1:B2"><cfRule type="expression" priority="1"><formula>$A$1&gt;0</formula></cfRule></conditionalFormatting>)"
        R"(<hyperlinks><hyperlink ref="A1" r:id="rId2"/></hyperlinks>)"
        R"(<printOptions horizontalCentered="1"/>)"
        R"(<pageMargins left="0.7" right="0.7" top="0.75" bottom="0.75" header="0.3" footer="0.3"/>)"
        R"(<pageSetup orientation="landscape"/>)"
        R"(<ignoredErrors><ignoredError sqref="A1:B2" numberStoredAsText="1"/></ignoredErrors>)"
        R"(<drawing r:id="rId1"/>)"
        R"(<tableParts count="1"><tablePart r:id="rId3"/></tableParts>)"
        R"(<extLst><ext uri="{fastxlsx-test}"/></extLst>)"
        R"(</worksheet>)";

    bool failed = false;
    try {
        replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_worksheet, fail_policy);
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed,
        "PackageEditor should fail worksheet replacement when policy blocks linked parts");

    check(editor.edit_plan().size() == initial_plan_size,
        "policy failure should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "policy failure should not change edit plan notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "shared string indexes"}),
        "policy failure should not leak worksheet sharedStrings payload audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "style id references"}),
        "policy failure should not leak worksheet styles payload audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "contains formulas"}),
        "policy failure should not leak worksheet formula payload audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "worksheet relationships"}),
        "policy failure should not leak worksheet relationship payload audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "autoFilter metadata"}),
        "policy failure should not leak worksheet autoFilter payload audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "sheet property metadata"}),
        "policy failure should not leak worksheet sheet property payload audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "dimension metadata"}),
        "policy failure should not leak worksheet dimension payload audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "view metadata"}),
        "policy failure should not leak worksheet view payload audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "default format metadata"}),
        "policy failure should not leak worksheet default format payload audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "column metadata"}),
        "policy failure should not leak worksheet column payload audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "data validation metadata"}),
        "policy failure should not leak worksheet data validation payload audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "print options metadata"}),
        "policy failure should not leak worksheet print options payload audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "page margins metadata"}),
        "policy failure should not leak worksheet page margins payload audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "page setup metadata"}),
        "policy failure should not leak worksheet page setup payload audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "extension metadata"}),
        "policy failure should not leak worksheet extension payload audit notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "policy failure should not change relationship target audit records");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "policy failure should not change worksheet relationship reference audit records");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == initial_worksheet_payload_dependency_audit_count,
        "policy failure should not change worksheet payload audit records");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == initial_workbook_payload_dependency_audit_count,
        "policy failure should not change workbook payload audit records");
    check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
        "policy failure should not change package-entry audit records");
    check(editor.edit_plan().removed_parts().size() == initial_removed_part_count,
        "policy failure should not record removed parts");
    check(editor.edit_plan().removed_package_entries().size()
            == initial_removed_package_entry_count,
        "policy failure should not record removed package-entry audit");
    check(editor.edit_plan().full_calculation_on_load()
            == initial_full_calculation_on_load,
        "policy failure should not request full calculation");
    check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
        "policy failure should not change calcChain action");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave linked drawing copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave linked table copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave VBA copy-original");
    check(editor.manifest().find_part(calc_chain_part) != nullptr,
        "policy failure should keep calcChain in the manifest");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave worksheet manifest copy-original");
    check_manifest_write_mode(editor, drawing_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave drawing manifest copy-original");
    check_manifest_write_mode(editor, table_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave table manifest copy-original");
    check_manifest_write_mode(editor, shared_strings_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave sharedStrings manifest copy-original");
    check_manifest_write_mode(editor, styles_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave styles manifest copy-original");
    check_manifest_write_mode(editor, vba_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave VBA manifest copy-original");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave calcChain manifest copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(editor.planned_output_entries().size() == output_plan.entries.size(),
        "policy failure should keep planned output snapshot consistent");
    check(output_plan.notes.size() == initial_note_count,
        "policy failure output plan should not add audit notes");
    check(output_plan.relationship_target_audits.size()
            == initial_relationship_target_audit_count,
        "policy failure output plan should not add relationship target audits");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == initial_worksheet_relationship_reference_audit_count,
        "policy failure output plan should not add worksheet reference audits");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == initial_worksheet_payload_dependency_audit_count,
        "policy failure output plan should not add worksheet payload audits");
    check(output_plan.workbook_payload_dependency_audits.size()
            == initial_workbook_payload_dependency_audit_count,
        "policy failure output plan should not add workbook payload audits");
    check(output_plan.removed_parts.size() == initial_removed_part_count,
        "policy failure output plan should not record removed parts");
    check(output_plan.removed_package_entries.size()
            == initial_removed_package_entry_count,
        "policy failure output plan should not record omitted metadata entries");
    check(output_plan.full_calculation_on_load == initial_full_calculation_on_load,
        "policy failure output plan should not request full calculation");
    check(output_plan.calc_chain_action == initial_calc_chain_action,
        "policy failure output plan should preserve calcChain policy");
    check_output_plan_preserves_source_copy_original(editor, output_plan,
        "policy failure output plan should preserve every source entry copy-original");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/sheet1.xml",
        true, worksheet_part.value(),
        "policy failure output plan should classify worksheet as a package part");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "policy failure output plan should classify content types as metadata entry");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "policy failure output should preserve worksheet bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "policy failure output should preserve calcChain bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "policy failure output should preserve content types bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "policy failure output should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "policy failure output should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "policy failure output should preserve drawing XML bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "policy failure output should preserve table XML bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "policy failure output should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "policy failure output should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "policy failure output should preserve VBA bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "policy failure output should preserve unknown extension bytes");
}

void test_package_editor_reference_policy_fail_preserves_prior_replacement()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-policy-fail-prior-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-policy-fail-prior-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::string replacement_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Queued" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<definedNames><definedName name="ReportRange">Sheet1!$A$1:$B$2</definedName></definedNames>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, replacement_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "queued workbook metadata replacement before policy failure");

    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    const std::size_t queued_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t queued_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t queued_worksheet_payload_dependency_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t queued_workbook_payload_dependency_audit_count =
        editor.edit_plan().workbook_payload_dependency_audits().size();
    const std::size_t queued_package_entry_count =
        editor.edit_plan().package_entries().size();
    const auto* queued_workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(queued_workbook_plan != nullptr,
        "queued policy-failure fixture should record prior workbook replacement");
    check(queued_workbook_plan->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "queued workbook replacement should be local-DOM-rewrite before policy failure");
    check(queued_workbook_plan->reason.find("queued workbook metadata replacement")
            != std::string::npos,
        "queued workbook replacement should keep its reason before policy failure");
    const auto* queued_workbook_relationships_plan =
        editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels");
    check(queued_workbook_relationships_plan != nullptr,
        "queued workbook replacement should audit preserved workbook relationships");
    check(queued_workbook_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "queued workbook relationships audit should be copy-original");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "queued workbook replacement should update manifest write mode before policy failure");

    fastxlsx::detail::ReferencePolicy fail_policy;
    fail_policy.unsupported_linked_part_action =
        fastxlsx::detail::ReferencePolicyAction::Fail;

    bool failed = false;
    try {
        replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, "<worksheet/>", fail_policy);
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed,
        "PackageEditor should still fail linked worksheet replacement after a queued edit");

    check(editor.edit_plan().size() == queued_plan_size,
        "policy failure should preserve prior edit-plan size");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "policy failure should not append notes to a queued edit plan");
    check(editor.edit_plan().relationship_target_audits().size()
            == queued_relationship_target_audit_count,
        "policy failure should not append relationship target audits to a queued edit plan");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == queued_worksheet_relationship_reference_audit_count,
        "policy failure should not append worksheet relationship reference audits to a queued edit plan");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == queued_worksheet_payload_dependency_audit_count,
        "policy failure should not append worksheet payload audits to a queued edit plan");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == queued_workbook_payload_dependency_audit_count,
        "policy failure should not append workbook payload audits to a queued edit plan");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "policy failure should preserve queued package-entry audit count");
    check(editor.edit_plan().removed_parts().empty(),
        "policy failure should not add removed parts after a queued edit");
    check(editor.edit_plan().removed_package_entries().empty(),
        "policy failure should not add removed package entries after a queued edit");
    check(!editor.edit_plan().full_calculation_on_load(),
        "policy failure should not request recalculation after a queued edit");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "policy failure should preserve calcChain action after a queued edit");
    const auto* final_workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(final_workbook_plan != nullptr
            && final_workbook_plan->write_mode
                == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "policy failure should keep prior workbook replacement in the edit plan");
    check(final_workbook_plan->reason.find("queued workbook metadata replacement")
            != std::string::npos,
        "policy failure should keep prior workbook replacement reason");
    const auto* final_workbook_relationships_plan =
        editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels");
    check(final_workbook_relationships_plan != nullptr
            && final_workbook_relationships_plan->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should keep prior workbook relationships audit");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave worksheet copy-original after a queued edit");
    check(editor.manifest().find_part(calc_chain_part) != nullptr,
        "policy failure should keep calcChain in manifest after a queued edit");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "policy failure should keep prior workbook manifest write mode");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave worksheet manifest copy-original after a queued edit");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave calcChain manifest copy-original after a queued edit");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(editor.planned_output_entries().size() == output_plan.entries.size(),
        "queued workbook policy failure should keep planned output snapshot consistent");
    check(output_plan.notes.size() == queued_note_count,
        "queued workbook policy failure output plan should not append notes");
    check(output_plan.relationship_target_audits.size()
            == queued_relationship_target_audit_count,
        "queued workbook policy failure output plan should not append relationship target audits");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == queued_worksheet_relationship_reference_audit_count,
        "queued workbook policy failure output plan should not append worksheet reference audits");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == queued_worksheet_payload_dependency_audit_count,
        "queued workbook policy failure output plan should not append worksheet payload audits");
    check(output_plan.workbook_payload_dependency_audits.size()
            == queued_workbook_payload_dependency_audit_count,
        "queued workbook policy failure output plan should not append workbook payload audits");
    check(output_plan.removed_parts.empty(),
        "queued workbook policy failure output plan should not record removed parts");
    check(output_plan.removed_package_entries.empty(),
        "queued workbook policy failure output plan should not record omitted metadata entries");
    check(!output_plan.full_calculation_on_load,
        "queued workbook policy failure output plan should not request recalculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "queued workbook policy failure output plan should preserve calcChain policy");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "queued workbook policy failure output plan should keep workbook rewrite");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "queued workbook policy failure output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "queued workbook policy failure output plan should preserve worksheet copy-original");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "queued workbook policy failure output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "queued workbook policy failure output plan should preserve calcChain copy-original");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "queued workbook policy failure output plan should preserve content types copy-original");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "queued workbook policy failure output plan should preserve unknown extension copy-original");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/workbook.xml") == replacement_workbook,
        "policy failure output should keep prior workbook replacement bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "policy failure output should preserve workbook relationships for prior replacement");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "policy failure output should preserve worksheet bytes after a queued edit");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "policy failure output should preserve worksheet relationships after a queued edit");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "policy failure output should preserve calcChain bytes after a queued edit");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "policy failure output should preserve content types after a queued edit");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "policy failure output should preserve unknown bytes after a queued edit");
}

void test_package_editor_reference_policy_fail_preserves_prior_document_properties()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-policy-fail-docprops-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-policy-fail-docprops-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName app_part("/docProps/app.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    fastxlsx::DocumentProperties properties;
    properties.creator = "Queued Patch Author";
    properties.last_modified_by = "Queued Patch Reviewer";
    properties.title = "Queued metadata";
    properties.description = "Preserved after linked worksheet policy failure";
    properties.application = "FastXLSX Patch";
    properties.app_version = "4.1";
    editor.set_document_properties(properties);

    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    const std::size_t queued_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t queued_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t queued_worksheet_payload_dependency_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t queued_workbook_payload_dependency_audit_count =
        editor.edit_plan().workbook_payload_dependency_audits().size();
    const std::size_t queued_package_entry_count =
        editor.edit_plan().package_entries().size();
    const auto* queued_core_plan = editor.edit_plan().find_part(core_part);
    check(queued_core_plan != nullptr
            && queued_core_plan->write_mode
                == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "queued docProps fixture should record generated core properties");
    const auto* queued_app_plan = editor.edit_plan().find_part(app_part);
    check(queued_app_plan != nullptr
            && queued_app_plan->write_mode
                == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "queued docProps fixture should record generated app properties");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") != nullptr,
        "queued docProps fixture should audit content types rewrite");
    check(editor.edit_plan().find_package_entry("_rels/.rels") != nullptr,
        "queued docProps fixture should audit package relationships rewrite");
    check_manifest_write_mode(editor, core_part,
        fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "queued docProps fixture should mark core manifest generated");
    check_manifest_write_mode(editor, app_part,
        fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "queued docProps fixture should mark app manifest generated");

    fastxlsx::detail::ReferencePolicy fail_policy;
    fail_policy.unsupported_linked_part_action =
        fastxlsx::detail::ReferencePolicyAction::Fail;

    bool failed = false;
    try {
        replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, "<worksheet/>", fail_policy);
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed,
        "PackageEditor should fail linked worksheet replacement after queued docProps");

    check(editor.edit_plan().size() == queued_plan_size,
        "policy failure should preserve queued docProps edit-plan size");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "policy failure should not append notes after queued docProps");
    check(editor.edit_plan().relationship_target_audits().size()
            == queued_relationship_target_audit_count,
        "policy failure should not append relationship target audits after queued docProps");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == queued_worksheet_relationship_reference_audit_count,
        "policy failure should not append worksheet relationship reference audits after queued docProps");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == queued_worksheet_payload_dependency_audit_count,
        "policy failure should not append worksheet payload audits after queued docProps");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == queued_workbook_payload_dependency_audit_count,
        "policy failure should not append workbook payload audits after queued docProps");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "policy failure should preserve queued docProps package-entry audit count");
    check(editor.edit_plan().removed_parts().empty(),
        "policy failure should not add removed parts after queued docProps");
    check(editor.edit_plan().removed_package_entries().empty(),
        "policy failure should not add removed package entries after queued docProps");
    check(!editor.edit_plan().full_calculation_on_load(),
        "policy failure should not request recalculation after queued docProps");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "policy failure should preserve calcChain policy after queued docProps");
    check(editor.edit_plan().find_part(core_part)->write_mode
            == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "policy failure should keep generated core properties in edit plan");
    check(editor.edit_plan().find_part(app_part)->write_mode
            == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "policy failure should keep generated app properties in edit plan");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave worksheet copy-original after queued docProps");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave workbook copy-original after queued docProps");
    check(editor.manifest().find_part(calc_chain_part) != nullptr,
        "policy failure should keep calcChain in manifest after queued docProps");
    check_manifest_write_mode(editor, core_part,
        fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "policy failure should keep generated core manifest state");
    check_manifest_write_mode(editor, app_part,
        fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "policy failure should keep generated app manifest state");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave worksheet manifest copy-original after queued docProps");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave calcChain manifest copy-original after queued docProps");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(editor.planned_output_entries().size() == output_plan.entries.size(),
        "queued docProps policy failure should keep planned output snapshot consistent");
    check(output_plan.notes.size() == queued_note_count,
        "queued docProps policy failure output plan should not append notes");
    check(output_plan.relationship_target_audits.size()
            == queued_relationship_target_audit_count,
        "queued docProps policy failure output plan should not append relationship target audits");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == queued_worksheet_relationship_reference_audit_count,
        "queued docProps policy failure output plan should not append worksheet reference audits");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == queued_worksheet_payload_dependency_audit_count,
        "queued docProps policy failure output plan should not append worksheet payload audits");
    check(output_plan.workbook_payload_dependency_audits.size()
            == queued_workbook_payload_dependency_audit_count,
        "queued docProps policy failure output plan should not append workbook payload audits");
    check(output_plan.removed_parts.empty(),
        "queued docProps policy failure output plan should not record removed parts");
    check(output_plan.removed_package_entries.empty(),
        "queued docProps policy failure output plan should not record omitted metadata entries");
    check(!output_plan.full_calculation_on_load,
        "queued docProps policy failure output plan should not request recalculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "queued docProps policy failure output plan should preserve calcChain policy");
    const auto* output_core_plan =
        find_output_entry_plan(output_plan.entries, "docProps/core.xml");
    check(output_core_plan != nullptr
            && output_core_plan->write_mode
                == fastxlsx::detail::PartWriteMode::GenerateSmallXml
            && output_core_plan->package_part
            && output_core_plan->part_name == core_part.value()
            && !output_core_plan->omitted,
        "queued docProps policy failure output plan should keep generated core properties");
    const auto* output_app_plan =
        find_output_entry_plan(output_plan.entries, "docProps/app.xml");
    check(output_app_plan != nullptr
            && output_app_plan->write_mode
                == fastxlsx::detail::PartWriteMode::GenerateSmallXml
            && output_app_plan->package_part
            && output_app_plan->part_name == app_part.value()
            && !output_app_plan->omitted,
        "queued docProps policy failure output plan should keep generated app properties");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "queued docProps policy failure output plan should rewrite content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "queued docProps policy failure output plan should rewrite package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "queued docProps policy failure output plan should preserve workbook copy-original");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "queued docProps policy failure output plan should preserve worksheet copy-original");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "queued docProps policy failure output plan should preserve calcChain copy-original");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "queued docProps policy failure output plan should preserve unknown extension copy-original");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string core_xml = output_reader.read_entry("docProps/core.xml");
    check_contains(core_xml, "<dc:creator>Queued Patch Author</dc:creator>",
        "policy failure output should keep queued core properties creator");
    check_contains(core_xml, "<cp:lastModifiedBy>Queued Patch Reviewer</cp:lastModifiedBy>",
        "policy failure output should keep queued core properties modifier");
    check_contains(core_xml, "<dc:title>Queued metadata</dc:title>",
        "policy failure output should keep queued core properties title");
    const std::string app_xml = output_reader.read_entry("docProps/app.xml");
    check_contains(app_xml, "<Application>FastXLSX Patch</Application>",
        "policy failure output should keep queued app properties application");
    check_contains(app_xml, "<AppVersion>4.1</AppVersion>",
        "policy failure output should keep queued app properties version");
    check_contains(output_reader.read_entry("[Content_Types].xml"), "/docProps/app.xml",
        "policy failure output should keep queued docProps content type");
    check_contains(output_reader.read_entry("_rels/.rels"), "Target=\"docProps/app.xml\"",
        "policy failure output should keep queued docProps package relationship");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "policy failure output should preserve workbook bytes after queued docProps");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "policy failure output should preserve worksheet bytes after queued docProps");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "policy failure output should preserve calcChain bytes after queued docProps");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "policy failure output should preserve unknown extension bytes after queued docProps");
}

void test_package_editor_reference_policy_request_recalculation_updates_workbook_metadata()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-policy-recalc-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-policy-recalc-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    fastxlsx::detail::ReferencePolicy policy;
    policy.request_full_calculation_on_sheet_rewrite = false;
    policy.unsupported_linked_part_action =
        fastxlsx::detail::ReferencePolicyAction::RequestRecalculation;
    policy.calc_chain_action = fastxlsx::detail::CalcChainAction::Preserve;

    const std::string replacement_sheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>101</v></c></row></sheetData>)"
        R"(<drawing r:id="rId1"/>)"
        R"(<tableParts count="1"><tablePart r:id="rId3"/></tableParts>)"
        R"(</worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_sheet, policy);

    check(editor.edit_plan().full_calculation_on_load(),
        "request-recalculation policy should request full calculation for linked worksheet rewrite");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "request-recalculation policy should preserve its calcChain action");
    check(editor.edit_plan().removed_parts().empty(),
        "request-recalculation preserve policy should not remove calcChain");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "request-recalculation policy should record workbook metadata rewrite");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "request-recalculation policy should local-DOM-rewrite workbook metadata");
    check(workbook_plan->reason.find("definedNames") != std::string::npos,
        "request-recalculation workbook rewrite should preserve definedNames review context");
    const auto* workbook_relationships_plan =
        editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels");
    check(workbook_relationships_plan != nullptr,
        "request-recalculation policy should audit preserved workbook relationships");
    check(workbook_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "preserved workbook relationships should be copy-original in package-entry audit");
    check(workbook_relationships_plan->reason.find("/xl/workbook.xml") != std::string::npos,
        "preserved workbook relationships audit should name the owner part");
    const auto* workbook_manifest_part = editor.manifest().find_part(workbook_part);
    check(workbook_manifest_part != nullptr,
        "request-recalculation policy should keep workbook in the manifest");
    check(workbook_manifest_part->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "request-recalculation policy manifest should mirror workbook metadata rewrite mode");
    check(workbook_manifest_part->dirty && !workbook_manifest_part->preserve_original
            && !workbook_manifest_part->generated,
        "request-recalculation policy manifest should mark workbook metadata dirty");
    check(editor.manifest().find_part(calc_chain_part) != nullptr,
        "request-recalculation preserve policy should keep calcChain in the manifest");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load,
        "request-recalculation output plan should expose full calculation request");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "request-recalculation output plan should expose calcChain preserve policy");
    check(output_plan.relationship_target_audits.size()
            == editor.edit_plan().relationship_target_audits().size(),
        "request-recalculation output plan should snapshot relationship target audits");
    check(has_note_containing(output_plan.notes,
              {"worksheet relationships are preserved", "policy review"}),
        "request-recalculation output plan should snapshot dependency audit notes");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "request-recalculation output plan should keep calcChain copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") != entries.end(),
        "request-recalculation preserve output should keep calcChain.xml");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_sheet,
        "request-recalculation output should replace worksheet XML");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "request-recalculation preserve output should preserve calcChain bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "request-recalculation preserve output should preserve content types bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "request-recalculation preserve output should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "request-recalculation preserve output should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "request-recalculation preserve output should preserve styles bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "request-recalculation preserve output should preserve unknown extension bytes");

    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "request-recalculation output should request full calculation on load");
    check_contains(workbook_xml, R"(<definedNames><definedName name="ReportRange">Sheet1!$A$1:$B$2</definedName></definedNames>)",
        "request-recalculation output should preserve workbook defined names");
}

void test_package_editor_rejects_calc_chain_rebuild_without_state_changes()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-rebuild-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-rebuild-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t initial_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    fastxlsx::detail::ReferencePolicy policy;
    policy.calc_chain_action = fastxlsx::detail::CalcChainAction::Rebuild;

    bool failed = false;
    try {
        replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, "<worksheet/>", policy);
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed,
        "PackageEditor should reject calcChain rebuild because it is not implemented");

    check(editor.edit_plan().size() == initial_plan_size,
        "calcChain rebuild failure should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "calcChain rebuild failure should not change edit plan notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "calcChain rebuild failure should not change relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "calcChain rebuild failure should not change worksheet relationship reference audits");
    check(editor.edit_plan().removed_parts().empty(),
        "calcChain rebuild failure should not record removed parts");
    check(editor.edit_plan().package_entries().empty(),
        "calcChain rebuild failure should not record package-entry audit");
    check(editor.edit_plan().removed_package_entries().empty(),
        "calcChain rebuild failure should not record removed package-entry audit");
    check(!editor.edit_plan().full_calculation_on_load(),
        "calcChain rebuild failure should not request full calculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "calcChain rebuild failure should not change calcChain action");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "calcChain rebuild failure should leave worksheet copy-original");
    check(editor.manifest().find_part(calc_chain_part) != nullptr,
        "calcChain rebuild failure should keep calcChain in the manifest");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "calcChain rebuild failure should leave worksheet manifest copy-original");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "calcChain rebuild failure should leave calcChain manifest copy-original");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "calcChain rebuild failure output should preserve worksheet bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "calcChain rebuild failure output should preserve calcChain bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "calcChain rebuild failure output should preserve content types bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "calcChain rebuild failure output should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "calcChain rebuild failure output should preserve workbook XML bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "calcChain rebuild failure output should preserve unknown bytes");
}

void test_package_editor_rejects_malformed_workbook_metadata_without_state_changes()
{
    CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-malformed-workbook-source.xlsx");
    source.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)";
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

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-malformed-workbook-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t initial_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    bool failed = false;
    try {
        replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, "<worksheet/>");
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed,
        "PackageEditor should reject workbook metadata rewrite when workbook XML is malformed");

    check(editor.edit_plan().size() == initial_plan_size,
        "malformed workbook failure should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "malformed workbook failure should not change edit plan notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "malformed workbook failure should not change relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "malformed workbook failure should not change worksheet relationship reference audits");
    check(editor.edit_plan().removed_parts().empty(),
        "malformed workbook failure should not record removed parts");
    check(editor.edit_plan().package_entries().empty(),
        "malformed workbook failure should not record package-entry audit");
    check(editor.edit_plan().removed_package_entries().empty(),
        "malformed workbook failure should not record removed package-entry audit");
    check(!editor.edit_plan().full_calculation_on_load(),
        "malformed workbook failure should not request full calculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "malformed workbook failure should not change calcChain action");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "malformed workbook failure should leave worksheet copy-original");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "malformed workbook failure should leave workbook copy-original");
    check(editor.manifest().find_part(calc_chain_part) != nullptr,
        "malformed workbook failure should keep calcChain in the manifest");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "malformed workbook failure should leave worksheet manifest copy-original");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "malformed workbook failure should leave workbook manifest copy-original");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "malformed workbook failure should leave calcChain manifest copy-original");
    check(editor.manifest().content_types().override_for(calc_chain_part) != nullptr,
        "malformed workbook failure should keep calcChain content type override");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "malformed workbook failure output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "malformed workbook failure output plan should preserve calcChain policy");
    check(output_plan.notes.size() == initial_note_count,
        "malformed workbook failure output plan should not add audit notes");
    check(output_plan.relationship_target_audits.size()
            == initial_relationship_target_audit_count,
        "malformed workbook failure output plan should not add relationship target audits");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == initial_worksheet_relationship_reference_audit_count,
        "malformed workbook failure output plan should not add worksheet reference audits");
    check(output_plan.removed_parts.empty(),
        "malformed workbook failure output plan should not record removed parts");
    check(output_plan.removed_package_entries.empty(),
        "malformed workbook failure output plan should not record omitted metadata entries");
    check_output_plan_preserves_source_copy_original(editor, output_plan,
        "malformed workbook failure output plan should keep every source entry copy-original");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "malformed workbook failure output plan should classify content types as metadata entry");
    check_output_entry_part_context(output_plan.entries, "_rels/.rels", false, "",
        "malformed workbook failure output plan should classify package relationships as metadata entry");
    check_output_entry_part_context(output_plan.entries, "xl/workbook.xml", true,
        workbook_part.value(),
        "malformed workbook failure output plan should classify workbook as a package part");
    check_output_entry_part_context(output_plan.entries, "xl/_rels/workbook.xml.rels",
        false, "",
        "malformed workbook failure output plan should classify workbook relationships as metadata entry");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/sheet1.xml", true,
        worksheet_part.value(),
        "malformed workbook failure output plan should classify worksheet as a package part");
    check_output_entry_part_context(output_plan.entries, "xl/calcChain.xml", true,
        calc_chain_part.value(),
        "malformed workbook failure output plan should classify calcChain as a package part");

    {
        fastxlsx::detail::PackageEditor cell_editor =
            fastxlsx::detail::PackageEditor::open(source.path);
        const std::vector<std::filesystem::path> temp_files_before =
            package_editor_temp_files();
        const std::array replacements {
            worksheet_cell_replacement("A1", R"(<c r="A1"><v>7</v></c>)"),
        };

        bool cell_replacement_failed = false;
        try {
            cell_editor.replace_worksheet_cells(worksheet_part, replacements);
        } catch (const std::exception& error) {
            cell_replacement_failed = true;
            check_contains(error.what(), "workbook XML closing tag",
                "malformed workbook cell replacement failure should report workbook XML preflight");
        }
        check(cell_replacement_failed,
            "cell replacement should reject malformed workbook metadata after output staging");
        check(cell_editor.edit_plan().size() == initial_plan_size,
            "malformed workbook cell replacement failure should not change edit plan size");
        check(!cell_editor.edit_plan().full_calculation_on_load(),
            "malformed workbook cell replacement failure should not request full calculation");
        check_manifest_write_mode(cell_editor, worksheet_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "malformed workbook cell replacement failure should leave worksheet manifest copy-original");
        check_no_new_package_editor_temp_files(temp_files_before,
            "malformed workbook cell replacement failure should clean staged output temp file immediately");
    }

    {
        fastxlsx::detail::PackageEditor sheet_data_editor =
            fastxlsx::detail::PackageEditor::open(source.path);
        const std::vector<std::filesystem::path> temp_files_before =
            package_editor_temp_files();

        bool sheet_data_failed = false;
        try {
            replace_worksheet_sheet_data_from_single_chunk_source(sheet_data_editor, worksheet_part,
                R"(<sheetData><row r="1"><c r="A1"><v>9</v></c></row></sheetData>)");
        } catch (const std::exception& error) {
            sheet_data_failed = true;
            check_contains(error.what(), "workbook XML closing tag",
                "malformed workbook sheetData failure should report workbook XML preflight");
        }
        check(sheet_data_failed,
            "sheetData replacement should reject malformed workbook metadata after output staging");
        check(sheet_data_editor.edit_plan().size() == initial_plan_size,
            "malformed workbook sheetData failure should not change edit plan size");
        check(sheet_data_editor.edit_plan().notes().size() == initial_note_count,
            "malformed workbook sheetData failure should not change edit plan notes");
        check(!sheet_data_editor.edit_plan().full_calculation_on_load(),
            "malformed workbook sheetData failure should not request full calculation");
        check_manifest_write_mode(sheet_data_editor, worksheet_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "malformed workbook sheetData failure should leave worksheet manifest copy-original");
        check_no_new_package_editor_temp_files(temp_files_before,
            "malformed workbook sheetData failure should clean staged temp files immediately");
    }

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "malformed workbook failure output should preserve workbook XML bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "malformed workbook failure output should preserve worksheet bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "malformed workbook failure output should preserve calcChain bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "malformed workbook failure output should preserve content types bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "malformed workbook failure output should preserve workbook relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "malformed workbook failure output should preserve unknown bytes");
}

void test_package_editor_rejects_worksheet_rewrite_without_workbook_metadata()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-editor-missing-workbook-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-missing-workbook-output.xlsx");

    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(</Types>)";
    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"/>)";
    const std::string worksheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData></worksheet>)";
    const std::string unknown = std::string("missing-workbook\0unknown", 24);

    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/worksheets/sheet1.xml", worksheet},
            {"custom/opaque.bin", unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source_path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t initial_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();

    bool failed = false;
    try {
        replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, "<worksheet/>");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "officeDocument relationship",
            "missing workbook failure should report workbook metadata requirement");
    }
    check(failed,
        "PackageEditor should reject worksheet replacement without workbook metadata");

    check(editor.edit_plan().size() == initial_plan_size,
        "missing workbook failure should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "missing workbook failure should not change edit plan notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "missing workbook failure should not change relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "missing workbook failure should not change worksheet relationship reference audits");
    check(editor.edit_plan().removed_parts().empty(),
        "missing workbook failure should not record removed parts");
    check(editor.edit_plan().package_entries().empty(),
        "missing workbook failure should not record package-entry audit");
    check(editor.edit_plan().removed_package_entries().empty(),
        "missing workbook failure should not record removed package-entry audit");
    check(!editor.edit_plan().full_calculation_on_load(),
        "missing workbook failure should not request full calculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "missing workbook failure should not change calcChain action");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "missing workbook failure should leave worksheet copy-original");
    check(editor.edit_plan().find_part(workbook_part) == nullptr,
        "missing workbook failure should not invent workbook edit-plan entries");
    check(editor.manifest().find_part(workbook_part) == nullptr,
        "missing workbook failure should not invent workbook manifest parts");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "missing workbook failure should leave worksheet manifest copy-original");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.find_entry("xl/workbook.xml") == nullptr,
        "missing workbook failure output should not create workbook XML");
    check(output_reader.find_entry("xl/_rels/workbook.xml.rels") == nullptr,
        "missing workbook failure output should not create workbook relationships");
    check(output_reader.read_entry("[Content_Types].xml") == content_types,
        "missing workbook failure output should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == package_relationships,
        "missing workbook failure output should preserve package relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == worksheet,
        "missing workbook failure output should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == unknown,
        "missing workbook failure output should preserve unknown bytes");
}

void test_package_editor_rejects_saving_over_source_package()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-overwrite-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-overwrite-safe-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const std::string replacement_core =
        "<cp:coreProperties><dc:creator>Unsafe</dc:creator></cp:coreProperties>";
    editor.replace_part(core_part, replacement_core,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite);
    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    const std::size_t queued_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t queued_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const std::size_t queued_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t queued_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t queued_worksheet_payload_dependency_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t queued_workbook_payload_dependency_audit_count =
        editor.edit_plan().workbook_payload_dependency_audits().size();
    const bool queued_full_calculation_on_load =
        editor.edit_plan().full_calculation_on_load();
    const fastxlsx::detail::CalcChainAction queued_calc_chain_action =
        editor.edit_plan().calc_chain_action();

    bool failed = false;
    try {
        editor.save_as(source.path);
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed,
        "PackageEditor should reject saving over the source package");

    const std::filesystem::path equivalent_source_path =
        source.path.parent_path() / "." / source.path.filename();
    bool equivalent_failed = false;
    try {
        editor.save_as(equivalent_source_path);
    } catch (const std::exception&) {
        equivalent_failed = true;
    }
    check(equivalent_failed,
        "PackageEditor should reject saving over a path-equivalent source package");

    bool empty_path_failed = false;
    try {
        editor.save_as(std::filesystem::path());
    } catch (const std::exception&) {
        empty_path_failed = true;
    }
    check(empty_path_failed,
        "PackageEditor should reject saving to an empty output path");

    const std::filesystem::path missing_parent_output =
        output_path("fastxlsx-package-editor-missing-parent-output") / "out.xlsx";
    bool missing_parent_failed = false;
    try {
        editor.save_as(missing_parent_output);
    } catch (const std::exception&) {
        missing_parent_failed = true;
    }
    check(missing_parent_failed,
        "PackageEditor should reject saving under a missing output parent directory");

    const std::filesystem::path parent_file_output =
        output_path("fastxlsx-package-editor-parent-file-output");
    {
        std::ofstream parent_file(parent_file_output, std::ios::binary);
        parent_file << "not a directory";
        check(parent_file.good(),
            "test setup should create a non-directory output parent");
    }
    bool parent_file_failed = false;
    try {
        editor.save_as(parent_file_output / "out.xlsx");
    } catch (const std::exception&) {
        parent_file_failed = true;
    }
    check(parent_file_failed,
        "PackageEditor should reject saving under a non-directory output parent");

    const std::filesystem::path directory_output =
        output_path("fastxlsx-package-editor-directory-output");
    std::filesystem::create_directory(directory_output);
    bool directory_failed = false;
    try {
        editor.save_as(directory_output);
    } catch (const std::exception&) {
        directory_failed = true;
    }
    check(directory_failed,
        "PackageEditor should reject saving to an existing directory");

    check(editor.edit_plan().size() == queued_plan_size,
        "save_as guard rejection should not change queued edit-plan entries");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "save_as guard rejection should not add edit-plan notes");
    check(editor.edit_plan().removed_parts().size() == queued_removed_part_count,
        "save_as guard rejection should not change removed-part audits");
    check(editor.edit_plan().removed_package_entries().size()
            == queued_removed_package_entry_count,
        "save_as guard rejection should not change removed package-entry audits");
    check(editor.edit_plan().relationship_target_audits().size()
            == queued_relationship_target_audit_count,
        "save_as guard rejection should not change relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == queued_worksheet_relationship_reference_audit_count,
        "save_as guard rejection should not change worksheet reference audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == queued_worksheet_payload_dependency_audit_count,
        "save_as guard rejection should not change worksheet payload audits");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == queued_workbook_payload_dependency_audit_count,
        "save_as guard rejection should not change workbook payload audits");
    check(editor.edit_plan().full_calculation_on_load()
            == queued_full_calculation_on_load,
        "save_as guard rejection should not change fullCalcOnLoad intent");
    check(editor.edit_plan().calc_chain_action() == queued_calc_chain_action,
        "save_as guard rejection should not change calcChain policy");
    const auto* core_plan = editor.edit_plan().find_part(core_part);
    check(core_plan != nullptr
            && core_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "save_as guard rejection should keep queued core replacement active");
    check_manifest_write_mode(editor, core_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "save_as guard rejection should keep manifest replacement active");
    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(editor.planned_output_entries().size() == output_plan.entries.size(),
        "save_as guard rejection should keep planned output snapshot consistent");
    check(output_plan.removed_parts.size() == queued_removed_part_count,
        "save_as guard rejection output plan should keep removed-part audits stable");
    check(output_plan.removed_package_entries.size()
            == queued_removed_package_entry_count,
        "save_as guard rejection output plan should keep package-entry audits stable");
    check(output_plan.relationship_target_audits.size()
            == queued_relationship_target_audit_count,
        "save_as guard rejection output plan should keep relationship audits stable");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == queued_worksheet_relationship_reference_audit_count,
        "save_as guard rejection output plan should keep worksheet reference audits stable");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == queued_worksheet_payload_dependency_audit_count,
        "save_as guard rejection output plan should keep worksheet payload audits stable");
    check(output_plan.workbook_payload_dependency_audits.size()
            == queued_workbook_payload_dependency_audit_count,
        "save_as guard rejection output plan should keep workbook payload audits stable");
    check(output_plan.full_calculation_on_load == queued_full_calculation_on_load,
        "save_as guard rejection output plan should keep fullCalcOnLoad intent stable");
    check(output_plan.calc_chain_action == queued_calc_chain_action,
        "save_as guard rejection output plan should keep calcChain policy stable");
    check_output_entry_plan(output_plan.entries, "docProps/core.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "save_as guard rejection should keep planned core rewrite");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "save_as guard rejection should keep planned unknown copy-original");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    check(source_reader.read_entry("docProps/core.xml") == source.core_properties,
        "save_as guard rejection should preserve source core properties");
    check(source_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "save_as guard rejection should preserve source worksheet bytes");
    check(source_reader.read_entry("custom/opaque.bin") == source.unknown,
        "save_as guard rejection should preserve source unknown bytes");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("docProps/core.xml") == replacement_core,
        "save_as guard rejection should allow later safe output of queued replacement");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "later safe output should preserve worksheet bytes after rejected save_as guard");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "later safe output should preserve unknown bytes after rejected save_as guard");
}

void test_package_editor_save_as_copy_original_read_failure_preserves_state_and_output()
{
    const std::vector<std::filesystem::path> temp_files_before =
        package_editor_temp_files();
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-copy-failure-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-copy-failure-output.xlsx");
    const std::string output_sentinel = "do not overwrite this failed output";
    write_binary_file(output, output_sentinel);
    const std::vector<std::filesystem::path> output_temp_files_before =
        package_editor_output_sibling_temp_files(output);

    const std::string original_source_bytes = fastxlsx::test::read_file(source.path);
    std::string corrupted_source_bytes = original_source_bytes;
    corrupt_first_occurrence(corrupted_source_bytes,
        std::string_view(source.unknown.data(), source.unknown.size()));
    write_binary_file(source.path, corrupted_source_bytes);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName opaque_part("/custom/opaque.bin");
    const std::string replacement_core =
        "<cp:coreProperties><dc:creator>Queued</dc:creator></cp:coreProperties>";
    editor.replace_part(core_part, replacement_core,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite);

    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    const std::size_t queued_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t queued_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t queued_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const std::size_t queued_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t queued_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t queued_worksheet_payload_dependency_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t queued_workbook_payload_dependency_audit_count =
        editor.edit_plan().workbook_payload_dependency_audits().size();
    const bool queued_full_calculation_on_load =
        editor.edit_plan().full_calculation_on_load();
    const fastxlsx::detail::CalcChainAction queued_calc_chain_action =
        editor.edit_plan().calc_chain_action();
    const fastxlsx::detail::PackageEditorOutputPlan queued_output_plan =
        editor.planned_output();

    bool failed = false;
    try {
        editor.save_as(output);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(),
            "failed to materialize planned output source-copy entry 'custom/opaque.bin'",
            "copy-original failure should include planned output entry materialization context");
        check_contains(error.what(), "failed to copy source package entry",
            "copy-original failure should include copy context");
        check_contains(error.what(), "custom/opaque.bin",
            "copy-original failure should include the source entry name");
        check_contains(error.what(), "CRC",
            "copy-original failure should preserve the reader failure reason");
    }
    check(failed,
        "PackageEditor should reject save_as when a copy-original source entry cannot be read");

    check(editor.edit_plan().size() == queued_plan_size,
        "copy-original read failure should not change queued edit-plan entries");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "copy-original read failure should not add edit-plan notes");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "copy-original read failure should not change package-entry audits");
    check(editor.edit_plan().removed_parts().size() == queued_removed_part_count,
        "copy-original read failure should not change removed-part audits");
    check(editor.edit_plan().removed_package_entries().size()
            == queued_removed_package_entry_count,
        "copy-original read failure should not change removed package-entry audits");
    check(editor.edit_plan().relationship_target_audits().size()
            == queued_relationship_target_audit_count,
        "copy-original read failure should not change relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == queued_worksheet_relationship_reference_audit_count,
        "copy-original read failure should not change worksheet reference audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == queued_worksheet_payload_dependency_audit_count,
        "copy-original read failure should not change worksheet payload audits");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == queued_workbook_payload_dependency_audit_count,
        "copy-original read failure should not change workbook payload audits");
    check(editor.edit_plan().full_calculation_on_load()
            == queued_full_calculation_on_load,
        "copy-original read failure should not change fullCalcOnLoad intent");
    check(editor.edit_plan().calc_chain_action() == queued_calc_chain_action,
        "copy-original read failure should not change calcChain policy");
    check_manifest_write_mode(editor, core_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "copy-original read failure should keep queued core replacement active");
    check_manifest_write_mode(editor, opaque_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "copy-original read failure should keep opaque part copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.entries.size() == queued_output_plan.entries.size(),
        "copy-original read failure should keep output-plan entry count stable");
    check(output_plan.notes.size() == queued_output_plan.notes.size(),
        "copy-original read failure should keep output-plan notes stable");
    check(output_plan.removed_parts.size() == queued_output_plan.removed_parts.size(),
        "copy-original read failure should keep output-plan removed parts stable");
    check(output_plan.removed_package_entries.size()
            == queued_output_plan.removed_package_entries.size(),
        "copy-original read failure should keep output-plan package-entry omissions stable");
    check(output_plan.relationship_target_audits.size()
            == queued_output_plan.relationship_target_audits.size(),
        "copy-original read failure should keep output-plan relationship audits stable");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == queued_output_plan.worksheet_relationship_reference_audits.size(),
        "copy-original read failure should keep output-plan worksheet audits stable");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == queued_output_plan.worksheet_payload_dependency_audits.size(),
        "copy-original read failure should keep output-plan worksheet payload audits stable");
    check(output_plan.workbook_payload_dependency_audits.size()
            == queued_output_plan.workbook_payload_dependency_audits.size(),
        "copy-original read failure should keep output-plan workbook payload audits stable");
    check(output_plan.full_calculation_on_load
            == queued_output_plan.full_calculation_on_load,
        "copy-original read failure should keep output-plan fullCalcOnLoad stable");
    check(output_plan.calc_chain_action == queued_output_plan.calc_chain_action,
        "copy-original read failure should keep output-plan calcChain policy stable");
    check_output_entry_plan(output_plan.entries, "docProps/core.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "copy-original read failure should keep planned core rewrite");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "copy-original read failure should keep planned opaque copy-original");
    check(fastxlsx::test::read_file(output) == output_sentinel,
        "copy-original read failure should not overwrite an existing output file");
    check_no_new_package_editor_temp_files(temp_files_before,
        "copy-original read failure should clean staged source-copy temp files");
    check_no_new_package_editor_output_sibling_temp_files(output_temp_files_before, output,
        "copy-original read failure should clean committed-output sibling temp files");

    write_binary_file(source.path, original_source_bytes);
    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("docProps/core.xml") == replacement_core,
        "later safe output should write queued core replacement after copy-original failure");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "later safe output should preserve copied unknown bytes after copy-original failure");
}

void test_package_editor_save_as_rejects_mutated_source_copy_temp_size_without_state_changes()
{
    const std::vector<std::filesystem::path> temp_files_before =
        package_editor_temp_files();
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-source-copy-temp-size-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-source-copy-temp-size-output.xlsx");
    const std::string output_sentinel = "do not overwrite this source-copy temp failure output";
    write_binary_file(output, output_sentinel);
    const std::vector<std::filesystem::path> output_temp_files_before =
        package_editor_output_sibling_temp_files(output);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName opaque_part("/custom/opaque.bin");
    const std::string replacement_core =
        "<cp:coreProperties><dc:creator>TempContract</dc:creator></cp:coreProperties>";
    editor.replace_part(core_part, replacement_core,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite);

    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    const std::size_t queued_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t queued_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t queued_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const std::size_t queued_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t queued_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t queued_worksheet_payload_dependency_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t queued_workbook_payload_dependency_audit_count =
        editor.edit_plan().workbook_payload_dependency_audits().size();
    const bool queued_full_calculation_on_load =
        editor.edit_plan().full_calculation_on_load();
    const fastxlsx::detail::CalcChainAction queued_calc_chain_action =
        editor.edit_plan().calc_chain_action();
    const fastxlsx::detail::PackageEditorOutputPlan queued_output_plan =
        editor.planned_output();

    bool failed = false;
    {
        ScopedPackageEditorSourceCopyTempFilesHook hook(
            truncate_package_editor_source_copy_temp_files);
        try {
            editor.save_as(output);
        } catch (const std::exception& error) {
            failed = true;
            check_contains(error.what(), "failed to write PackageEditor output package",
                "source-copy temp size failure should include PackageEditor write context");
            check_contains(error.what(), "ZIP entry '",
                "source-copy temp size failure should include ZIP entry context");
            check_contains(error.what(), "chunk 0",
                "source-copy temp size failure should include chunk index context");
            check_contains(error.what(), "ZIP entry chunk size changed after staging",
                "source-copy temp size failure should enforce expected-size metadata");
            check_contains(error.what(), "actual 0 bytes",
                "source-copy temp size failure should report the truncated temp size");
            check_contains(error.what(), "fastxlsx-package-editor-",
                "source-copy temp size failure should include the file-backed temp path");
        }
    }
    check(failed,
        "PackageEditor should reject source-copy temp files whose size changes before write");

    check(editor.edit_plan().size() == queued_plan_size,
        "source-copy temp size failure should not change queued edit-plan entries");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "source-copy temp size failure should not add edit-plan notes");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "source-copy temp size failure should not change package-entry audits");
    check(editor.edit_plan().removed_parts().size() == queued_removed_part_count,
        "source-copy temp size failure should not change removed-part audits");
    check(editor.edit_plan().removed_package_entries().size()
            == queued_removed_package_entry_count,
        "source-copy temp size failure should not change removed package-entry audits");
    check(editor.edit_plan().relationship_target_audits().size()
            == queued_relationship_target_audit_count,
        "source-copy temp size failure should not change relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == queued_worksheet_relationship_reference_audit_count,
        "source-copy temp size failure should not change worksheet reference audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == queued_worksheet_payload_dependency_audit_count,
        "source-copy temp size failure should not change worksheet payload audits");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == queued_workbook_payload_dependency_audit_count,
        "source-copy temp size failure should not change workbook payload audits");
    check(editor.edit_plan().full_calculation_on_load()
            == queued_full_calculation_on_load,
        "source-copy temp size failure should not change fullCalcOnLoad intent");
    check(editor.edit_plan().calc_chain_action() == queued_calc_chain_action,
        "source-copy temp size failure should not change calcChain policy");
    check_manifest_write_mode(editor, core_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "source-copy temp size failure should keep queued core replacement active");
    check_manifest_write_mode(editor, opaque_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-copy temp size failure should keep opaque part copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.entries.size() == queued_output_plan.entries.size(),
        "source-copy temp size failure should keep output-plan entry count stable");
    check(output_plan.notes.size() == queued_output_plan.notes.size(),
        "source-copy temp size failure should keep output-plan notes stable");
    check(output_plan.removed_parts.size() == queued_output_plan.removed_parts.size(),
        "source-copy temp size failure should keep output-plan removed parts stable");
    check(output_plan.removed_package_entries.size()
            == queued_output_plan.removed_package_entries.size(),
        "source-copy temp size failure should keep output-plan package-entry omissions stable");
    check(output_plan.relationship_target_audits.size()
            == queued_output_plan.relationship_target_audits.size(),
        "source-copy temp size failure should keep output-plan relationship audits stable");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == queued_output_plan.worksheet_relationship_reference_audits.size(),
        "source-copy temp size failure should keep output-plan worksheet audits stable");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == queued_output_plan.worksheet_payload_dependency_audits.size(),
        "source-copy temp size failure should keep output-plan worksheet payload audits stable");
    check(output_plan.workbook_payload_dependency_audits.size()
            == queued_output_plan.workbook_payload_dependency_audits.size(),
        "source-copy temp size failure should keep output-plan workbook payload audits stable");
    check(output_plan.full_calculation_on_load
            == queued_output_plan.full_calculation_on_load,
        "source-copy temp size failure should keep output-plan fullCalcOnLoad stable");
    check(output_plan.calc_chain_action == queued_output_plan.calc_chain_action,
        "source-copy temp size failure should keep output-plan calcChain policy stable");
    check_output_entry_plan(output_plan.entries, "docProps/core.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "source-copy temp size failure should keep planned core rewrite");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "source-copy temp size failure should keep planned opaque copy-original");
    check(fastxlsx::test::read_file(output) == output_sentinel,
        "source-copy temp size failure should not overwrite an existing output file");
    check_no_new_package_editor_temp_files(temp_files_before,
        "source-copy temp size failure should clean staged source-copy temp files");
    check_no_new_package_editor_output_sibling_temp_files(output_temp_files_before, output,
        "source-copy temp size failure should clean committed-output sibling temp files");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("docProps/core.xml") == replacement_core,
        "later safe output should write queued core replacement after temp size failure");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "later safe output should preserve copied unknown bytes after temp size failure");
}

void test_package_editor_save_as_rejects_missing_source_copy_temp_with_expected_size()
{
    const std::vector<std::filesystem::path> temp_files_before =
        package_editor_temp_files();
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-source-copy-temp-missing-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-source-copy-temp-missing-output.xlsx");
    const std::string output_sentinel =
        "do not overwrite this missing source-copy temp failure output";
    write_binary_file(output, output_sentinel);
    const std::vector<std::filesystem::path> output_temp_files_before =
        package_editor_output_sibling_temp_files(output);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName opaque_part("/custom/opaque.bin");
    const std::string replacement_core =
        "<cp:coreProperties><dc:creator>MissingTemp</dc:creator></cp:coreProperties>";
    editor.replace_part(core_part, replacement_core,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite);

    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    const std::size_t queued_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t queued_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t queued_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const bool queued_full_calculation_on_load =
        editor.edit_plan().full_calculation_on_load();
    const fastxlsx::detail::CalcChainAction queued_calc_chain_action =
        editor.edit_plan().calc_chain_action();
    const fastxlsx::detail::PackageEditorOutputPlan queued_output_plan =
        editor.planned_output();

    bool failed = false;
    {
        ScopedPackageEditorSourceCopyTempFilesHook hook(
            delete_package_editor_source_copy_temp_files);
        try {
            editor.save_as(output);
        } catch (const std::exception& error) {
            failed = true;
            check_contains(error.what(), "failed to write PackageEditor output package",
                "missing source-copy temp failure should include PackageEditor write context");
            check_contains(error.what(), "ZIP entry '",
                "missing source-copy temp failure should include ZIP entry context");
            check_contains(error.what(), "chunk 0",
                "missing source-copy temp failure should include chunk index context");
            check_contains(error.what(), "failed to stat file-backed ZIP entry chunk",
                "missing source-copy temp failure should report preflight stat failure");
            check_contains(error.what(), "expected ",
                "missing source-copy temp failure should report expected-size metadata");
            check_contains(error.what(), " bytes",
                "missing source-copy temp failure should keep expected byte units");
            check_contains(error.what(), "fastxlsx-package-editor-",
                "missing source-copy temp failure should include the file-backed temp path");
        }
    }
    check(failed,
        "PackageEditor should reject deleted source-copy temp files before output commit");

    check(editor.edit_plan().size() == queued_plan_size,
        "missing source-copy temp failure should not change queued edit-plan entries");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "missing source-copy temp failure should not add edit-plan notes");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "missing source-copy temp failure should not change package-entry audits");
    check(editor.edit_plan().removed_parts().size() == queued_removed_part_count,
        "missing source-copy temp failure should not change removed-part audits");
    check(editor.edit_plan().removed_package_entries().size()
            == queued_removed_package_entry_count,
        "missing source-copy temp failure should not change removed package-entry audits");
    check(editor.edit_plan().full_calculation_on_load()
            == queued_full_calculation_on_load,
        "missing source-copy temp failure should not change fullCalcOnLoad intent");
    check(editor.edit_plan().calc_chain_action() == queued_calc_chain_action,
        "missing source-copy temp failure should not change calcChain policy");
    check_manifest_write_mode(editor, core_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "missing source-copy temp failure should keep queued core replacement active");
    check_manifest_write_mode(editor, opaque_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "missing source-copy temp failure should keep opaque part copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.entries.size() == queued_output_plan.entries.size(),
        "missing source-copy temp failure should keep output-plan entry count stable");
    check(output_plan.notes.size() == queued_output_plan.notes.size(),
        "missing source-copy temp failure should keep output-plan notes stable");
    check(output_plan.full_calculation_on_load
            == queued_output_plan.full_calculation_on_load,
        "missing source-copy temp failure should keep output-plan fullCalcOnLoad stable");
    check(output_plan.calc_chain_action == queued_output_plan.calc_chain_action,
        "missing source-copy temp failure should keep output-plan calcChain policy stable");
    check_output_entry_plan(output_plan.entries, "docProps/core.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "missing source-copy temp failure should keep planned core rewrite");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "missing source-copy temp failure should keep planned opaque copy-original");
    check(fastxlsx::test::read_file(output) == output_sentinel,
        "missing source-copy temp failure should not overwrite an existing output file");
    check_no_new_package_editor_temp_files(temp_files_before,
        "missing source-copy temp failure should clean staged source-copy temp files");
    check_no_new_package_editor_output_sibling_temp_files(output_temp_files_before, output,
        "missing source-copy temp failure should clean committed-output sibling temp files");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("docProps/core.xml") == replacement_core,
        "later safe output should write queued core replacement after missing temp failure");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "later safe output should preserve copied unknown bytes after missing temp failure");
}

void test_package_editor_save_as_rejects_mutated_source_copy_temp_crc_without_state_changes()
{
    const std::vector<std::filesystem::path> temp_files_before =
        package_editor_temp_files();
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-source-copy-temp-crc-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-source-copy-temp-crc-output.xlsx");
    const std::string output_sentinel = "do not overwrite this source-copy crc failure output";
    write_binary_file(output, output_sentinel);
    const std::vector<std::filesystem::path> output_temp_files_before =
        package_editor_output_sibling_temp_files(output);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName opaque_part("/custom/opaque.bin");
    const std::string replacement_core =
        "<cp:coreProperties><dc:creator>TempCrcContract</dc:creator></cp:coreProperties>";
    editor.replace_part(core_part, replacement_core,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite);

    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    const std::size_t queued_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t queued_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t queued_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const std::size_t queued_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t queued_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t queued_worksheet_payload_dependency_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t queued_workbook_payload_dependency_audit_count =
        editor.edit_plan().workbook_payload_dependency_audits().size();
    const bool queued_full_calculation_on_load =
        editor.edit_plan().full_calculation_on_load();
    const fastxlsx::detail::CalcChainAction queued_calc_chain_action =
        editor.edit_plan().calc_chain_action();
    const fastxlsx::detail::PackageEditorOutputPlan queued_output_plan =
        editor.planned_output();

    bool failed = false;
    {
        ScopedPackageEditorSourceCopyTempFilesHook hook(
            rewrite_package_editor_source_copy_temp_files_same_size);
        try {
            editor.save_as(output);
        } catch (const std::exception& error) {
            failed = true;
            check_contains(error.what(), "failed to write PackageEditor output package",
                "source-copy temp CRC failure should include PackageEditor write context");
            check_contains(error.what(), "ZIP entry '",
                "source-copy temp CRC failure should include ZIP entry context");
            check_contains(error.what(), "chunk 0",
                "source-copy temp CRC failure should include chunk index context");
            check_contains(error.what(), "ZIP entry chunk CRC32 changed after staging",
                "source-copy temp CRC failure should enforce expected-CRC metadata");
            check_contains(error.what(), "actual ",
                "source-copy temp CRC failure should report actual CRC");
            check_contains(error.what(), "fastxlsx-package-editor-",
                "source-copy temp CRC failure should include the file-backed temp path");
        }
    }
    check(failed,
        "PackageEditor should reject source-copy temp files whose CRC changes before write");

    check(editor.edit_plan().size() == queued_plan_size,
        "source-copy temp CRC failure should not change queued edit-plan entries");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "source-copy temp CRC failure should not add edit-plan notes");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "source-copy temp CRC failure should not change package-entry audits");
    check(editor.edit_plan().removed_parts().size() == queued_removed_part_count,
        "source-copy temp CRC failure should not change removed-part audits");
    check(editor.edit_plan().removed_package_entries().size()
            == queued_removed_package_entry_count,
        "source-copy temp CRC failure should not change removed package-entry audits");
    check(editor.edit_plan().relationship_target_audits().size()
            == queued_relationship_target_audit_count,
        "source-copy temp CRC failure should not change relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == queued_worksheet_relationship_reference_audit_count,
        "source-copy temp CRC failure should not change worksheet reference audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == queued_worksheet_payload_dependency_audit_count,
        "source-copy temp CRC failure should not change worksheet payload audits");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == queued_workbook_payload_dependency_audit_count,
        "source-copy temp CRC failure should not change workbook payload audits");
    check(editor.edit_plan().full_calculation_on_load()
            == queued_full_calculation_on_load,
        "source-copy temp CRC failure should not change fullCalcOnLoad intent");
    check(editor.edit_plan().calc_chain_action() == queued_calc_chain_action,
        "source-copy temp CRC failure should not change calcChain policy");
    check_manifest_write_mode(editor, core_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "source-copy temp CRC failure should keep queued core replacement active");
    check_manifest_write_mode(editor, opaque_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-copy temp CRC failure should keep opaque part copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.entries.size() == queued_output_plan.entries.size(),
        "source-copy temp CRC failure should keep output-plan entry count stable");
    check(output_plan.notes.size() == queued_output_plan.notes.size(),
        "source-copy temp CRC failure should keep output-plan notes stable");
    check(output_plan.removed_parts.size() == queued_output_plan.removed_parts.size(),
        "source-copy temp CRC failure should keep output-plan removed parts stable");
    check(output_plan.removed_package_entries.size()
            == queued_output_plan.removed_package_entries.size(),
        "source-copy temp CRC failure should keep output-plan package-entry omissions stable");
    check(output_plan.relationship_target_audits.size()
            == queued_output_plan.relationship_target_audits.size(),
        "source-copy temp CRC failure should keep output-plan relationship audits stable");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == queued_output_plan.worksheet_relationship_reference_audits.size(),
        "source-copy temp CRC failure should keep output-plan worksheet audits stable");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == queued_output_plan.worksheet_payload_dependency_audits.size(),
        "source-copy temp CRC failure should keep output-plan worksheet payload audits stable");
    check(output_plan.workbook_payload_dependency_audits.size()
            == queued_output_plan.workbook_payload_dependency_audits.size(),
        "source-copy temp CRC failure should keep output-plan workbook payload audits stable");
    check(output_plan.full_calculation_on_load
            == queued_output_plan.full_calculation_on_load,
        "source-copy temp CRC failure should keep output-plan fullCalcOnLoad stable");
    check(output_plan.calc_chain_action == queued_output_plan.calc_chain_action,
        "source-copy temp CRC failure should keep output-plan calcChain policy stable");
    check_output_entry_plan(output_plan.entries, "docProps/core.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "source-copy temp CRC failure should keep planned core rewrite");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "source-copy temp CRC failure should keep planned opaque copy-original");
    check(fastxlsx::test::read_file(output) == output_sentinel,
        "source-copy temp CRC failure should not overwrite an existing output file");
    check_no_new_package_editor_temp_files(temp_files_before,
        "source-copy temp CRC failure should clean staged source-copy temp files");
    check_no_new_package_editor_output_sibling_temp_files(output_temp_files_before, output,
        "source-copy temp CRC failure should clean committed-output sibling temp files");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("docProps/core.xml") == replacement_core,
        "later safe output should write queued core replacement after temp CRC failure");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "later safe output should preserve copied unknown bytes after temp CRC failure");
}

void test_package_editor_save_as_writer_failure_preserves_state_and_output()
{
    const std::vector<std::filesystem::path> temp_files_before =
        package_editor_temp_files();
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-writer-failure-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-writer-failure-output.xlsx");
    const std::string output_sentinel = "do not overwrite this writer failure output";
    write_binary_file(output, output_sentinel);
    const std::vector<std::filesystem::path> output_temp_files_before =
        package_editor_output_sibling_temp_files(output);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName opaque_part("/custom/opaque.bin");
    const std::string replacement_core =
        "<cp:coreProperties><dc:creator>Writer</dc:creator></cp:coreProperties>";
    editor.replace_part(core_part, replacement_core,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite);

    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    const std::size_t queued_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t queued_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t queued_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const std::size_t queued_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t queued_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t queued_worksheet_payload_dependency_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t queued_workbook_payload_dependency_audit_count =
        editor.edit_plan().workbook_payload_dependency_audits().size();
    const bool queued_full_calculation_on_load =
        editor.edit_plan().full_calculation_on_load();
    const fastxlsx::detail::CalcChainAction queued_calc_chain_action =
        editor.edit_plan().calc_chain_action();
    const fastxlsx::detail::PackageEditorOutputPlan queued_output_plan =
        editor.planned_output();

    fastxlsx::detail::PackageWriterOptions options;
    options.backend = static_cast<fastxlsx::detail::PackageWriterBackend>(999);

    bool failed = false;
    try {
        editor.save_as(output, options);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "failed to write PackageEditor output package",
            "writer failure should include PackageEditor write context");
        check_contains(error.what(), "fastxlsx-package-editor-writer-failure-output.xlsx",
            "writer failure should include the output package path");
        check_contains(error.what(), "unsupported package writer backend",
            "writer failure should preserve the backend failure reason");
    }
    check(failed,
        "PackageEditor should reject save_as when the selected writer backend fails");

    check(editor.edit_plan().size() == queued_plan_size,
        "writer failure should not change queued edit-plan entries");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "writer failure should not add edit-plan notes");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "writer failure should not change package-entry audits");
    check(editor.edit_plan().removed_parts().size() == queued_removed_part_count,
        "writer failure should not change removed-part audits");
    check(editor.edit_plan().removed_package_entries().size()
            == queued_removed_package_entry_count,
        "writer failure should not change removed package-entry audits");
    check(editor.edit_plan().relationship_target_audits().size()
            == queued_relationship_target_audit_count,
        "writer failure should not change relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == queued_worksheet_relationship_reference_audit_count,
        "writer failure should not change worksheet reference audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == queued_worksheet_payload_dependency_audit_count,
        "writer failure should not change worksheet payload audits");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == queued_workbook_payload_dependency_audit_count,
        "writer failure should not change workbook payload audits");
    check(editor.edit_plan().full_calculation_on_load()
            == queued_full_calculation_on_load,
        "writer failure should not change fullCalcOnLoad intent");
    check(editor.edit_plan().calc_chain_action() == queued_calc_chain_action,
        "writer failure should not change calcChain policy");
    check_manifest_write_mode(editor, core_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "writer failure should keep queued core replacement active");
    check_manifest_write_mode(editor, opaque_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "writer failure should keep opaque part copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.entries.size() == queued_output_plan.entries.size(),
        "writer failure should keep output-plan entry count stable");
    check(output_plan.notes.size() == queued_output_plan.notes.size(),
        "writer failure should keep output-plan notes stable");
    check(output_plan.removed_parts.size() == queued_output_plan.removed_parts.size(),
        "writer failure should keep output-plan removed parts stable");
    check(output_plan.removed_package_entries.size()
            == queued_output_plan.removed_package_entries.size(),
        "writer failure should keep output-plan package-entry omissions stable");
    check(output_plan.relationship_target_audits.size()
            == queued_output_plan.relationship_target_audits.size(),
        "writer failure should keep output-plan relationship audits stable");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == queued_output_plan.worksheet_relationship_reference_audits.size(),
        "writer failure should keep output-plan worksheet audits stable");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == queued_output_plan.worksheet_payload_dependency_audits.size(),
        "writer failure should keep output-plan worksheet payload audits stable");
    check(output_plan.workbook_payload_dependency_audits.size()
            == queued_output_plan.workbook_payload_dependency_audits.size(),
        "writer failure should keep output-plan workbook payload audits stable");
    check(output_plan.full_calculation_on_load
            == queued_output_plan.full_calculation_on_load,
        "writer failure should keep output-plan fullCalcOnLoad stable");
    check(output_plan.calc_chain_action == queued_output_plan.calc_chain_action,
        "writer failure should keep output-plan calcChain policy stable");
    check_output_entry_plan(output_plan.entries, "docProps/core.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "writer failure should keep planned core rewrite");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "writer failure should keep planned opaque copy-original");
    check(fastxlsx::test::read_file(output) == output_sentinel,
        "writer failure should not overwrite an existing output file");
    check_no_new_package_editor_temp_files(temp_files_before,
        "writer failure should clean staged source-copy temp files");
    check_no_new_package_editor_output_sibling_temp_files(output_temp_files_before, output,
        "writer failure should clean committed-output sibling temp files");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("docProps/core.xml") == replacement_core,
        "later safe output should write queued core replacement after writer failure");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "later safe output should preserve unknown bytes after writer failure");
}

void test_package_editor_rejects_invalid_replacements()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-invalid-source.xlsx");
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t initial_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t initial_worksheet_payload_dependency_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t initial_workbook_payload_dependency_audit_count =
        editor.edit_plan().workbook_payload_dependency_audits().size();
    const std::size_t initial_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t initial_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t initial_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const bool initial_full_calculation_on_load =
        editor.edit_plan().full_calculation_on_load();
    const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
        editor.edit_plan().calc_chain_action();
    expect_replace_failure(editor,
        fastxlsx::detail::PartName("/xl/missing.xml"),
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "PackageEditor should reject replacement for missing parts");
    expect_replace_failure(editor,
        core_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "PackageEditor should reject copy-original replacement mode");
    bool worksheet_failed = false;
    try {
        replace_worksheet_part_from_single_chunk_source(editor, core_part, "<worksheet/>");
    } catch (const std::exception&) {
        worksheet_failed = true;
    }
    check(worksheet_failed,
        "PackageEditor should reject worksheet replacement for non-worksheet parts");
    check(editor.edit_plan().size() == initial_plan_size,
        "failed replacements should not alter the edit plan");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "failed replacements should not alter edit plan notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "failed replacements should not alter relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "failed replacements should not alter worksheet relationship reference audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == initial_worksheet_payload_dependency_audit_count,
        "failed replacements should not alter worksheet payload audits");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == initial_workbook_payload_dependency_audit_count,
        "failed replacements should not alter workbook payload audits");
    check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
        "failed replacements should not alter package-entry audit");
    check(editor.edit_plan().removed_parts().size() == initial_removed_part_count,
        "failed replacements should not record removed parts");
    check(editor.edit_plan().removed_package_entries().size()
            == initial_removed_package_entry_count,
        "failed replacements should not record removed package-entry audit");
    check(editor.edit_plan().full_calculation_on_load()
            == initial_full_calculation_on_load,
        "failed replacements should not alter full calculation policy");
    check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
        "failed replacements should not alter calcChain action");
    check(editor.edit_plan().find_part(core_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "failed replacements should leave core properties copy-original in the edit plan");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "failed replacements should leave worksheet copy-original in the edit plan");
    check_manifest_write_mode(editor, core_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "failed replacements should leave core properties copy-original in the manifest");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "failed replacements should leave worksheet copy-original in the manifest");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load == initial_full_calculation_on_load,
        "failed replacements output plan should not request full calculation");
    check(output_plan.calc_chain_action == initial_calc_chain_action,
        "failed replacements output plan should preserve calcChain policy");
    check(output_plan.notes.size() == initial_note_count,
        "failed replacements output plan should not add audit notes");
    check(output_plan.relationship_target_audits.size()
            == initial_relationship_target_audit_count,
        "failed replacements output plan should not add relationship target audits");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == initial_worksheet_relationship_reference_audit_count,
        "failed replacements output plan should not add worksheet reference audits");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == initial_worksheet_payload_dependency_audit_count,
        "failed replacements output plan should not add worksheet payload audits");
    check(output_plan.workbook_payload_dependency_audits.size()
            == initial_workbook_payload_dependency_audit_count,
        "failed replacements output plan should not add workbook payload audits");
    check(output_plan.removed_parts.size() == initial_removed_part_count,
        "failed replacements output plan should not record removed parts");
    check(output_plan.removed_package_entries.size()
            == initial_removed_package_entry_count,
        "failed replacements output plan should not record omitted metadata entries");
    check_output_plan_preserves_source_copy_original(editor, output_plan,
        "failed replacements output plan should keep every source entry copy-original");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "failed replacements output plan should classify content types as metadata entry");
    check_output_entry_part_context(output_plan.entries, "_rels/.rels", false, "",
        "failed replacements output plan should classify package relationships as metadata entry");
    check_output_entry_part_context(output_plan.entries, "docProps/core.xml", true,
        core_part.value(),
        "failed replacements output plan should classify core properties as a package part");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/sheet1.xml", true,
        worksheet_part.value(),
        "failed replacements output plan should classify worksheet as a package part");
    check_output_entry_part_context(output_plan.entries, "custom/opaque.bin", true,
        "/custom/opaque.bin",
        "failed replacements output plan should classify unknown bytes as a package part");

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-invalid-output.xlsx");
    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "failed replacements should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "failed replacements should preserve package relationships bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "failed replacements should preserve workbook relationships bytes");
    check(output_reader.read_entry("docProps/core.xml") == source.core_properties,
        "failed replacements should preserve core properties bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "failed replacements should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "failed replacements should preserve unknown bytes");
}

void test_package_editor_rejects_metadata_entry_replacements_without_state_changes()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-metadata-targets-source.xlsx");
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t initial_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t initial_worksheet_payload_dependency_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t initial_workbook_payload_dependency_audit_count =
        editor.edit_plan().workbook_payload_dependency_audits().size();
    const std::size_t initial_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t initial_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t initial_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const bool initial_full_calculation_on_load =
        editor.edit_plan().full_calculation_on_load();
    const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
        editor.edit_plan().calc_chain_action();
    expect_replace_failure_containing(editor,
        fastxlsx::detail::PartName("[Content_Types].xml"),
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "metadata package entries",
        "PackageEditor should reject content-types replacement as an ordinary part");
    expect_replace_failure_containing(editor,
        fastxlsx::detail::PartName("_rels/.rels"),
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "metadata package entries",
        "PackageEditor should reject package relationships replacement as an ordinary part");
    expect_replace_failure_containing(editor,
        fastxlsx::detail::PartName("xl/_rels/workbook.xml.rels"),
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "metadata package entries",
        "PackageEditor should reject source-owned relationships replacement as an ordinary part");
    expect_replace_failure_containing(editor,
        fastxlsx::detail::PartName("_rels/root.xml.rels"),
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "metadata package entries",
        "PackageEditor should reject root source-owned relationships replacement as an ordinary part");
    check(editor.edit_plan().size() == initial_plan_size,
        "metadata replacement failures should not alter the edit plan");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "metadata replacement failures should not alter edit plan notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "metadata replacement failures should not alter relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "metadata replacement failures should not alter worksheet relationship reference audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == initial_worksheet_payload_dependency_audit_count,
        "metadata replacement failures should not alter worksheet payload audits");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == initial_workbook_payload_dependency_audit_count,
        "metadata replacement failures should not alter workbook payload audits");
    check(editor.edit_plan().removed_parts().size() == initial_removed_part_count,
        "metadata replacement failures should not record removed parts");
    check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
        "metadata replacement failures should not record package-entry audit");
    check(editor.edit_plan().removed_package_entries().size()
            == initial_removed_package_entry_count,
        "metadata replacement failures should not record removed package-entry audit");
    check(editor.edit_plan().full_calculation_on_load()
            == initial_full_calculation_on_load,
        "metadata replacement failures should not request full calculation");
    check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
        "metadata replacement failures should not change calcChain action");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "metadata replacement failures should not audit content types rewrite");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "metadata replacement failures should not audit package relationships rewrite");
    check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels") == nullptr,
        "metadata replacement failures should not audit workbook relationships rewrite");
    check(editor.edit_plan().find_part(core_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "metadata replacement failures should leave core properties copy-original");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "metadata replacement failures should leave workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "metadata replacement failures should leave worksheet copy-original");
    check_manifest_write_mode(editor, core_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "metadata replacement failures should leave core manifest copy-original");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "metadata replacement failures should leave workbook manifest copy-original");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "metadata replacement failures should leave worksheet manifest copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load == initial_full_calculation_on_load,
        "metadata replacement failures output plan should not request full calculation");
    check(output_plan.calc_chain_action == initial_calc_chain_action,
        "metadata replacement failures output plan should preserve calcChain policy");
    check(output_plan.notes.size() == initial_note_count,
        "metadata replacement failures output plan should not add audit notes");
    check(output_plan.relationship_target_audits.size()
            == initial_relationship_target_audit_count,
        "metadata replacement failures output plan should not add relationship target audits");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == initial_worksheet_relationship_reference_audit_count,
        "metadata replacement failures output plan should not add worksheet reference audits");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == initial_worksheet_payload_dependency_audit_count,
        "metadata replacement failures output plan should not add worksheet payload audits");
    check(output_plan.workbook_payload_dependency_audits.size()
            == initial_workbook_payload_dependency_audit_count,
        "metadata replacement failures output plan should not add workbook payload audits");
    check(output_plan.removed_parts.size() == initial_removed_part_count,
        "metadata replacement failures output plan should not record removed parts");
    check(output_plan.removed_package_entries.size()
            == initial_removed_package_entry_count,
        "metadata replacement failures output plan should not record omitted metadata entries");
    check_output_plan_preserves_source_copy_original(editor, output_plan,
        "metadata replacement failures output plan should keep every source entry copy-original");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "metadata replacement failures output plan should classify content types as metadata entry");
    check_output_entry_part_context(output_plan.entries, "_rels/.rels", false, "",
        "metadata replacement failures output plan should classify package relationships as metadata entry");
    check_output_entry_part_context(output_plan.entries, "xl/_rels/workbook.xml.rels",
        false, "",
        "metadata replacement failures output plan should classify workbook relationships as metadata entry");
    check_output_entry_part_context(output_plan.entries, "docProps/core.xml", true,
        core_part.value(),
        "metadata replacement failures output plan should classify core properties as a package part");
    check_output_entry_part_context(output_plan.entries, "xl/workbook.xml", true,
        workbook_part.value(),
        "metadata replacement failures output plan should classify workbook as a package part");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/sheet1.xml", true,
        worksheet_part.value(),
        "metadata replacement failures output plan should classify worksheet as a package part");

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-metadata-targets-output.xlsx");
    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "metadata replacement failure output should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "metadata replacement failure output should preserve package relationships bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "metadata replacement failure output should preserve workbook relationships bytes");
    check(output_reader.read_entry("docProps/core.xml") == source.core_properties,
        "metadata replacement failure output should preserve ordinary part bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "metadata replacement failure output should preserve unknown bytes");
}

void test_package_editor_rejects_invalid_removals_without_state_changes()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-invalid-removal-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-invalid-removal-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t initial_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t initial_worksheet_payload_dependency_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t initial_workbook_payload_dependency_audit_count =
        editor.edit_plan().workbook_payload_dependency_audits().size();
    const std::size_t initial_package_entry_count = editor.edit_plan().package_entries().size();
    const std::size_t initial_removed_part_count = editor.edit_plan().removed_parts().size();
    const std::size_t initial_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const bool initial_full_calculation_on_load =
        editor.edit_plan().full_calculation_on_load();
    const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
        editor.edit_plan().calc_chain_action();

    bool missing_failed = false;
    try {
        editor.remove_part(
            fastxlsx::detail::PartName("/xl/missing.xml"), "missing part removal");
    } catch (const std::exception& error) {
        missing_failed = true;
        check_contains(error.what(), "not present",
            "missing removal failure should report missing source part");
    }
    check(missing_failed,
        "PackageEditor should reject removal for missing parts");

    bool content_types_failed = false;
    try {
        editor.remove_part(fastxlsx::detail::PartName("/[Content_Types].xml"),
            "content types removal");
    } catch (const std::exception& error) {
        content_types_failed = true;
        check_contains(error.what(), "metadata package entries",
            "content-types removal failure should report metadata entry restriction");
    }
    check(content_types_failed,
        "PackageEditor should reject content-types removal as an ordinary part");

    bool package_relationships_failed = false;
    try {
        editor.remove_part(fastxlsx::detail::PartName("/_rels/.rels"),
            "package relationships removal");
    } catch (const std::exception& error) {
        package_relationships_failed = true;
        check_contains(error.what(), "metadata package entries",
            "package relationships removal failure should report metadata entry restriction");
    }
    check(package_relationships_failed,
        "PackageEditor should reject package relationships removal as an ordinary part");

    bool source_relationships_failed = false;
    try {
        editor.remove_part(fastxlsx::detail::PartName("/xl/_rels/workbook.xml.rels"),
            "source relationships removal");
    } catch (const std::exception& error) {
        source_relationships_failed = true;
        check_contains(error.what(), "metadata package entries",
            "source relationships removal failure should report metadata entry restriction");
    }
    check(source_relationships_failed,
        "PackageEditor should reject source-owned relationships removal as an ordinary part");

    check(editor.edit_plan().size() == initial_plan_size,
        "invalid removal failures should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "invalid removal failures should not change edit plan notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "invalid removal failures should not change relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "invalid removal failures should not change worksheet relationship reference audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == initial_worksheet_payload_dependency_audit_count,
        "invalid removal failures should not change worksheet payload audits");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == initial_workbook_payload_dependency_audit_count,
        "invalid removal failures should not change workbook payload audits");
    check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
        "invalid removal failures should not change package-entry audit");
    check(editor.edit_plan().removed_parts().size() == initial_removed_part_count,
        "invalid removal failures should not record removed parts");
    check(editor.edit_plan().removed_package_entries().size()
            == initial_removed_package_entry_count,
        "invalid removal failures should not record removed package-entry audit");
    check(editor.edit_plan().full_calculation_on_load()
            == initial_full_calculation_on_load,
        "invalid removal failures should not request full calculation");
    check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
        "invalid removal failures should not change calcChain action");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "invalid removal failures should leave workbook copy-original");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "invalid removal failures should leave workbook manifest copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load == initial_full_calculation_on_load,
        "invalid removal failures output plan should not request full calculation");
    check(output_plan.calc_chain_action == initial_calc_chain_action,
        "invalid removal failures output plan should preserve calcChain policy");
    check(output_plan.notes.size() == initial_note_count,
        "invalid removal failures output plan should not add audit notes");
    check(output_plan.relationship_target_audits.size()
            == initial_relationship_target_audit_count,
        "invalid removal failures output plan should not add relationship target audits");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == initial_worksheet_relationship_reference_audit_count,
        "invalid removal failures output plan should not add worksheet reference audits");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == initial_worksheet_payload_dependency_audit_count,
        "invalid removal failures output plan should not add worksheet payload audits");
    check(output_plan.workbook_payload_dependency_audits.size()
            == initial_workbook_payload_dependency_audit_count,
        "invalid removal failures output plan should not add workbook payload audits");
    check(output_plan.removed_parts.size() == initial_removed_part_count,
        "invalid removal failures output plan should not record removed parts");
    check(output_plan.removed_package_entries.size()
            == initial_removed_package_entry_count,
        "invalid removal failures output plan should not record omitted metadata entries");
    check(output_plan.entries.size() == editor.reader().entries().size(),
        "invalid removal failures output plan should keep one decision per source entry");
    check(editor.planned_output_entries().size() == output_plan.entries.size(),
        "invalid removal failures legacy output-entry preview should match aggregate plan");
    for (const fastxlsx::detail::PackageReaderEntry& entry : editor.reader().entries()) {
        check_output_entry_plan(output_plan.entries, entry.name,
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "invalid removal failures output plan should keep every source entry copy-original");
    }
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "invalid removal failures output plan should classify content types as metadata entry");
    check_output_entry_part_context(output_plan.entries, "_rels/.rels", false, "",
        "invalid removal failures output plan should classify package relationships as metadata entry");
    check_output_entry_part_context(output_plan.entries, "xl/workbook.xml", true,
        workbook_part.value(),
        "invalid removal failures output plan should classify workbook as a package part");
    check_output_entry_part_context(output_plan.entries, "xl/_rels/workbook.xml.rels",
        false, "",
        "invalid removal failures output plan should classify workbook relationships as metadata entry");
    check_output_entry_part_context(output_plan.entries, "custom/opaque-extension.bin",
        true, "/custom/opaque-extension.bin",
        "invalid removal failures output plan should classify unknown extension as a package part");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "invalid removal failures output should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "invalid removal failures output should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "invalid removal failures output should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "invalid removal failures output should preserve workbook relationships bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "invalid removal failures output should preserve unknown extension bytes");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "core")) {
        test_package_editor_noop_save_preserves_all_source_entries();
        test_package_editor_file_backs_copy_original_package_part_source_entries();
        test_package_editor_replaces_one_part_and_preserves_unknown_parts();
        test_package_editor_staged_chunk_part_replacement_writes_chunks();
        test_package_editor_save_as_rejects_changed_staged_chunk_size_without_state_changes();
        test_package_editor_save_as_rejects_changed_staged_chunk_crc_without_state_changes();
        test_package_editor_rejects_invalid_generic_staged_chunks_without_state_changes();
        test_package_editor_rejects_invalid_worksheet_staged_chunks_without_state_changes();
        test_package_editor_rejects_materialized_stream_rewrite_part_without_state_changes();
        test_package_editor_generic_staged_chunks_route_worksheet_targets();
        test_package_editor_replaces_worksheet_with_staged_chunks();
        test_package_editor_replaces_worksheet_by_name_with_staged_chunks();
        test_package_editor_replaces_worksheet_by_planned_name_with_staged_chunks();
        test_package_editor_replaces_worksheet_from_chunk_source();
        test_package_editor_replaces_worksheet_by_name_from_chunk_source();
        test_package_editor_repeated_part_replacement_updates_final_state();
        test_package_editor_replacement_audits_preserved_root_relationships();
        test_package_editor_sets_document_properties_and_adds_missing_metadata_parts();
        test_package_editor_document_properties_preserves_custom_properties_part();
        test_package_editor_document_properties_adds_missing_core_and_app_parts();
        test_package_editor_part_replacement_overrides_generated_document_properties();
        test_package_editor_document_properties_override_prior_part_replacement();
        test_package_editor_document_properties_override_prior_part_removal();
        test_package_editor_document_properties_failure_preserves_state();
        test_package_editor_document_properties_app_relationship_failure_preserves_state();
        test_package_editor_combines_document_properties_and_worksheet_rewrite();
        test_package_editor_replaces_worksheet_and_removes_stale_calc_chain();
        test_package_editor_source_overwrite_rejection_preserves_worksheet_rewrite_plan();
        test_package_editor_cleans_stale_calc_chain_metadata_without_payload();
        test_package_editor_worksheet_rewrite_omits_prior_calc_chain_replacement();
        test_package_editor_worksheet_rewrite_preserves_prior_workbook_replacement();
        test_package_editor_workbook_replacement_after_worksheet_rewrite_keeps_calc_policy();
        test_package_editor_removes_stale_calc_chain_relationship_part();
        test_package_editor_replaces_worksheet_and_preserves_calc_chain_when_requested();
        test_package_editor_worksheet_rewrite_preserves_prior_calc_chain_replacement_when_requested();
        test_package_editor_preserve_calc_chain_does_not_audit_missing_relationship_part();
        test_package_editor_request_full_calculation_removes_calc_chain_only();
        test_package_editor_request_full_calculation_uses_prior_workbook_replacement();
        test_package_editor_request_full_calculation_updates_direct_child_calc_pr_only();
        test_package_editor_request_full_calculation_inserts_direct_calc_pr_when_only_nested_exists();
        test_package_editor_request_full_calculation_omits_prior_calc_chain_replacement();
        test_package_editor_request_full_calculation_cleans_metadata_only_calc_chain();
        test_package_editor_request_full_calculation_preserves_calc_chain_when_requested();
        test_package_editor_request_full_calculation_preserves_prior_calc_chain_replacement();
        test_package_editor_rejects_workbook_calc_rebuild_without_state_changes();
        test_package_editor_rejects_malformed_workbook_calc_metadata_without_state_changes();
        test_package_editor_replaces_workbook_and_preserves_linked_fixture_entries();
#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
        test_package_editor_replaces_worksheet_cells_from_deflated_source_chunk_source();
        test_package_editor_replaces_workbook_from_deflated_source_and_preserves_unknown_payloads();
        test_package_editor_replaces_unknown_extension_from_deflated_source_and_preserves_owner_relationships();
        test_package_editor_request_full_calculation_from_deflated_source_preserves_linked_payloads();
        test_package_editor_replaces_worksheet_from_deflated_source_and_preserves_linked_payloads();
#endif
        }

        if (should_run_package_editor_shard(shard, "c5")) {
        test_package_editor_replaces_worksheet_cells_by_name_with_file_backed_transformer_handoff();
        test_package_editor_replaces_worksheet_cells_with_chunked_payload();
        test_package_editor_contextualizes_current_worksheet_source_read_failure_without_state_changes();
        test_package_editor_contextualizes_missing_current_worksheet_entry_without_state_changes();
        test_package_editor_rejects_malformed_current_worksheet_events_without_state_changes();
        test_package_editor_contextualizes_sheet_data_current_worksheet_source_read_failure_without_state_changes();
        test_package_editor_worksheet_cell_replacement_preserves_linked_object_parts();
        test_package_editor_worksheet_cell_replacement_skips_old_target_cell_payload_audit();
        test_package_editor_worksheet_cell_replacement_audits_replacement_payload_policy();
        test_replacement_cell_payload_scanner_streams_long_ignored_markup_chunks();
        test_sheet_data_start_tag_scanner_streams_long_ignored_markup_chunks();
        test_relationship_reference_scanner_streams_retained_tag_before_long_ignored_markup();
        test_package_entry_chunk_reader_rejects_stale_memory_chunk_size();
        test_package_entry_chunk_reader_reports_replay_cursor_after_prior_chunks();
        test_package_entry_chunk_reader_rejects_stale_empty_memory_chunk_crc();
        test_package_entry_chunk_reader_reports_unknown_chunk_kind();
        test_package_entry_chunk_reader_closes_file_on_early_exit();
        test_package_editor_worksheet_cell_replacement_refreshes_stale_dimension();
        test_package_editor_worksheet_cell_replacement_uses_planned_worksheet_input();
        test_package_editor_ordinary_worksheet_replace_part_rejects_without_state_change();
        test_package_editor_empty_ordinary_worksheet_replace_part_fails_without_state_change();
        test_package_editor_worksheet_cell_replacement_uses_sheet_data_staged_output();
        test_package_editor_streams_planned_staged_chunks_for_worksheet_cell_replacement();
        test_package_editor_rejects_changed_planned_staged_chunk_sizes_without_state_changes();
        test_package_editor_rejects_changed_planned_staged_chunk_sizes_for_sheet_data_without_state_changes();
        test_package_editor_rejects_changed_planned_staged_chunk_crc_without_state_changes();
        test_package_editor_rejects_missing_planned_staged_chunk_file_at_read_boundary();
        test_package_editor_contextualizes_by_name_planned_staged_chunk_read_failures_without_state_changes();
        test_package_editor_rejects_changed_planned_staged_chunk_crc_for_sheet_data_without_state_changes();
        test_package_editor_streams_large_source_worksheet_cell_replacement_beyond_event_window_total_size();
        test_package_editor_streams_large_planned_worksheet_cell_replacement_beyond_event_window_total_size();
        test_package_editor_worksheet_cell_replacement_missing_target_fails_before_state_change();
        test_package_editor_rejects_invalid_cell_replacement_payload_without_state_changes();
        }

        if (should_run_package_editor_shard(shard, "policy")) {
        test_package_editor_repeated_worksheet_rewrite_upserts_relationship_target_audits();
        test_package_editor_preserves_source_relationship_parts_when_replacement_omits_references();
        test_package_editor_reference_policy_fail_preserves_state();
        test_package_editor_reference_policy_fail_preserves_prior_replacement();
        test_package_editor_reference_policy_fail_preserves_prior_document_properties();
        test_package_editor_reference_policy_request_recalculation_updates_workbook_metadata();
        test_package_editor_rejects_calc_chain_rebuild_without_state_changes();
        test_package_editor_rejects_malformed_workbook_metadata_without_state_changes();
        test_package_editor_rejects_worksheet_rewrite_without_workbook_metadata();
        test_package_editor_rejects_saving_over_source_package();
        test_package_editor_save_as_copy_original_read_failure_preserves_state_and_output();
        test_package_editor_save_as_rejects_mutated_source_copy_temp_size_without_state_changes();
        test_package_editor_save_as_rejects_missing_source_copy_temp_with_expected_size();
        test_package_editor_save_as_rejects_mutated_source_copy_temp_crc_without_state_changes();
        test_package_editor_save_as_writer_failure_preserves_state_and_output();
        test_package_editor_rejects_invalid_replacements();
        test_package_editor_rejects_metadata_entry_replacements_without_state_changes();
        test_package_editor_rejects_invalid_removals_without_state_changes();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
