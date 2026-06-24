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
    return shard == "all" || shard == "core";
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
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
