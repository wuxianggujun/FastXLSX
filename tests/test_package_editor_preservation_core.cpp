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
    return shard == "all" || shard == "preservation-core";
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
void test_package_editor_replaces_drawing_and_preserves_linked_media_entries()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-linked-drawing-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-linked-drawing-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string replacement_drawing =
        R"(<xdr:wsDr xmlns:xdr="http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing" )"
        R"(xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main"/>)";
    replace_part_with_memory_chunks(editor, drawing_part, replacement_drawing,
        "linked fixture drawing local-DOM rewrite");

    const auto* drawing_plan = editor.edit_plan().find_part(drawing_part);
    check(drawing_plan != nullptr,
        "linked fixture drawing replacement should be present in the edit plan");
    check(drawing_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "linked fixture drawing replacement should be local-DOM-rewrite");
    check_manifest_write_mode(editor, drawing_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "linked fixture drawing replacement should mirror write mode into manifest");
    const auto* drawing_relationships_audit =
        editor.edit_plan().find_package_entry("xl/drawings/_rels/drawing1.xml.rels");
    check(drawing_relationships_audit != nullptr,
        "linked fixture drawing replacement should audit preserved drawing relationships");
    check(drawing_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture drawing relationships audit should be copy-original");
    check(drawing_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "linked fixture drawing relationships audit should keep source relationship role");
    check(drawing_relationships_audit->owner_part == drawing_part.value(),
        "linked fixture drawing relationships audit should keep owner part");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture chart should remain copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture image should remain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture unknown extension should remain copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "linked fixture drawing replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "linked fixture drawing replacement output plan should preserve calcChain policy");
    check(output_plan.relationship_target_audits.empty(),
        "linked fixture drawing replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "linked fixture drawing replacement output plan should not remove parts");
    check(output_plan.removed_package_entries.empty(),
        "linked fixture drawing replacement output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "linked fixture drawing replacement output plan should rewrite drawing");
    check_output_entry_part_context(output_plan.entries, "xl/drawings/drawing1.xml",
        true, drawing_part.value(),
        "linked fixture drawing replacement output plan should classify drawing");
    const auto* output_drawing_plan =
        find_output_entry_plan(output_plan.entries, "xl/drawings/drawing1.xml");
    check(output_drawing_plan->reason.find("local-DOM rewrite") != std::string::npos,
        "linked fixture drawing replacement output plan should keep replacement reason");
    check_output_entry_plan(output_plan.entries, "xl/drawings/_rels/drawing1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture drawing replacement output plan should preserve drawing relationships");
    check_output_entry_part_context(output_plan.entries,
        "xl/drawings/_rels/drawing1.xml.rels", false, "",
        "linked fixture drawing replacement output plan should classify drawing relationships as metadata");
    const auto* output_drawing_relationships_plan =
        find_output_entry_plan(output_plan.entries, "xl/drawings/_rels/drawing1.xml.rels");
    check(output_drawing_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "linked fixture drawing replacement output plan should classify drawing relationships metadata");
    check(output_drawing_relationships_plan->owner_part == drawing_part.value(),
        "linked fixture drawing replacement output plan should keep drawing relationships owner");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture drawing replacement output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "linked fixture drawing replacement output plan should classify content types as metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture drawing replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture drawing replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture drawing replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture drawing replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture drawing replacement output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/charts/chart1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture drawing replacement output plan should preserve chart");
    check_output_entry_plan(output_plan.entries, "xl/media/image1.png",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture drawing replacement output plan should preserve media");
    check_output_entry_plan(output_plan.entries, "xl/tables/table1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture drawing replacement output plan should preserve table");
    check_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawing1.vml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture drawing replacement output plan should preserve VML drawing");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing space.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture drawing replacement output plan should preserve percent-decoded drawing");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture drawing replacement output plan should preserve sharedStrings");
    check_output_entry_plan(output_plan.entries, "xl/_rels/sharedStrings.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture drawing replacement output plan should preserve sharedStrings relationships");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture drawing replacement output plan should preserve styles");
    check_output_entry_plan(output_plan.entries, "xl/vbaProject.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture drawing replacement output plan should preserve VBA");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture drawing replacement output plan should preserve calcChain");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture drawing replacement output plan should preserve unknown extension");
    check_output_entry_plan(output_plan.entries,
        "custom/_rels/opaque-extension.bin.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture drawing replacement output plan should preserve unknown extension relationships");
    check_output_entry_part_context(output_plan.entries,
        "custom/_rels/opaque-extension.bin.rels", false, "",
        "linked fixture drawing replacement output plan should classify unknown relationships as metadata");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, drawing_part.zip_path());
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == replacement_drawing,
        "linked fixture drawing replacement should write replacement bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "linked fixture drawing replacement should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "linked fixture drawing replacement should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "linked fixture drawing replacement should preserve image bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "linked fixture drawing replacement should preserve unknown extension bytes");

    const auto* drawing_relationships = output_reader.relationships_for(drawing_part);
    check(drawing_relationships != nullptr,
        "linked fixture drawing replacement should keep drawing relationships readable");
    check(drawing_relationships->find_by_id("rId1") != nullptr,
        "linked fixture drawing replacement should keep image relationship");
    check(drawing_relationships->find_by_id("rId2") != nullptr,
        "linked fixture drawing replacement should keep chart relationship");
    check(drawing_relationships->find_by_id("rId3") != nullptr,
        "linked fixture drawing replacement should keep external relationship");
    check(output_reader.content_types().override_for(drawing_part) != nullptr,
        "linked fixture drawing replacement should keep drawing content type");
    check(output_reader.content_types().default_for("png") != nullptr,
        "linked fixture drawing replacement should keep PNG default content type");
}

void test_package_editor_replaces_unknown_extension_and_preserves_owner_relationships()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-linked-opaque-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-linked-opaque-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    std::string replacement_opaque("replacement", 11);
    replacement_opaque.append(1, '\0');
    replacement_opaque += "opaque";
    replacement_opaque.append(1, '\0');
    replacement_opaque += "payload";
    replace_part_with_memory_chunks(editor, opaque_extension_part,
        replacement_opaque, "linked fixture opaque extension stream rewrite");

    const auto* opaque_plan = editor.edit_plan().find_part(opaque_extension_part);
    check(opaque_plan != nullptr,
        "linked fixture unknown extension replacement should be present in the edit plan");
    check(opaque_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "linked fixture unknown extension replacement should be stream-rewrite");
    check_manifest_write_mode(editor, opaque_extension_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "linked fixture unknown extension replacement should mirror write mode into manifest");
    const auto* opaque_relationships_audit =
        editor.edit_plan().find_package_entry("custom/_rels/opaque-extension.bin.rels");
    check(opaque_relationships_audit != nullptr,
        "linked fixture unknown extension replacement should audit preserved owner relationships");
    check(opaque_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture unknown extension relationships audit should be copy-original");
    check(opaque_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "linked fixture unknown extension relationships audit should keep source relationship role");
    check(opaque_relationships_audit->owner_part == opaque_extension_part.value(),
        "linked fixture unknown extension relationships audit should keep owner part");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture workbook should remain copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture worksheet should remain copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture drawing should remain copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture chart should remain copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture image should remain copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "linked opaque output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "linked opaque output plan should keep default calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "linked opaque output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "linked opaque output plan should not remove parts");
    check(output_plan.removed_package_entries.empty(),
        "linked opaque output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "linked opaque output plan should stream-rewrite unknown extension");
    check_output_entry_part_context(output_plan.entries, "custom/opaque-extension.bin",
        true, opaque_extension_part.value(),
        "linked opaque output plan should classify unknown extension as package part");
    const auto* output_opaque_plan =
        find_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin");
    check(output_opaque_plan->reason.find("opaque extension stream rewrite")
            != std::string::npos,
        "linked opaque output plan should keep replacement reason");
    check_output_entry_plan(output_plan.entries,
        "custom/_rels/opaque-extension.bin.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked opaque output plan should preserve owner relationships");
    check_output_entry_part_context(output_plan.entries,
        "custom/_rels/opaque-extension.bin.rels", false, "",
        "linked opaque output plan should classify owner relationships as metadata");
    const auto* output_opaque_relationships_plan = find_output_entry_plan(
        output_plan.entries, "custom/_rels/opaque-extension.bin.rels");
    check(output_opaque_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "linked opaque output plan should classify owner relationships metadata");
    check(output_opaque_relationships_plan->owner_part == opaque_extension_part.value(),
        "linked opaque output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked opaque output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false,
        "",
        "linked opaque output plan should not classify content types as package part");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked opaque output plan should preserve package relationships");
    check_output_entry_part_context(output_plan.entries, "_rels/.rels", false, "",
        "linked opaque output plan should not classify package relationships as package part");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked opaque output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked opaque output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked opaque output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked opaque output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked opaque output plan should preserve drawing");
    check_output_entry_plan(output_plan.entries, "xl/drawings/_rels/drawing1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked opaque output plan should preserve drawing relationships");
    check_output_entry_plan(output_plan.entries, "xl/charts/chart1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked opaque output plan should preserve chart");
    check_output_entry_plan(output_plan.entries, "xl/media/image1.png",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked opaque output plan should preserve media");
    check_output_entry_plan(output_plan.entries, "xl/tables/table1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked opaque output plan should preserve table");
    check_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawing1.vml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked opaque output plan should preserve VML drawing");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing space.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked opaque output plan should preserve percent-decoded drawing");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked opaque output plan should preserve sharedStrings");
    check_output_entry_plan(output_plan.entries, "xl/_rels/sharedStrings.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked opaque output plan should preserve sharedStrings relationships");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked opaque output plan should preserve styles");
    check_output_entry_plan(output_plan.entries, "xl/vbaProject.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked opaque output plan should preserve VBA");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked opaque output plan should preserve calcChain");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(
        editor.reader(), output_reader, opaque_extension_part.zip_path());
    check(output_reader.read_entry("custom/opaque-extension.bin") == replacement_opaque,
        "linked fixture unknown extension replacement should write replacement bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "linked fixture unknown extension replacement should preserve owner relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "linked fixture unknown extension replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "linked fixture unknown extension replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "linked fixture unknown extension replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "linked fixture unknown extension replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "linked fixture unknown extension replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "linked fixture unknown extension replacement should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "linked fixture unknown extension replacement should preserve media bytes");

    const auto* opaque_relationships =
        output_reader.relationships_for(opaque_extension_part);
    check(opaque_relationships != nullptr,
        "linked fixture unknown extension replacement should keep owner relationships readable");
    const auto* opaque_external = opaque_relationships->find_by_id("rIdOpaqueExternal");
    check(opaque_external != nullptr,
        "linked fixture unknown extension replacement should keep owner relationship id");
    check(opaque_external->target == "https://example.invalid/opaque-extension-audit",
        "linked fixture unknown extension replacement should keep owner relationship target");
    check(opaque_external->target_mode == fastxlsx::detail::Relationship::TargetMode::External,
        "linked fixture unknown extension replacement should keep owner relationship target mode");
    check(output_reader.content_types().default_for("bin") != nullptr,
        "linked fixture unknown extension replacement should keep BIN default content type");
}

void test_package_editor_repeated_unknown_extension_replacement_upserts_owner_audit()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-linked-opaque-repeat-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-linked-opaque-repeat-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string first_replacement("first opaque replacement");
    const std::string final_replacement("final opaque replacement");
    replace_part_with_memory_chunks(editor, opaque_extension_part,
        first_replacement, "first opaque extension stream rewrite");
    const std::size_t first_package_entry_count = editor.edit_plan().package_entries().size();
    replace_part_with_memory_chunks(editor, opaque_extension_part, final_replacement,
        "final opaque extension local-DOM rewrite");

    const auto* opaque_plan = editor.edit_plan().find_part(opaque_extension_part);
    check(opaque_plan != nullptr,
        "repeated unknown extension replacement should keep the target in the edit plan");
    check(opaque_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated unknown extension replacement should keep the latest write mode");
    check(opaque_plan->reason.find("final opaque extension local-DOM rewrite")
            != std::string::npos,
        "repeated unknown extension replacement should keep the latest reason");
    check_manifest_write_mode(editor, opaque_extension_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated unknown extension replacement should mirror latest write mode into manifest");
    check(editor.edit_plan().package_entries().size() == first_package_entry_count,
        "repeated unknown extension replacement should upsert owner relationship audit");
    const auto* opaque_relationships_audit =
        editor.edit_plan().find_package_entry("custom/_rels/opaque-extension.bin.rels");
    check(opaque_relationships_audit != nullptr,
        "repeated unknown extension replacement should keep owner relationships audit");
    check(opaque_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated unknown extension owner relationships audit should stay copy-original");
    check(opaque_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "repeated unknown extension audit should keep source relationship role");
    check(opaque_relationships_audit->owner_part == opaque_extension_part.value(),
        "repeated unknown extension audit should keep owner part");
    check(editor.edit_plan().removed_parts().empty(),
        "repeated unknown extension replacement should not record removed parts");
    check(editor.edit_plan().removed_package_entries().empty(),
        "repeated unknown extension replacement should not record removed package entries");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(
        editor.reader(), output_reader, opaque_extension_part.zip_path());
    check(output_reader.read_entry("custom/opaque-extension.bin") == final_replacement,
        "repeated unknown extension replacement should write final bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "repeated unknown extension replacement should preserve owner relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "repeated unknown extension replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "repeated unknown extension replacement should preserve worksheet bytes");

    const auto* opaque_relationships =
        output_reader.relationships_for(opaque_extension_part);
    check(opaque_relationships != nullptr,
        "repeated unknown extension replacement should keep owner relationships readable");
    check(opaque_relationships->find_by_id("rIdOpaqueExternal") != nullptr,
        "repeated unknown extension replacement should keep owner relationship id");
}

void test_package_editor_unknown_extension_replacement_restores_prior_removal()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-replace-after-remove-opaque-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-opaque-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName vba_part("/xl/vbaProject.bin");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    editor.remove_part(opaque_extension_part, "temporary opaque extension removal");
    const auto* removed_opaque = editor.edit_plan().find_removed_part(opaque_extension_part);
    check(removed_opaque != nullptr,
        "setup should record removed unknown extension before replacement restore");
    check(removed_opaque->inbound_relationships.size() == 1,
        "setup should retain unknown extension inbound relationship audit before restore");
    const auto& opaque_inbound = removed_opaque->inbound_relationships.front();
    check(opaque_inbound.owner_part == worksheet_part.value(),
        "setup should keep unknown extension inbound owner part before restore");
    check(opaque_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "setup should keep unknown extension inbound owner entry before restore");
    check(opaque_inbound.relationship_id == "rId9",
        "setup should keep unknown extension inbound relationship id before restore");
    check(opaque_inbound.relationship_type
            == "https://fastxlsx.invalid/relationships/opaque-extension",
        "setup should keep unknown extension inbound relationship type before restore");
    check(opaque_inbound.relationship_target == "../../custom/opaque-extension.bin",
        "setup should keep unknown extension inbound raw target before restore");
    check(opaque_inbound.target_part == opaque_extension_part,
        "setup should keep unknown extension normalized target before restore");
    check(editor.edit_plan().find_removed_package_entry("custom/_rels/opaque-extension.bin.rels")
            != nullptr,
        "setup should omit owner relationships before replacement restore");
    check(editor.edit_plan().find_package_entry("custom/_rels/opaque-extension.bin.rels")
            == nullptr,
        "setup should not keep active owner relationships audit before replacement restore");

    const std::string restored_opaque("restored opaque extension bytes");
    replace_part_with_memory_chunks(editor, opaque_extension_part, restored_opaque,
        "restored opaque extension after removal");

    check(editor.edit_plan().find_removed_part(opaque_extension_part) == nullptr,
        "replacement after removal should clear stale removed-part audit");
    const auto* opaque_plan = editor.edit_plan().find_part(opaque_extension_part);
    check(opaque_plan != nullptr,
        "replacement after removal should restore active edit-plan part");
    check(opaque_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "replacement after removal should keep final write mode");
    check(opaque_plan->reason.find("after removal") != std::string::npos,
        "replacement after removal should keep final replacement reason");
    check_manifest_write_mode(editor, opaque_extension_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "replacement after removal should restore manifest part write mode");
    check(editor.edit_plan().find_removed_package_entry("custom/_rels/opaque-extension.bin.rels")
            == nullptr,
        "replacement after removal should clear stale removed owner relationships audit");
    const auto* opaque_relationships_audit =
        editor.edit_plan().find_package_entry("custom/_rels/opaque-extension.bin.rels");
    check(opaque_relationships_audit != nullptr,
        "replacement after removal should restore owner relationships copy audit");
    check(opaque_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "replacement after removal should preserve owner relationships bytes");
    check(opaque_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "replacement after removal should keep source relationship audit role");
    check(opaque_relationships_audit->owner_part == opaque_extension_part.value(),
        "replacement after removal should keep owner part on restored relationship audit");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "replacement after default-typed removal should not rewrite content types");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "unknown extension replacement after removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "unknown extension replacement after removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "unknown extension replacement after removal should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "unknown extension replacement after removal should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "unknown extension replacement after removal should keep image copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "unknown extension replacement after removal should keep table copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "unknown extension replacement after removal should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "unknown extension replacement after removal should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "unknown extension replacement after removal should keep VBA project copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "unknown extension replacement after removal should keep calcChain copy-original");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(
        editor.reader(), output_reader, opaque_extension_part.zip_path());
    check(output_reader.read_entry("custom/opaque-extension.bin") == restored_opaque,
        "replacement after removal should write restored target bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "replacement after removal should restore owner relationships entry");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "replacement after removal should preserve source content types bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "unknown extension replacement after removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "unknown extension replacement after removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "unknown extension replacement after removal should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "unknown extension replacement after removal should preserve drawing bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "unknown extension replacement after removal should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "unknown extension replacement after removal should preserve media bytes");

    const auto* opaque_relationships =
        output_reader.relationships_for(opaque_extension_part);
    check(opaque_relationships != nullptr,
        "replacement after removal should make owner relationships readable again");
    const auto* opaque_external = opaque_relationships->find_by_id("rIdOpaqueExternal");
    check(opaque_external != nullptr,
        "replacement after removal should keep owner relationship id");
    check(opaque_external->target == "https://example.invalid/opaque-extension-audit",
        "replacement after removal should keep owner relationship target");
    check(opaque_external->target_mode
            == fastxlsx::detail::Relationship::TargetMode::External,
        "replacement after removal should keep owner relationship target mode");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "unknown extension replacement after removal should keep worksheet relationships readable");
    const auto* opaque_inbound_relationship = worksheet_relationships->find_by_id("rId9");
    check(opaque_inbound_relationship != nullptr,
        "unknown extension replacement after removal should keep inbound worksheet relationship id");
    check(opaque_inbound_relationship->target == "../../custom/opaque-extension.bin",
        "unknown extension replacement after removal should not rewrite inbound target");
    check(opaque_inbound_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "unknown extension replacement after removal should keep inbound target mode");
    check(output_reader.content_types().default_for("bin") != nullptr,
        "unknown extension replacement after removal should keep BIN default content type");
}

void test_package_editor_unknown_extension_removal_overrides_prior_replacement()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-after-replace-opaque-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-opaque-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName vba_part("/xl/vbaProject.bin");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    std::string stale_opaque("stale", 5);
    stale_opaque.append(1, '\0');
    stale_opaque += "opaque replacement";
    replace_part_with_memory_chunks(editor, opaque_extension_part,
        stale_opaque, "prior opaque extension replacement before removal");

    const auto* prior_opaque_plan = editor.edit_plan().find_part(opaque_extension_part);
    check(prior_opaque_plan != nullptr,
        "setup should record active unknown extension replacement before removal override");
    check(prior_opaque_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "setup unknown extension replacement should be stream-rewrite before removal override");
    const auto* prior_opaque_relationships_audit =
        editor.edit_plan().find_package_entry("custom/_rels/opaque-extension.bin.rels");
    check(prior_opaque_relationships_audit != nullptr,
        "setup unknown extension replacement should audit preserved owner relationships");
    check(prior_opaque_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "setup unknown extension owner relationships audit should be copy-original");
    check(prior_opaque_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "setup unknown extension owner relationships audit should keep source role");
    check(editor.edit_plan().find_removed_package_entry("custom/_rels/opaque-extension.bin.rels")
            == nullptr,
        "setup unknown extension replacement should not omit owner relationships");

    editor.remove_part(opaque_extension_part,
        "explicit opaque extension removal after replacement");

    check(editor.edit_plan().find_part(opaque_extension_part) == nullptr,
        "unknown extension removal after replacement should clear active replacement entry");
    const auto* removed_opaque = editor.edit_plan().find_removed_part(opaque_extension_part);
    check(removed_opaque != nullptr,
        "unknown extension removal after replacement should record removed-part audit");
    check(removed_opaque->reason.find("after replacement") != std::string::npos,
        "unknown extension removal after replacement should keep final removal reason");
    check(removed_opaque->reason.find("inbound relationship preserved")
            != std::string::npos,
        "unknown extension removal after replacement should keep inbound relationship audit");
    check(removed_opaque->inbound_relationships.size() == 1,
        "unknown extension removal after replacement should keep structured inbound audit");
    const auto& opaque_inbound = removed_opaque->inbound_relationships.front();
    check(opaque_inbound.owner_part == worksheet_part.value(),
        "unknown extension removal after replacement should keep inbound owner part");
    check(opaque_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "unknown extension removal after replacement should keep inbound owner relationship entry");
    check(opaque_inbound.relationship_id == "rId9",
        "unknown extension removal after replacement should keep inbound relationship id");
    check(opaque_inbound.relationship_type
            == "https://fastxlsx.invalid/relationships/opaque-extension",
        "unknown extension removal after replacement should keep inbound relationship type");
    check(opaque_inbound.relationship_target == "../../custom/opaque-extension.bin",
        "unknown extension removal after replacement should keep inbound raw target");
    check(opaque_inbound.target_part == opaque_extension_part,
        "unknown extension removal after replacement should keep normalized inbound target");
    check(editor.manifest().find_part(opaque_extension_part) == nullptr,
        "unknown extension removal after replacement should remove manifest part");
    check(editor.manifest().content_types().default_for("bin") != nullptr,
        "unknown extension removal after replacement should keep BIN default in manifest");
    check(editor.manifest().content_types().override_for(opaque_extension_part) == nullptr,
        "unknown extension removal after replacement should not add manifest override");
    const auto* removed_opaque_relationships =
        editor.edit_plan().find_removed_package_entry("custom/_rels/opaque-extension.bin.rels");
    check(removed_opaque_relationships != nullptr,
        "unknown extension removal after replacement should omit owner relationships");
    check(removed_opaque_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "unknown extension removal after replacement owner omission should keep source role");
    check(removed_opaque_relationships->owner_part == opaque_extension_part.value(),
        "unknown extension removal after replacement owner omission should keep owner part");
    check(editor.edit_plan().find_package_entry("custom/_rels/opaque-extension.bin.rels")
            == nullptr,
        "unknown extension removal after replacement should clear active owner relationships audit");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "unknown extension removal after replacement should not rewrite content types");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "unknown extension removal after replacement should not rewrite package relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "unknown extension removal after replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "unknown extension removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "unknown extension removal after replacement should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "unknown extension removal after replacement should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "unknown extension removal after replacement should keep image copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "unknown extension removal after replacement should keep table copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "unknown extension removal after replacement should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "unknown extension removal after replacement should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "unknown extension removal after replacement should keep VBA project copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "unknown extension removal after replacement should keep calcChain copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "unknown extension removal after replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "unknown extension removal after replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "unknown extension removal after replacement output plan should not invent dependency audits");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "unknown extension removal after replacement output plan should omit target part");
    check_output_entry_part_context(output_plan.entries, "custom/opaque-extension.bin",
        true, opaque_extension_part.value(),
        "unknown extension removal after replacement output plan should classify omitted target");
    const auto* output_opaque_plan =
        find_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin");
    check(output_opaque_plan->reason.find("after replacement") != std::string::npos,
        "unknown extension removal after replacement output plan should keep final removal reason");
    check(output_opaque_plan->inbound_relationships.size() == 1,
        "unknown extension removal after replacement output plan should expose inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "custom/opaque-extension.bin", worksheet_part.value(),
        "xl/worksheets/_rels/sheet1.xml.rels", "rId9",
        "https://fastxlsx.invalid/relationships/opaque-extension",
        "../../custom/opaque-extension.bin", opaque_extension_part,
        "unknown extension removal after replacement output plan should keep inbound context");
    check_output_entry_plan(output_plan.entries,
        "custom/_rels/opaque-extension.bin.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "unknown extension removal after replacement output plan should omit owner relationships");
    check_output_entry_part_context(output_plan.entries,
        "custom/_rels/opaque-extension.bin.rels", false, "",
        "unknown extension removal after replacement output plan should classify owner relationships as metadata");
    const auto* output_opaque_relationships_plan = find_output_entry_plan(
        output_plan.entries, "custom/_rels/opaque-extension.bin.rels");
    check(output_opaque_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "unknown extension removal after replacement output plan should classify owner relationships metadata");
    check(output_opaque_relationships_plan->owner_part == opaque_extension_part.value(),
        "unknown extension removal after replacement output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "unknown extension removal after replacement output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false,
        "",
        "unknown extension removal after replacement output plan should not classify content types as package part");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "unknown extension removal after replacement output plan should preserve package relationships");
    check_output_entry_part_context(output_plan.entries, "_rels/.rels", false, "",
        "unknown extension removal after replacement output plan should not classify package relationships as package part");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "unknown extension removal after replacement output plan should preserve inbound worksheet relationships");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("custom/opaque-extension.bin") == entries.end(),
        "unknown extension removal after replacement output should omit target part");
    check(entries.find("custom/_rels/opaque-extension.bin.rels") == entries.end(),
        "unknown extension removal after replacement output should omit owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "unknown extension removal after replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "unknown extension removal after replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "unknown extension removal after replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "unknown extension removal after replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "unknown extension removal after replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "unknown extension removal after replacement should not prune inbound worksheet relationships");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "unknown extension removal after replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "unknown extension removal after replacement should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "unknown extension removal after replacement should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "unknown extension removal after replacement should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "unknown extension removal after replacement should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "unknown extension removal after replacement should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "unknown extension removal after replacement should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "unknown extension removal after replacement should preserve VBA project bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "unknown extension removal after replacement should preserve calcChain bytes");
    check(output_reader.relationships_for(opaque_extension_part) == nullptr,
        "unknown extension removal after replacement should not keep owner relationships for absent part");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "unknown extension removal after replacement should keep worksheet relationships readable");
    const auto* opaque_link = worksheet_relationships->find_by_id("rId9");
    check(opaque_link != nullptr,
        "unknown extension removal after replacement should keep inbound unknown relationship id");
    check(opaque_link->target == "../../custom/opaque-extension.bin",
        "unknown extension removal after replacement should not rewrite inbound relationship target");
    check(opaque_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "unknown extension removal after replacement should keep inbound relationship target mode");
    check(output_reader.content_types().default_for("bin") != nullptr,
        "unknown extension removal after replacement should keep BIN default content type");
}

void test_package_editor_media_replacement_restores_prior_removal()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-replace-after-remove-media-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-media-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    editor.remove_part(image_part, "temporary media removal");
    check(editor.edit_plan().find_removed_part(image_part) != nullptr,
        "setup should record removed media before replacement restore");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "setup should not rewrite content types for default-typed media removal");
    check(editor.edit_plan().find_removed_package_entry("xl/media/_rels/image1.png.rels")
            == nullptr,
        "setup should not invent media owner relationships omission");
    check(editor.manifest().find_part(image_part) == nullptr,
        "setup should remove media from manifest before replacement restore");

    std::string restored_media("\x89PNG\r\n\x1A\n", 8);
    restored_media += "restored-media-bytes";
    replace_part_with_memory_chunks(
        editor, image_part, restored_media, "restored media after removal");

    check(editor.edit_plan().find_removed_part(image_part) == nullptr,
        "media replacement after removal should clear stale removed-part audit");
    const auto* media_plan = editor.edit_plan().find_part(image_part);
    check(media_plan != nullptr,
        "media replacement after removal should restore active edit-plan part");
    check(media_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "media replacement after removal should keep final write mode");
    check(media_plan->reason.find("after removal") != std::string::npos,
        "media replacement after removal should keep final replacement reason");
    check_manifest_write_mode(editor, image_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "media replacement after removal should restore manifest part write mode");
    check(editor.manifest().content_types().default_for("png") != nullptr,
        "media replacement after removal should keep PNG default in manifest");
    check(editor.manifest().content_types().override_for(image_part) == nullptr,
        "media replacement after removal should not add manifest image override");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "media replacement after removal should not introduce content types audit");
    check(editor.edit_plan().find_removed_package_entry("xl/media/_rels/image1.png.rels")
            == nullptr,
        "media replacement after removal should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/media/_rels/image1.png.rels") == nullptr,
        "media replacement after removal should not invent owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "media replacement after removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "media replacement after removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "media replacement after removal should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "media replacement after removal should keep chart copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "media replacement after removal should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "media replacement after removal should keep styles copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "media replacement after removal should keep unknown extension copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/media/image1.png") != entries.end(),
        "media replacement after removal output should restore media part entry");
    check(entries.find("xl/media/_rels/image1.png.rels") == entries.end(),
        "media replacement after removal output should not invent owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, image_part.zip_path());
    check(output_reader.read_entry("xl/media/image1.png") == restored_media,
        "media replacement after removal should write restored media bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "media replacement after removal should preserve source content types bytes");
    check(output_reader.content_types().default_for("png") != nullptr,
        "media replacement after removal should preserve PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "media replacement after removal should not promote PNG media to an override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "media replacement after removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "media replacement after removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "media replacement after removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "media replacement after removal should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "media replacement after removal should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "media replacement after removal should preserve inbound drawing relationships");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "media replacement after removal should preserve chart bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "media replacement after removal should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "media replacement after removal should preserve styles bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "media replacement after removal should preserve unknown extension bytes");

    const auto* drawing_relationships = output_reader.relationships_for(drawing_part);
    check(drawing_relationships != nullptr,
        "media replacement after removal should keep drawing relationships readable");
    const auto* image_relationship = drawing_relationships->find_by_id("rId1");
    check(image_relationship != nullptr,
        "media replacement after removal should keep inbound image relationship id");
    check(image_relationship->target == "../media/image1.png",
        "media replacement after removal should not rewrite inbound image target");
    check(image_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "media replacement after removal should keep inbound image target mode");
    check(output_reader.relationships_for(image_part) == nullptr,
        "media replacement after removal should not create media owner relationships");
}

void test_package_editor_media_removal_overrides_prior_replacement()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-after-replace-media-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-media-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName vba_part("/xl/vbaProject.bin");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    std::string replacement_media("\x89PNG\r\n\x1A\n", 8);
    replacement_media += "stale-media-replacement";
    replace_part_with_memory_chunks(
        editor, image_part, replacement_media, "prior media replacement before removal");

    const auto* prior_media_plan = editor.edit_plan().find_part(image_part);
    check(prior_media_plan != nullptr,
        "setup should record active media replacement before removal override");
    check(prior_media_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "setup media replacement should be stream-rewrite before removal override");
    check(editor.manifest().content_types().default_for("png") != nullptr,
        "setup media replacement should keep PNG default content type");
    check(editor.manifest().content_types().override_for(image_part) == nullptr,
        "setup media replacement should not add manifest image override");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "setup media replacement should not rewrite content types audit");
    check(editor.edit_plan().find_package_entry("xl/media/_rels/image1.png.rels") == nullptr,
        "setup media replacement should not invent owner relationships audit");

    editor.remove_part(image_part, "explicit media removal after replacement");

    check(editor.edit_plan().find_part(image_part) == nullptr,
        "media removal after replacement should clear active replacement entry");
    const auto* removed_media = editor.edit_plan().find_removed_part(image_part);
    check(removed_media != nullptr,
        "media removal after replacement should record removed-part audit");
    check(removed_media->reason.find("after replacement") != std::string::npos,
        "media removal after replacement should keep final removal reason");
    check(removed_media->reason.find("inbound relationship preserved")
            != std::string::npos,
        "media removal after replacement should keep inbound relationship audit");
    check(removed_media->inbound_relationships.size() == 1,
        "media removal after replacement should keep structured inbound audit");
    const auto& media_inbound = removed_media->inbound_relationships.front();
    check(media_inbound.owner_part == drawing_part.value(),
        "media removal after replacement should keep inbound owner part");
    check(media_inbound.owner_entry == "xl/drawings/_rels/drawing1.xml.rels",
        "media removal after replacement should keep inbound owner relationship entry");
    check(media_inbound.relationship_id == "rId1",
        "media removal after replacement should keep inbound relationship id");
    check(media_inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image",
        "media removal after replacement should keep inbound relationship type");
    check(media_inbound.relationship_target == "../media/image1.png",
        "media removal after replacement should keep inbound raw target");
    check(media_inbound.target_part == image_part,
        "media removal after replacement should keep normalized inbound target");
    check(editor.manifest().find_part(image_part) == nullptr,
        "media removal after replacement should remove manifest part");
    check(editor.manifest().content_types().default_for("png") != nullptr,
        "media removal after replacement should keep PNG default in manifest");
    check(editor.manifest().content_types().override_for(image_part) == nullptr,
        "media removal after replacement should not add manifest image override");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "media removal after replacement should not rewrite content types audit");
    check(editor.edit_plan().find_removed_package_entry("xl/media/_rels/image1.png.rels")
            == nullptr,
        "media removal after replacement should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/media/_rels/image1.png.rels") == nullptr,
        "media removal after replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "media removal after replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "media removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "media removal after replacement should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "media removal after replacement should keep chart copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "media removal after replacement should keep table copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "media removal after replacement should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "media removal after replacement should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "media removal after replacement should keep VBA project copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "media removal after replacement should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "media removal after replacement should keep unknown extension copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "media removal after replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "media removal after replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "media removal after replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.size() == 1,
        "media removal after replacement output plan should expose one removed part");
    check(output_plan.removed_parts.front().part_name == image_part,
        "media removal after replacement output plan should expose removed media part");
    check(output_plan.removed_parts.front().reason.find("after replacement")
            != std::string::npos,
        "media removal after replacement output plan should keep removed-part reason");
    check(output_plan.removed_parts.front().inbound_relationships.size() == 1,
        "media removal after replacement output plan should keep removed-part inbound audit");
    check(output_plan.removed_package_entries.empty(),
        "media removal after replacement output plan should not omit metadata entries");
    check_output_entry_plan(output_plan.entries, "xl/media/image1.png",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "media removal after replacement output plan should omit media part");
    check_output_entry_part_context(output_plan.entries, "xl/media/image1.png",
        true, image_part.value(),
        "media removal after replacement output plan should classify omitted media");
    const auto* output_media_plan =
        find_output_entry_plan(output_plan.entries, "xl/media/image1.png");
    check(output_media_plan->reason.find("after replacement") != std::string::npos,
        "media removal after replacement output plan should keep final removal reason");
    check(output_media_plan->inbound_relationships.size() == 1,
        "media removal after replacement output plan should expose inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "xl/media/image1.png", drawing_part.value(),
        "xl/drawings/_rels/drawing1.xml.rels", "rId1",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image",
        "../media/image1.png", image_part,
        "media removal after replacement output plan should keep drawing inbound audit");
    check(find_output_entry_plan(output_plan.entries, "xl/media/_rels/image1.png.rels")
            == nullptr,
        "media removal after replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "media removal after replacement output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false,
        "",
        "media removal after replacement output plan should keep content types as metadata");
    check_output_entry_plan(output_plan.entries, "xl/drawings/_rels/drawing1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "media removal after replacement output plan should preserve inbound drawing relationships");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/media/image1.png") == entries.end(),
        "media removal after replacement output should omit media part");
    check(entries.find("xl/media/_rels/image1.png.rels") == entries.end(),
        "media removal after replacement output should not invent owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "media removal after replacement should preserve content types bytes");
    check(output_reader.content_types().default_for("png") != nullptr,
        "media removal after replacement should preserve PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "media removal after replacement should not promote PNG media to an override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "media removal after replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "media removal after replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "media removal after replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "media removal after replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "media removal after replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "media removal after replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "media removal after replacement should preserve inbound drawing relationships");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "media removal after replacement should preserve chart bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "media removal after replacement should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "media removal after replacement should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "media removal after replacement should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "media removal after replacement should preserve VBA project bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "media removal after replacement should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "media removal after replacement should preserve unknown extension bytes");

    const auto* drawing_relationships = output_reader.relationships_for(drawing_part);
    check(drawing_relationships != nullptr,
        "media removal after replacement should keep drawing relationships readable");
    const auto* image_relationship = drawing_relationships->find_by_id("rId1");
    check(image_relationship != nullptr,
        "media removal after replacement should keep inbound image relationship id");
    check(image_relationship->target == "../media/image1.png",
        "media removal after replacement should not rewrite inbound image target");
    check(image_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "media removal after replacement should keep inbound image target mode");
    check(output_reader.relationships_for(image_part) == nullptr,
        "media removal after replacement should not keep owner relationships for absent media");
}

void test_package_editor_chart_replacement_restores_prior_removal()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-replace-after-remove-chart-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-chart-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName vml_drawing_part("/xl/drawings/vmlDrawing1.vml");
    const fastxlsx::detail::PartName percent_encoded_drawing_part(
        "/xl/drawings/drawing space.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName vba_part("/xl/vbaProject.bin");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    editor.remove_part(chart_part, "temporary chart removal");
    const auto* removed_chart = editor.edit_plan().find_removed_part(chart_part);
    check(removed_chart != nullptr,
        "setup should record removed chart before replacement restore");
    check(removed_chart->inbound_relationships.size() == 2,
        "setup should retain chart inbound relationship audit before restore");
    const auto* removal_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(removal_content_types != nullptr,
        "setup should record content types rewrite before chart restore");
    check(removal_content_types->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "setup content types audit should be local-DOM-rewrite after chart removal");
    check(editor.edit_plan().find_removed_package_entry("xl/charts/_rels/chart1.xml.rels")
            == nullptr,
        "setup should not invent chart owner relationships omission");
    check(editor.manifest().find_part(chart_part) == nullptr,
        "setup should remove chart from manifest before replacement restore");
    check(editor.manifest().content_types().override_for(chart_part) == nullptr,
        "setup should remove chart content type override before replacement restore");

    const std::string restored_chart =
        R"(<c:chartSpace xmlns:c="http://schemas.openxmlformats.org/drawingml/2006/chart">)"
        R"(<c:chart><c:title><c:tx><c:v>restored chart</c:v></c:tx></c:title></c:chart>)"
        R"(</c:chartSpace>)";
    replace_part_with_memory_chunks(
        editor, chart_part, restored_chart, "restored chart after removal");

    check(editor.edit_plan().find_removed_part(chart_part) == nullptr,
        "chart replacement after removal should clear stale removed-part audit");
    const auto* chart_plan = editor.edit_plan().find_part(chart_part);
    check(chart_plan != nullptr,
        "chart replacement after removal should restore active edit-plan part");
    check(chart_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "chart replacement after removal should keep final write mode");
    check(chart_plan->reason.find("after removal") != std::string::npos,
        "chart replacement after removal should keep final replacement reason");
    check_manifest_write_mode(editor, chart_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "chart replacement after removal should restore manifest part write mode");
    const auto* restored_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(restored_content_types != nullptr,
        "chart replacement after removal should keep content types audit");
    check(restored_content_types->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart replacement after removal should restore source content types audit");
    check(restored_content_types->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "chart replacement after removal should keep content types audit role");
    check(editor.edit_plan().find_removed_package_entry("xl/charts/_rels/chart1.xml.rels")
            == nullptr,
        "chart replacement after removal should not invent chart owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/charts/_rels/chart1.xml.rels") == nullptr,
        "chart replacement after removal should not invent chart owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart replacement after removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart replacement after removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart replacement after removal should keep drawing copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart replacement after removal should keep image copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart replacement after removal should keep table copy-original");
    check(editor.edit_plan().find_part(vml_drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart replacement after removal should keep VML drawing copy-original");
    check(editor.edit_plan().find_part(percent_encoded_drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart replacement after removal should keep percent-decoded drawing copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart replacement after removal should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart replacement after removal should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart replacement after removal should keep VBA project copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart replacement after removal should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart replacement after removal should keep unknown extension copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "chart replacement after removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "chart replacement after removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "chart replacement after removal output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "chart replacement after removal output plan should clear stale removed-part audits");
    check(output_plan.removed_package_entries.empty(),
        "chart replacement after removal output plan should clear stale removed package-entry audits");
    check_output_entry_plan(output_plan.entries, "xl/charts/chart1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "chart replacement after removal output plan should rewrite chart");
    check_output_entry_part_context(output_plan.entries, "xl/charts/chart1.xml",
        true, chart_part.value(),
        "chart replacement after removal output plan should classify rewritten chart");
    const auto* output_chart_plan =
        find_output_entry_plan(output_plan.entries, "xl/charts/chart1.xml");
    check(output_chart_plan->reason.find("after removal") != std::string::npos,
        "chart replacement after removal output plan should keep replacement reason");
    check(find_output_entry_plan(output_plan.entries, "xl/charts/_rels/chart1.xml.rels")
            == nullptr,
        "chart replacement after removal output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "chart replacement after removal output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "chart replacement after removal output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "chart replacement after removal output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "xl/drawings/_rels/drawing1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "chart replacement after removal output plan should preserve inbound drawing relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "chart replacement after removal output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "chart replacement after removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "chart replacement after removal output plan should preserve drawing");
    check_output_entry_plan(output_plan.entries, "xl/media/image1.png",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "chart replacement after removal output plan should preserve media");
    check_output_entry_plan(output_plan.entries, "xl/tables/table1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "chart replacement after removal output plan should preserve table");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "chart replacement after removal output plan should preserve sharedStrings");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "chart replacement after removal output plan should preserve styles");
    check_output_entry_plan(output_plan.entries, "xl/vbaProject.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "chart replacement after removal output plan should preserve VBA project");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "chart replacement after removal output plan should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/charts/chart1.xml") != entries.end(),
        "chart replacement after removal output should restore chart part entry");
    check(entries.find("xl/charts/_rels/chart1.xml.rels") == entries.end(),
        "chart replacement after removal output should not invent owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, chart_part.zip_path());
    check(output_reader.read_entry("xl/charts/chart1.xml") == restored_chart,
        "chart replacement after removal should write restored chart bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "chart replacement after removal should restore source content types bytes");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "chart replacement after removal should restore chart content type override");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "chart replacement after removal should not prune inbound drawing relationships");
    const auto* drawing_relationships = output_reader.relationships_for(drawing_part);
    check(drawing_relationships != nullptr,
        "chart replacement after removal should keep drawing relationships readable");
    const auto* chart_link = drawing_relationships->find_by_id("rId2");
    check(chart_link != nullptr,
        "chart replacement after removal should keep inbound chart relationship id");
    check(chart_link->target == "../charts/chart1.xml",
        "chart replacement after removal should not rewrite inbound chart relationship target");
    check(chart_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "chart replacement after removal should keep inbound chart target mode");
    const auto* chart_fragment_link = drawing_relationships->find_by_id("rId4");
    check(chart_fragment_link != nullptr,
        "chart replacement after removal should keep URI-qualified chart relationship id");
    check(chart_fragment_link->target == "../charts/chart1.xml#plotArea",
        "chart replacement after removal should not rewrite URI-qualified chart target");
    check(chart_fragment_link->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "chart replacement after removal should keep URI-qualified chart target mode");
    check(output_reader.relationships_for(chart_part) == nullptr,
        "chart replacement after removal should not create chart owner relationships");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "chart replacement after removal should keep table content type override");
    check(output_reader.content_types().override_for(vml_drawing_part) != nullptr,
        "chart replacement after removal should keep VML drawing content type override");
    check(output_reader.content_types().override_for(percent_encoded_drawing_part) != nullptr,
        "chart replacement after removal should keep percent-decoded drawing content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "chart replacement after removal should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "chart replacement after removal should keep styles content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "chart replacement after removal should keep VBA content type override");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "chart replacement after removal should keep calcChain content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "chart replacement after removal should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "chart replacement after removal should not promote PNG media to an override");
}

void test_package_editor_table_replacement_restores_prior_removal()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-replace-after-remove-table-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-table-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    editor.remove_part(table_part, "temporary table removal");
    check(editor.edit_plan().find_removed_part(table_part) != nullptr,
        "setup should record removed table before replacement restore");
    const auto* removal_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(removal_content_types != nullptr,
        "setup should record content types rewrite before table restore");
    check(removal_content_types->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "setup content types audit should be local-DOM-rewrite after table removal");
    check(editor.edit_plan().find_removed_package_entry("xl/tables/_rels/table1.xml.rels")
            == nullptr,
        "setup should not invent table owner relationships omission");
    check(editor.manifest().find_part(table_part) == nullptr,
        "setup should remove table from manifest before replacement restore");

    const std::string restored_table =
        R"(<table xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" )"
        R"(id="1" name="Table1" displayName="Table1" ref="A1:B3">)"
        R"(<autoFilter ref="A1:B3"/>)"
        R"(<tableColumns count="2"><tableColumn id="1" name="A"/><tableColumn id="2" name="B"/></tableColumns>)"
        R"(</table>)";
    replace_part_with_memory_chunks(editor, table_part, restored_table,
        "restored table after removal");

    check(editor.edit_plan().find_removed_part(table_part) == nullptr,
        "table replacement after removal should clear stale removed-part audit");
    const auto* table_plan = editor.edit_plan().find_part(table_part);
    check(table_plan != nullptr,
        "table replacement after removal should restore active edit-plan part");
    check(table_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "table replacement after removal should keep final write mode");
    check(table_plan->reason.find("after removal") != std::string::npos,
        "table replacement after removal should keep final replacement reason");
    check_manifest_write_mode(editor, table_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "table replacement after removal should restore manifest part write mode");
    const auto* restored_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(restored_content_types != nullptr,
        "table replacement after removal should keep content types audit");
    check(restored_content_types->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "table replacement after removal should restore source content types audit");
    check(restored_content_types->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "table replacement after removal should keep content types audit role");
    check(editor.edit_plan().find_removed_package_entry("xl/tables/_rels/table1.xml.rels")
            == nullptr,
        "table replacement after removal should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/tables/_rels/table1.xml.rels") == nullptr,
        "table replacement after removal should not invent owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "table replacement after removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "table replacement after removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "table replacement after removal should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "table replacement after removal should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "table replacement after removal should keep image copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "table replacement after removal should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "table replacement after removal should keep styles copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "table replacement after removal should keep unknown extension copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/tables/table1.xml") != entries.end(),
        "table replacement after removal output should restore table part entry");
    check(entries.find("xl/tables/_rels/table1.xml.rels") == entries.end(),
        "table replacement after removal output should not invent owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, table_part.zip_path());
    check(output_reader.read_entry("xl/tables/table1.xml") == restored_table,
        "table replacement after removal should write restored table bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "table replacement after removal should restore source content types bytes");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "table replacement after removal should restore table content type override");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "table replacement after removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "table replacement after removal should preserve inbound worksheet relationships");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "table replacement after removal should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "table replacement after removal should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "table replacement after removal should preserve media bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "table replacement after removal should preserve chart bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "table replacement after removal should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "table replacement after removal should preserve styles bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "table replacement after removal should preserve unknown extension bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "table replacement after removal should keep worksheet relationships readable");
    const auto* table_link = worksheet_relationships->find_by_id("rId3");
    check(table_link != nullptr,
        "table replacement after removal should keep inbound table relationship id");
    check(table_link->target == "../tables/table1.xml",
        "table replacement after removal should not rewrite inbound table target");
    check(table_link->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "table replacement after removal should keep inbound table target mode");
    check(output_reader.relationships_for(table_part) == nullptr,
        "table replacement after removal should not create table owner relationships");
}

void test_package_editor_table_removal_overrides_prior_replacement()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-after-replace-table-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-table-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName vba_part("/xl/vbaProject.bin");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string replacement_table =
        R"(<table xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" )"
        R"(id="1" name="Table1" displayName="Table1" ref="A1:B4">)"
        R"(<autoFilter ref="A1:B4"/>)"
        R"(<tableColumns count="2"><tableColumn id="1" name="A"/><tableColumn id="2" name="B"/></tableColumns>)"
        R"(</table>)";
    replace_part_with_memory_chunks(editor, table_part, replacement_table,
        "prior table replacement before removal");

    const auto* prior_table_plan = editor.edit_plan().find_part(table_part);
    check(prior_table_plan != nullptr,
        "setup should record active table replacement before removal override");
    check(prior_table_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "setup table replacement should be local-DOM-rewrite before removal override");
    check(editor.edit_plan().find_package_entry("xl/tables/_rels/table1.xml.rels") == nullptr,
        "setup table replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "setup table replacement should not rewrite content types audit");

    editor.remove_part(table_part, "explicit table removal after replacement");

    check(editor.edit_plan().find_part(table_part) == nullptr,
        "table removal after replacement should clear active replacement entry");
    const auto* removed_table = editor.edit_plan().find_removed_part(table_part);
    check(removed_table != nullptr,
        "table removal after replacement should record removed-part audit");
    check(removed_table->reason.find("after replacement") != std::string::npos,
        "table removal after replacement should keep final removal reason");
    check(removed_table->reason.find("inbound relationship preserved")
            != std::string::npos,
        "table removal after replacement should keep inbound relationship audit");
    check(removed_table->inbound_relationships.size() == 1,
        "table removal after replacement should keep structured inbound audit");
    const auto& table_inbound = removed_table->inbound_relationships.front();
    check(table_inbound.owner_part == worksheet_part.value(),
        "table removal after replacement should keep inbound owner part");
    check(table_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "table removal after replacement should keep inbound owner relationship entry");
    check(table_inbound.relationship_id == "rId3",
        "table removal after replacement should keep inbound relationship id");
    check(table_inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/table",
        "table removal after replacement should keep inbound relationship type");
    check(table_inbound.relationship_target == "../tables/table1.xml",
        "table removal after replacement should keep inbound raw target");
    check(table_inbound.target_part == table_part,
        "table removal after replacement should keep normalized inbound target");
    check(editor.manifest().find_part(table_part) == nullptr,
        "table removal after replacement should remove manifest part");
    check(editor.manifest().content_types().override_for(table_part) == nullptr,
        "table removal after replacement should remove manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "table removal after replacement should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "table removal after replacement content types audit should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "table removal after replacement content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry("xl/tables/_rels/table1.xml.rels")
            == nullptr,
        "table removal after replacement should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/tables/_rels/table1.xml.rels") == nullptr,
        "table removal after replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "table removal after replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "table removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "table removal after replacement should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "table removal after replacement should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "table removal after replacement should keep image copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "table removal after replacement should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "table removal after replacement should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "table removal after replacement should keep VBA project copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "table removal after replacement should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "table removal after replacement should keep unknown extension copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "table removal after replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "table removal after replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "table removal after replacement output plan should not invent dependency audits");
    check_output_entry_plan(output_plan.entries, "xl/tables/table1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "table removal after replacement output plan should omit table part");
    check_output_entry_part_context(output_plan.entries, "xl/tables/table1.xml",
        true, table_part.value(),
        "table removal after replacement output plan should classify omitted table");
    const auto* output_table_plan =
        find_output_entry_plan(output_plan.entries, "xl/tables/table1.xml");
    check(output_table_plan->reason.find("after replacement") != std::string::npos,
        "table removal after replacement output plan should keep final removal reason");
    check(output_table_plan->inbound_relationships.size() == 1,
        "table removal after replacement output plan should expose inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "xl/tables/table1.xml", worksheet_part.value(),
        "xl/worksheets/_rels/sheet1.xml.rels", "rId3",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/table",
        "../tables/table1.xml", table_part,
        "table removal after replacement output plan should keep worksheet inbound audit");
    check(find_output_entry_plan(output_plan.entries, "xl/tables/_rels/table1.xml.rels")
            == nullptr,
        "table removal after replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "table removal after replacement output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false,
        "",
        "table removal after replacement output plan should keep content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "table removal after replacement output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "table removal after replacement output plan should preserve inbound worksheet relationships");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/tables/table1.xml") == entries.end(),
        "table removal after replacement output should omit table part");
    check(entries.find("xl/tables/_rels/table1.xml.rels") == entries.end(),
        "table removal after replacement output should not invent owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(table_part) == nullptr,
        "table removal after replacement output should remove table content type override");
    check_not_contains(output_reader.read_entry("[Content_Types].xml"),
        R"(PartName="/xl/tables/table1.xml")",
        "table removal after replacement output should omit table content type override");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "table removal after replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "table removal after replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "table removal after replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "table removal after replacement should preserve inbound worksheet relationships");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "table removal after replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "table removal after replacement should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "table removal after replacement should preserve media bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "table removal after replacement should preserve chart bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "table removal after replacement should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "table removal after replacement should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "table removal after replacement should preserve VBA project bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "table removal after replacement should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "table removal after replacement should preserve unknown extension bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "table removal after replacement should keep worksheet relationships readable");
    const auto* table_link = worksheet_relationships->find_by_id("rId3");
    check(table_link != nullptr,
        "table removal after replacement should keep inbound table relationship id");
    check(table_link->target == "../tables/table1.xml",
        "table removal after replacement should not rewrite inbound table target");
    check(table_link->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "table removal after replacement should keep inbound table target mode");
    check(output_reader.relationships_for(table_part) == nullptr,
        "table removal after replacement should not keep owner relationships for absent table");
    check(output_reader.content_types().default_for("png") != nullptr,
        "table removal after replacement should preserve PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "table removal after replacement should not promote PNG media to an override");
}

void test_package_editor_vml_drawing_replacement_restores_prior_removal()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-replace-after-remove-vml-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-vml-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
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
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    editor.remove_part(vml_drawing_part, "temporary VML drawing removal");
    check(editor.edit_plan().find_removed_part(vml_drawing_part) != nullptr,
        "setup should record removed VML before replacement restore");
    const auto* removal_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(removal_content_types != nullptr,
        "setup should record content types rewrite before VML restore");
    check(removal_content_types->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "setup content types audit should be local-DOM-rewrite after VML removal");
    check(editor.edit_plan().find_removed_package_entry("xl/drawings/_rels/vmlDrawing1.vml.rels")
            == nullptr,
        "setup should not invent VML owner relationships omission");
    check(editor.manifest().find_part(vml_drawing_part) == nullptr,
        "setup should remove VML from manifest before replacement restore");

    const std::string restored_vml = R"(<xml><v:shape id="restored-shape"/></xml>)";
    replace_part_with_memory_chunks(editor, vml_drawing_part, restored_vml,
        "restored VML drawing after removal");

    check(editor.edit_plan().find_removed_part(vml_drawing_part) == nullptr,
        "VML replacement after removal should clear stale removed-part audit");
    const auto* vml_plan = editor.edit_plan().find_part(vml_drawing_part);
    check(vml_plan != nullptr,
        "VML replacement after removal should restore active edit-plan part");
    check(vml_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "VML replacement after removal should keep final write mode");
    check(vml_plan->reason.find("after removal") != std::string::npos,
        "VML replacement after removal should keep final replacement reason");
    check_manifest_write_mode(editor, vml_drawing_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "VML replacement after removal should restore manifest part write mode");
    const auto* restored_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(restored_content_types != nullptr,
        "VML replacement after removal should keep content types audit");
    check(restored_content_types->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VML replacement after removal should restore source content types audit");
    check(restored_content_types->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "VML replacement after removal should keep content types audit role");
    check(editor.edit_plan().find_removed_package_entry("xl/drawings/_rels/vmlDrawing1.vml.rels")
            == nullptr,
        "VML replacement after removal should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/drawings/_rels/vmlDrawing1.vml.rels")
            == nullptr,
        "VML replacement after removal should not invent owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VML replacement after removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VML replacement after removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VML replacement after removal should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VML replacement after removal should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VML replacement after removal should keep image copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VML replacement after removal should keep table copy-original");
    check(editor.edit_plan().find_part(percent_encoded_drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VML replacement after removal should keep percent-decoded drawing copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VML replacement after removal should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VML replacement after removal should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VML replacement after removal should keep VBA copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VML replacement after removal should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VML replacement after removal should keep unknown extension copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "VML replacement after removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "VML replacement after removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "VML replacement after removal output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "VML replacement after removal output plan should clear stale removed-part audits");
    check(output_plan.removed_package_entries.empty(),
        "VML replacement after removal output plan should clear stale removed package-entry audits");
    check_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawing1.vml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "VML replacement after removal output plan should rewrite VML drawing");
    check_output_entry_part_context(output_plan.entries, "xl/drawings/vmlDrawing1.vml",
        true, vml_drawing_part.value(),
        "VML replacement after removal output plan should classify rewritten VML drawing");
    const auto* output_vml_plan =
        find_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawing1.vml");
    check(output_vml_plan->reason.find("after removal") != std::string::npos,
        "VML replacement after removal output plan should keep replacement reason");
    check(find_output_entry_plan(output_plan.entries,
              "xl/drawings/_rels/vmlDrawing1.vml.rels") == nullptr,
        "VML replacement after removal output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VML replacement after removal output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "VML replacement after removal output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "VML replacement after removal output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VML replacement after removal output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VML replacement after removal output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VML replacement after removal output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/drawings/_rels/drawing1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VML replacement after removal output plan should preserve drawing relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VML replacement after removal output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VML replacement after removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VML replacement after removal output plan should preserve drawing");
    check_output_entry_plan(output_plan.entries, "xl/charts/chart1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VML replacement after removal output plan should preserve chart");
    check_output_entry_plan(output_plan.entries, "xl/media/image1.png",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VML replacement after removal output plan should preserve media");
    check_output_entry_plan(output_plan.entries, "xl/tables/table1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VML replacement after removal output plan should preserve table");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing space.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VML replacement after removal output plan should preserve percent-decoded drawing");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VML replacement after removal output plan should preserve sharedStrings");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VML replacement after removal output plan should preserve styles");
    check_output_entry_plan(output_plan.entries, "xl/vbaProject.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VML replacement after removal output plan should preserve VBA");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VML replacement after removal output plan should preserve calcChain");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VML replacement after removal output plan should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/drawings/vmlDrawing1.vml") != entries.end(),
        "VML replacement after removal output should restore VML drawing part entry");
    check(entries.find("xl/drawings/_rels/vmlDrawing1.vml.rels") == entries.end(),
        "VML replacement after removal output should not invent owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, vml_drawing_part.zip_path());
    check(output_reader.read_entry("xl/drawings/vmlDrawing1.vml") == restored_vml,
        "VML replacement after removal should write restored VML bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "VML replacement after removal should restore source content types bytes");
    check(output_reader.content_types().override_for(vml_drawing_part) != nullptr,
        "VML replacement after removal should restore VML content type override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "VML replacement after removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "VML replacement after removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "VML replacement after removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "VML replacement after removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "VML replacement after removal should preserve inbound worksheet relationships");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "VML replacement after removal should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "VML replacement after removal should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "VML replacement after removal should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "VML replacement after removal should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "VML replacement after removal should preserve table bytes");
    check(output_reader.read_entry("xl/drawings/drawing space.xml")
            == source.percent_encoded_drawing,
        "VML replacement after removal should preserve percent-decoded drawing bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "VML replacement after removal should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "VML replacement after removal should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "VML replacement after removal should preserve VBA bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "VML replacement after removal should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "VML replacement after removal should preserve unknown extension bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "VML replacement after removal should keep worksheet relationships readable");
    const auto* vml_link = worksheet_relationships->find_by_id("rId7");
    check(vml_link != nullptr,
        "VML replacement after removal should keep inbound VML relationship id");
    check(vml_link->target == "../drawings/vmlDrawing1.vml#shape1",
        "VML replacement after removal should not rewrite URI-qualified inbound VML target");
    check(vml_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "VML replacement after removal should keep inbound VML target mode");
    check(worksheet_relationships->find_by_id("rId1") != nullptr,
        "VML replacement after removal should keep worksheet drawing relationship");
    check(worksheet_relationships->find_by_id("rId3") != nullptr,
        "VML replacement after removal should keep worksheet table relationship");
    check(worksheet_relationships->find_by_id("rId8") != nullptr,
        "VML replacement after removal should keep percent-encoded drawing relationship");
    check(output_reader.relationships_for(vml_drawing_part) == nullptr,
        "VML replacement after removal should not create VML owner relationships");
}

void test_package_editor_percent_decoded_drawing_replacement_restores_prior_removal()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package(
            "fastxlsx-package-editor-replace-after-remove-percent-drawing-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-percent-drawing-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
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
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    editor.remove_part(percent_encoded_drawing_part,
        "temporary percent-decoded drawing removal");
    check(editor.edit_plan().find_removed_part(percent_encoded_drawing_part) != nullptr,
        "setup should record removed percent-decoded drawing before replacement restore");
    const auto* removal_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(removal_content_types != nullptr,
        "setup should record content types rewrite before percent-decoded drawing restore");
    check(removal_content_types->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "setup content types audit should be local-DOM-rewrite after percent-decoded drawing removal");
    check(editor.edit_plan().find_removed_package_entry(
              "xl/drawings/_rels/drawing space.xml.rels") == nullptr,
        "setup should not invent percent-decoded drawing owner relationships omission");
    check(editor.manifest().find_part(percent_encoded_drawing_part) == nullptr,
        "setup should remove percent-decoded drawing from manifest before replacement restore");

    const std::string restored_drawing =
        R"(<xdr:wsDr xmlns:xdr="http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing">)"
        R"(<xdr:absoluteAnchor/></xdr:wsDr>)";
    replace_part_with_memory_chunks(editor, percent_encoded_drawing_part, restored_drawing,
        "restored percent-decoded drawing after removal");

    check(editor.edit_plan().find_removed_part(percent_encoded_drawing_part) == nullptr,
        "percent-decoded drawing replacement after removal should clear stale removed-part audit");
    const auto* drawing_plan = editor.edit_plan().find_part(percent_encoded_drawing_part);
    check(drawing_plan != nullptr,
        "percent-decoded drawing replacement after removal should restore active edit-plan part");
    check(drawing_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "percent-decoded drawing replacement after removal should keep final write mode");
    check(drawing_plan->reason.find("after removal") != std::string::npos,
        "percent-decoded drawing replacement after removal should keep final replacement reason");
    check_manifest_write_mode(editor, percent_encoded_drawing_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "percent-decoded drawing replacement after removal should restore manifest part write mode");
    const auto* restored_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(restored_content_types != nullptr,
        "percent-decoded drawing replacement after removal should keep content types audit");
    check(restored_content_types->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "percent-decoded drawing replacement after removal should restore source content types audit");
    check(restored_content_types->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "percent-decoded drawing replacement after removal should keep content types audit role");
    check(editor.edit_plan().find_removed_package_entry(
              "xl/drawings/_rels/drawing space.xml.rels") == nullptr,
        "percent-decoded drawing replacement after removal should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/drawings/_rels/drawing space.xml.rels")
            == nullptr,
        "percent-decoded drawing replacement after removal should not invent owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "percent-decoded drawing replacement after removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "percent-decoded drawing replacement after removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "percent-decoded drawing replacement after removal should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "percent-decoded drawing replacement after removal should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "percent-decoded drawing replacement after removal should keep image copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "percent-decoded drawing replacement after removal should keep table copy-original");
    check(editor.edit_plan().find_part(vml_drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "percent-decoded drawing replacement after removal should keep VML drawing copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "percent-decoded drawing replacement after removal should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "percent-decoded drawing replacement after removal should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "percent-decoded drawing replacement after removal should keep VBA copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "percent-decoded drawing replacement after removal should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "percent-decoded drawing replacement after removal should keep unknown extension copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "percent-decoded drawing replacement after removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "percent-decoded drawing replacement after removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "percent-decoded drawing replacement after removal output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "percent-decoded drawing replacement after removal output plan should clear stale removed-part audits");
    check(output_plan.removed_package_entries.empty(),
        "percent-decoded drawing replacement after removal output plan should clear stale removed package-entry audits");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing space.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "percent-decoded drawing replacement after removal output plan should rewrite drawing");
    check_output_entry_part_context(output_plan.entries, "xl/drawings/drawing space.xml",
        true, percent_encoded_drawing_part.value(),
        "percent-decoded drawing replacement after removal output plan should classify rewritten drawing");
    const auto* output_drawing_plan =
        find_output_entry_plan(output_plan.entries, "xl/drawings/drawing space.xml");
    check(output_drawing_plan->reason.find("after removal") != std::string::npos,
        "percent-decoded drawing replacement after removal output plan should keep replacement reason");
    check(find_output_entry_plan(output_plan.entries,
              "xl/drawings/_rels/drawing space.xml.rels") == nullptr,
        "percent-decoded drawing replacement after removal output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "percent-decoded drawing replacement after removal output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "percent-decoded drawing replacement after removal output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "percent-decoded drawing replacement after removal output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "percent-decoded drawing replacement after removal output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "percent-decoded drawing replacement after removal output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "percent-decoded drawing replacement after removal output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/drawings/_rels/drawing1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "percent-decoded drawing replacement after removal output plan should preserve drawing relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "percent-decoded drawing replacement after removal output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "percent-decoded drawing replacement after removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "percent-decoded drawing replacement after removal output plan should preserve drawing");
    check_output_entry_plan(output_plan.entries, "xl/charts/chart1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "percent-decoded drawing replacement after removal output plan should preserve chart");
    check_output_entry_plan(output_plan.entries, "xl/media/image1.png",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "percent-decoded drawing replacement after removal output plan should preserve media");
    check_output_entry_plan(output_plan.entries, "xl/tables/table1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "percent-decoded drawing replacement after removal output plan should preserve table");
    check_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawing1.vml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "percent-decoded drawing replacement after removal output plan should preserve VML drawing");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "percent-decoded drawing replacement after removal output plan should preserve sharedStrings");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "percent-decoded drawing replacement after removal output plan should preserve styles");
    check_output_entry_plan(output_plan.entries, "xl/vbaProject.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "percent-decoded drawing replacement after removal output plan should preserve VBA");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "percent-decoded drawing replacement after removal output plan should preserve calcChain");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "percent-decoded drawing replacement after removal output plan should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/drawings/drawing space.xml") != entries.end(),
        "percent-decoded drawing replacement after removal output should restore drawing part entry");
    check(entries.find("xl/drawings/_rels/drawing space.xml.rels") == entries.end(),
        "percent-decoded drawing replacement after removal output should not invent owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(
        editor.reader(), output_reader, percent_encoded_drawing_part.zip_path());
    check(output_reader.read_entry("xl/drawings/drawing space.xml") == restored_drawing,
        "percent-decoded drawing replacement after removal should write restored drawing bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "percent-decoded drawing replacement after removal should restore source content types bytes");
    check(output_reader.content_types().override_for(percent_encoded_drawing_part) != nullptr,
        "percent-decoded drawing replacement after removal should restore drawing content type override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "percent-decoded drawing replacement after removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "percent-decoded drawing replacement after removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "percent-decoded drawing replacement after removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "percent-decoded drawing replacement after removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "percent-decoded drawing replacement after removal should preserve inbound worksheet relationships");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "percent-decoded drawing replacement after removal should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "percent-decoded drawing replacement after removal should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "percent-decoded drawing replacement after removal should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "percent-decoded drawing replacement after removal should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "percent-decoded drawing replacement after removal should preserve table bytes");
    check(output_reader.read_entry("xl/drawings/vmlDrawing1.vml") == source.vml_drawing,
        "percent-decoded drawing replacement after removal should preserve VML bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "percent-decoded drawing replacement after removal should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "percent-decoded drawing replacement after removal should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "percent-decoded drawing replacement after removal should preserve VBA bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "percent-decoded drawing replacement after removal should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "percent-decoded drawing replacement after removal should preserve unknown extension bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "percent-decoded drawing replacement after removal should keep worksheet relationships readable");
    const auto* drawing_link = worksheet_relationships->find_by_id("rId8");
    check(drawing_link != nullptr,
        "percent-decoded drawing replacement after removal should keep inbound drawing relationship id");
    check(drawing_link->target == "../drawings/drawing%20space.xml",
        "percent-decoded drawing replacement after removal should not rewrite encoded target");
    check(drawing_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "percent-decoded drawing replacement after removal should keep target mode internal");
    check(worksheet_relationships->find_by_id("rId1") != nullptr,
        "percent-decoded drawing replacement after removal should keep worksheet drawing relationship");
    check(worksheet_relationships->find_by_id("rId3") != nullptr,
        "percent-decoded drawing replacement after removal should keep worksheet table relationship");
    check(worksheet_relationships->find_by_id("rId7") != nullptr,
        "percent-decoded drawing replacement after removal should keep worksheet VML relationship");
    check(output_reader.relationships_for(percent_encoded_drawing_part) == nullptr,
        "percent-decoded drawing replacement after removal should not create owner relationships");
}

void test_package_editor_vml_drawing_removal_overrides_prior_replacement()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-after-replace-vml-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-vml-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
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
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string replacement_vml = R"(<xml><v:shape id="stale-shape"/></xml>)";
    replace_part_with_memory_chunks(editor, vml_drawing_part, replacement_vml,
        "prior VML drawing replacement before removal");
    const auto* prior_vml_plan = editor.edit_plan().find_part(vml_drawing_part);
    check(prior_vml_plan != nullptr,
        "setup should record active VML replacement before removal override");
    check(prior_vml_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "setup VML replacement should be local-DOM-rewrite before removal override");
    check(editor.edit_plan().find_package_entry("xl/drawings/_rels/vmlDrawing1.vml.rels")
            == nullptr,
        "setup VML replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "setup VML replacement should not rewrite content types audit");

    editor.remove_part(vml_drawing_part, "explicit VML drawing removal after replacement");

    check(editor.edit_plan().find_part(vml_drawing_part) == nullptr,
        "VML removal after replacement should clear active replacement entry");
    const auto* removed_vml = editor.edit_plan().find_removed_part(vml_drawing_part);
    check(removed_vml != nullptr,
        "VML removal after replacement should record removed-part audit");
    check(removed_vml->reason.find("after replacement") != std::string::npos,
        "VML removal after replacement should keep final removal reason");
    check(removed_vml->reason.find("inbound relationship preserved")
            != std::string::npos,
        "VML removal after replacement should keep inbound relationship audit");
    check(removed_vml->inbound_relationships.size() == 1,
        "VML removal after replacement should keep structured inbound audit");
    const auto& vml_inbound = removed_vml->inbound_relationships.front();
    check(vml_inbound.owner_part == worksheet_part.value(),
        "VML removal after replacement should keep inbound owner part");
    check(vml_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "VML removal after replacement should keep inbound owner relationship entry");
    check(vml_inbound.relationship_id == "rId7",
        "VML removal after replacement should keep inbound relationship id");
    check(vml_inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing",
        "VML removal after replacement should keep inbound relationship type");
    check(vml_inbound.relationship_target == "../drawings/vmlDrawing1.vml#shape1",
        "VML removal after replacement should keep URI-qualified inbound target");
    check(vml_inbound.target_part == vml_drawing_part,
        "VML removal after replacement should keep normalized inbound target");
    check(editor.manifest().find_part(vml_drawing_part) == nullptr,
        "VML removal after replacement should remove manifest part");
    check(editor.manifest().content_types().override_for(vml_drawing_part) == nullptr,
        "VML removal after replacement should remove manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "VML removal after replacement should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "VML removal after replacement content types audit should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "VML removal after replacement content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry("xl/drawings/_rels/vmlDrawing1.vml.rels")
            == nullptr,
        "VML removal after replacement should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/drawings/_rels/vmlDrawing1.vml.rels")
            == nullptr,
        "VML removal after replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VML removal after replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VML removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VML removal after replacement should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VML removal after replacement should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VML removal after replacement should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VML removal after replacement should keep table copy-original");
    check(editor.edit_plan().find_part(percent_encoded_drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VML removal after replacement should keep percent-decoded drawing copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VML removal after replacement should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VML removal after replacement should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VML removal after replacement should keep VBA copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VML removal after replacement should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VML removal after replacement should keep unknown extension copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "VML removal after replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "VML removal after replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "VML removal after replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.size() == 1,
        "VML removal after replacement output plan should expose one removed part");
    check(output_plan.removed_parts.front().part_name == vml_drawing_part,
        "VML removal after replacement output plan should expose removed VML drawing");
    check(output_plan.removed_parts.front().reason.find("after replacement")
            != std::string::npos,
        "VML removal after replacement output plan should keep removed-part reason");
    check(output_plan.removed_parts.front().inbound_relationships.size() == 1,
        "VML removal after replacement output plan should keep removed-part inbound audit");
    check(output_plan.removed_package_entries.empty(),
        "VML removal after replacement output plan should not omit metadata entries");
    check_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawing1.vml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "VML removal after replacement output plan should omit VML drawing part");
    check_output_entry_part_context(output_plan.entries, "xl/drawings/vmlDrawing1.vml",
        true, vml_drawing_part.value(),
        "VML removal after replacement output plan should classify omitted VML drawing");
    const auto* output_vml_plan =
        find_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawing1.vml");
    check(output_vml_plan->reason.find("after replacement") != std::string::npos,
        "VML removal after replacement output plan should keep final removal reason");
    check(output_vml_plan->inbound_relationships.size() == 1,
        "VML removal after replacement output plan should expose inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "xl/drawings/vmlDrawing1.vml", worksheet_part.value(),
        "xl/worksheets/_rels/sheet1.xml.rels", "rId7",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing",
        "../drawings/vmlDrawing1.vml#shape1", vml_drawing_part,
        "VML removal after replacement output plan should keep URI-qualified inbound audit");
    check(find_output_entry_plan(output_plan.entries,
              "xl/drawings/_rels/vmlDrawing1.vml.rels") == nullptr,
        "VML removal after replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "VML removal after replacement output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "VML removal after replacement output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "VML removal after replacement output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VML removal after replacement output plan should preserve inbound worksheet relationships");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/drawings/vmlDrawing1.vml") == entries.end(),
        "VML removal after replacement output should omit VML drawing part");
    check(entries.find("xl/drawings/_rels/vmlDrawing1.vml.rels") == entries.end(),
        "VML removal after replacement output should not invent owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(vml_drawing_part) == nullptr,
        "VML removal after replacement output should remove VML content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/drawings/vmlDrawing1.vml",
        "VML removal after replacement content types XML should omit VML override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "VML removal after replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "VML removal after replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "VML removal after replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "VML removal after replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "VML removal after replacement should preserve inbound worksheet relationships");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "VML removal after replacement should keep worksheet relationships readable");
    const auto* vml_link = worksheet_relationships->find_by_id("rId7");
    check(vml_link != nullptr,
        "VML removal after replacement should keep inbound VML relationship id");
    check(vml_link->target == "../drawings/vmlDrawing1.vml#shape1",
        "VML removal after replacement should not rewrite URI-qualified inbound target");
    check(vml_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "VML removal after replacement should keep inbound VML target mode");
    check(output_reader.relationships_for(vml_drawing_part) == nullptr,
        "VML removal after replacement should not create owner relationships");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "VML removal after replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "VML removal after replacement should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "VML removal after replacement should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "VML removal after replacement should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "VML removal after replacement should preserve table bytes");
    check(output_reader.read_entry("xl/drawings/drawing space.xml")
            == source.percent_encoded_drawing,
        "VML removal after replacement should preserve percent-decoded drawing bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "VML removal after replacement should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "VML removal after replacement should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "VML removal after replacement should preserve VBA bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "VML removal after replacement should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "VML removal after replacement should preserve unknown extension bytes");
    check(output_reader.content_types().override_for(percent_encoded_drawing_part) != nullptr,
        "VML removal after replacement should keep percent-decoded drawing content type override");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "VML removal after replacement should keep chart content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "VML removal after replacement should keep table content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "VML removal after replacement should keep VBA content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "VML removal after replacement should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "VML removal after replacement should not promote PNG media to an override");
}

void test_package_editor_percent_decoded_drawing_removal_overrides_prior_replacement()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package(
            "fastxlsx-package-editor-remove-after-replace-percent-drawing-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-percent-drawing-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
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
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string replacement_drawing =
        R"(<xdr:wsDr xmlns:xdr="http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing">)"
        R"(<xdr:absoluteAnchor/></xdr:wsDr>)";
    replace_part_with_memory_chunks(editor, percent_encoded_drawing_part, replacement_drawing,
        "prior percent-decoded drawing replacement before removal");
    const auto* prior_drawing_plan =
        editor.edit_plan().find_part(percent_encoded_drawing_part);
    check(prior_drawing_plan != nullptr,
        "setup should record active percent-decoded drawing replacement before removal override");
    check(prior_drawing_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "setup percent-decoded drawing replacement should be local-DOM-rewrite before removal override");
    check(editor.edit_plan().find_package_entry("xl/drawings/_rels/drawing space.xml.rels")
            == nullptr,
        "setup percent-decoded drawing replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "setup percent-decoded drawing replacement should not rewrite content types audit");

    editor.remove_part(percent_encoded_drawing_part,
        "explicit percent-decoded drawing removal after replacement");

    check(editor.edit_plan().find_part(percent_encoded_drawing_part) == nullptr,
        "percent-decoded drawing removal after replacement should clear active replacement entry");
    const auto* removed_drawing =
        editor.edit_plan().find_removed_part(percent_encoded_drawing_part);
    check(removed_drawing != nullptr,
        "percent-decoded drawing removal after replacement should record removed-part audit");
    check(removed_drawing->reason.find("after replacement") != std::string::npos,
        "percent-decoded drawing removal after replacement should keep final removal reason");
    check(removed_drawing->reason.find("inbound relationship preserved")
            != std::string::npos,
        "percent-decoded drawing removal after replacement should keep inbound relationship audit");
    check(removed_drawing->inbound_relationships.size() == 1,
        "percent-decoded drawing removal after replacement should keep structured inbound audit");
    const auto& drawing_inbound = removed_drawing->inbound_relationships.front();
    check(drawing_inbound.owner_part == worksheet_part.value(),
        "percent-decoded drawing removal after replacement should keep inbound owner part");
    check(drawing_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "percent-decoded drawing removal after replacement should keep inbound owner relationship entry");
    check(drawing_inbound.relationship_id == "rId8",
        "percent-decoded drawing removal after replacement should keep inbound relationship id");
    check(drawing_inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
        "percent-decoded drawing removal after replacement should keep inbound relationship type");
    check(drawing_inbound.relationship_target == "../drawings/drawing%20space.xml",
        "percent-decoded drawing removal after replacement should keep encoded inbound target");
    check(drawing_inbound.target_part == percent_encoded_drawing_part,
        "percent-decoded drawing removal after replacement should keep normalized inbound target");
    check(editor.manifest().find_part(percent_encoded_drawing_part) == nullptr,
        "percent-decoded drawing removal after replacement should remove manifest part");
    check(editor.manifest().content_types().override_for(percent_encoded_drawing_part)
            == nullptr,
        "percent-decoded drawing removal after replacement should remove manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "percent-decoded drawing removal after replacement should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "percent-decoded drawing removal after replacement content types audit should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "percent-decoded drawing removal after replacement content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry(
              "xl/drawings/_rels/drawing space.xml.rels") == nullptr,
        "percent-decoded drawing removal after replacement should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/drawings/_rels/drawing space.xml.rels")
            == nullptr,
        "percent-decoded drawing removal after replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "percent-decoded drawing removal after replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "percent-decoded drawing removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "percent-decoded drawing removal after replacement should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "percent-decoded drawing removal after replacement should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "percent-decoded drawing removal after replacement should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "percent-decoded drawing removal after replacement should keep table copy-original");
    check(editor.edit_plan().find_part(vml_drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "percent-decoded drawing removal after replacement should keep VML drawing copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "percent-decoded drawing removal after replacement should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "percent-decoded drawing removal after replacement should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "percent-decoded drawing removal after replacement should keep VBA copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "percent-decoded drawing removal after replacement should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "percent-decoded drawing removal after replacement should keep unknown extension copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "percent-decoded drawing removal after replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "percent-decoded drawing removal after replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "percent-decoded drawing removal after replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.size() == 1,
        "percent-decoded drawing removal after replacement output plan should expose one removed part");
    check(output_plan.removed_parts.front().part_name == percent_encoded_drawing_part,
        "percent-decoded drawing removal after replacement output plan should expose removed drawing part");
    check(output_plan.removed_parts.front().reason.find("after replacement")
            != std::string::npos,
        "percent-decoded drawing removal after replacement output plan should keep removed-part reason");
    check(output_plan.removed_parts.front().inbound_relationships.size() == 1,
        "percent-decoded drawing removal after replacement output plan should keep removed-part inbound audit");
    check(output_plan.removed_package_entries.empty(),
        "percent-decoded drawing removal after replacement output plan should not omit metadata entries");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing space.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "percent-decoded drawing removal after replacement output plan should omit drawing part");
    check_output_entry_part_context(output_plan.entries, "xl/drawings/drawing space.xml",
        true, percent_encoded_drawing_part.value(),
        "percent-decoded drawing removal after replacement output plan should classify omitted drawing");
    const auto* output_drawing_plan =
        find_output_entry_plan(output_plan.entries, "xl/drawings/drawing space.xml");
    check(output_drawing_plan->reason.find("after replacement") != std::string::npos,
        "percent-decoded drawing removal after replacement output plan should keep final removal reason");
    check(output_drawing_plan->inbound_relationships.size() == 1,
        "percent-decoded drawing removal after replacement output plan should expose inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "xl/drawings/drawing space.xml", worksheet_part.value(),
        "xl/worksheets/_rels/sheet1.xml.rels", "rId8",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
        "../drawings/drawing%20space.xml", percent_encoded_drawing_part,
        "percent-decoded drawing removal after replacement output plan should keep encoded inbound audit");
    check(find_output_entry_plan(output_plan.entries,
              "xl/drawings/_rels/drawing space.xml.rels") == nullptr,
        "percent-decoded drawing removal after replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "percent-decoded drawing removal after replacement output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "percent-decoded drawing removal after replacement output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "percent-decoded drawing removal after replacement output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "percent-decoded drawing removal after replacement output plan should preserve inbound worksheet relationships");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/drawings/drawing space.xml") == entries.end(),
        "percent-decoded drawing removal after replacement output should omit drawing part");
    check(entries.find("xl/drawings/_rels/drawing space.xml.rels") == entries.end(),
        "percent-decoded drawing removal after replacement output should not invent owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(percent_encoded_drawing_part)
            == nullptr,
        "percent-decoded drawing removal after replacement output should remove content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/drawings/drawing space.xml",
        "percent-decoded drawing removal after replacement content types XML should omit override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "percent-decoded drawing removal after replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "percent-decoded drawing removal after replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "percent-decoded drawing removal after replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "percent-decoded drawing removal after replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "percent-decoded drawing removal after replacement should preserve inbound worksheet relationships");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "percent-decoded drawing removal after replacement should keep worksheet relationships readable");
    const auto* drawing_link = worksheet_relationships->find_by_id("rId8");
    check(drawing_link != nullptr,
        "percent-decoded drawing removal after replacement should keep inbound relationship id");
    check(drawing_link->target == "../drawings/drawing%20space.xml",
        "percent-decoded drawing removal after replacement should not rewrite encoded target");
    check(drawing_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "percent-decoded drawing removal after replacement should keep target mode internal");
    check(output_reader.relationships_for(percent_encoded_drawing_part) == nullptr,
        "percent-decoded drawing removal after replacement should not create owner relationships");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "percent-decoded drawing removal after replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "percent-decoded drawing removal after replacement should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "percent-decoded drawing removal after replacement should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "percent-decoded drawing removal after replacement should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "percent-decoded drawing removal after replacement should preserve table bytes");
    check(output_reader.read_entry("xl/drawings/vmlDrawing1.vml") == source.vml_drawing,
        "percent-decoded drawing removal after replacement should preserve VML bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "percent-decoded drawing removal after replacement should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "percent-decoded drawing removal after replacement should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "percent-decoded drawing removal after replacement should preserve VBA bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "percent-decoded drawing removal after replacement should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "percent-decoded drawing removal after replacement should preserve unknown extension bytes");
    check(output_reader.content_types().override_for(vml_drawing_part) != nullptr,
        "percent-decoded drawing removal after replacement should keep VML content type override");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "percent-decoded drawing removal after replacement should keep chart content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "percent-decoded drawing removal after replacement should keep table content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "percent-decoded drawing removal after replacement should keep VBA content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "percent-decoded drawing removal after replacement should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "percent-decoded drawing removal after replacement should not promote PNG media to an override");
}

void test_package_editor_workbook_replacement_restores_prior_removal()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-replace-after-remove-workbook-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-workbook-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    editor.remove_part(workbook_part, "temporary workbook removal");
    check(editor.edit_plan().find_removed_part(workbook_part) != nullptr,
        "setup should record removed workbook before replacement restore");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/workbook.xml.rels")
            != nullptr,
        "setup should omit workbook owner relationships before replacement restore");
    const auto* removal_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(removal_content_types != nullptr,
        "setup should record content types rewrite before workbook restore");
    check(removal_content_types->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "setup content types audit should be local-DOM-rewrite after workbook removal");
    check(editor.manifest().find_part(workbook_part) == nullptr,
        "setup should remove workbook from manifest before replacement restore");

    const std::string restored_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Restored" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<definedNames><definedName name="ReportRange">Restored!$A$1:$B$2</definedName></definedNames>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, restored_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "restored workbook after removal");

    check(editor.edit_plan().find_removed_part(workbook_part) == nullptr,
        "workbook replacement after removal should clear stale removed-part audit");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "workbook replacement after removal should restore active edit-plan part");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "workbook replacement after removal should keep final write mode");
    check(workbook_plan->reason.find("after removal") != std::string::npos,
        "workbook replacement after removal should keep final replacement reason");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "workbook replacement after removal should restore manifest part write mode");
    const auto* restored_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(restored_content_types != nullptr,
        "workbook replacement after removal should keep content types audit");
    check(restored_content_types->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook replacement after removal should restore source content types audit");
    check(restored_content_types->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "workbook replacement after removal should keep content types audit role");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/workbook.xml.rels")
            == nullptr,
        "workbook replacement after removal should clear stale owner relationships omission");
    const auto* workbook_relationships_audit =
        editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels");
    check(workbook_relationships_audit != nullptr,
        "workbook replacement after removal should restore workbook relationships copy audit");
    check(workbook_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook replacement after removal should preserve workbook relationships bytes");
    check(workbook_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "workbook replacement after removal should keep source relationship audit role");
    check(workbook_relationships_audit->owner_part == workbook_part.value(),
        "workbook replacement after removal should keep owner part on relationship audit");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "workbook replacement after removal should not rewrite package relationships");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook replacement after removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook replacement after removal should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook replacement after removal should keep unknown extension copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/workbook.xml") != entries.end(),
        "workbook replacement after removal output should restore workbook part entry");
    check(entries.find("xl/_rels/workbook.xml.rels") != entries.end(),
        "workbook replacement after removal output should restore workbook relationships entry");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, workbook_part.zip_path());
    check(output_reader.read_entry("xl/workbook.xml") == restored_workbook,
        "workbook replacement after removal should write restored workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "workbook replacement after removal should restore workbook relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "workbook replacement after removal should restore source content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "workbook replacement after removal should preserve package relationships bytes");
    const auto& package_relationships = output_reader.package_relationships();
    const auto* workbook_link = package_relationships.find_by_id("rId1");
    check(workbook_link != nullptr,
        "workbook replacement after removal should keep package workbook relationship id");
    check(workbook_link->target == "xl/workbook.xml",
        "workbook replacement after removal should keep package workbook target");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "workbook replacement after removal should make workbook relationships readable again");
    check(workbook_relationships->find_by_id("rId1") != nullptr,
        "workbook replacement after removal should keep worksheet relationship id");
    check(workbook_relationships->find_by_id("rId3") != nullptr,
        "workbook replacement after removal should keep sharedStrings relationship id");
    check(workbook_relationships->find_by_id("rId5") != nullptr,
        "workbook replacement after removal should keep calcChain relationship id");
}

void test_package_editor_workbook_removal_overrides_prior_replacement()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-after-replace-workbook-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-workbook-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName vba_part("/xl/vbaProject.bin");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string stale_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Stale" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<definedNames><definedName name="ReportRange">Stale!$A$1:$B$2</definedName></definedNames>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, stale_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "stale workbook replacement before removal");
    check(editor.edit_plan().find_part(workbook_part) != nullptr,
        "setup should record active workbook replacement before removal");
    check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels")
            != nullptr,
        "setup should preserve owner relationships before final workbook removal");

    editor.remove_part(workbook_part, "final workbook removal after replacement");

    check(editor.edit_plan().find_part(workbook_part) == nullptr,
        "workbook removal after replacement should clear active replacement");
    const auto* removed_workbook = editor.edit_plan().find_removed_part(workbook_part);
    check(removed_workbook != nullptr,
        "workbook removal after replacement should record removed-part audit");
    check(removed_workbook->reason.find("after replacement") != std::string::npos,
        "workbook removal after replacement should keep final removal reason");
    check(removed_workbook->inbound_relationships.size() == 1,
        "workbook removal after replacement should keep package inbound audit");
    const auto& inbound = removed_workbook->inbound_relationships.front();
    check(inbound.owner_part.empty(),
        "workbook removal after replacement should audit package owner part as empty");
    check(inbound.owner_entry == "_rels/.rels",
        "workbook removal after replacement should audit package relationships entry");
    check(inbound.relationship_id == "rId1",
        "workbook removal after replacement should audit package relationship id");
    check(inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument",
        "workbook removal after replacement should audit package relationship type");
    check(inbound.relationship_target == "xl/workbook.xml",
        "workbook removal after replacement should audit raw workbook target");
    check(inbound.target_part == workbook_part,
        "workbook removal after replacement should audit normalized workbook target");
    check(editor.manifest().find_part(workbook_part) == nullptr,
        "workbook removal after replacement should remove manifest part");
    check(editor.manifest().content_types().override_for(workbook_part) == nullptr,
        "workbook removal after replacement should remove manifest content type override");

    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "workbook removal after replacement should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "workbook removal after replacement content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "workbook removal after replacement content types audit should keep structured role");

    const auto* removed_workbook_relationships =
        editor.edit_plan().find_removed_package_entry("xl/_rels/workbook.xml.rels");
    check(removed_workbook_relationships != nullptr,
        "workbook removal after replacement should omit source-owned workbook relationships");
    check(removed_workbook_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "workbook removal after replacement owner relationships omission should keep source role");
    check(removed_workbook_relationships->owner_part == workbook_part.value(),
        "workbook removal after replacement owner relationships omission should keep owner part");
    check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels") == nullptr,
        "workbook removal after replacement should clear active owner relationships audit");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "workbook removal after replacement should not rewrite package relationships");

    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook removal after replacement should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook removal after replacement should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook removal after replacement should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook removal after replacement should keep table copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook removal after replacement should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook removal after replacement should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook removal after replacement should keep VBA copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook removal after replacement should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook removal after replacement should keep unknown extension copy-original");

    const std::vector<fastxlsx::detail::PackageEditorOutputEntryPlan> output_plan =
        editor.planned_output_entries();
    check_output_entry_plan(output_plan, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "workbook removal after replacement output plan should omit workbook part");
    check_output_entry_part_context(output_plan, "xl/workbook.xml", true,
        "/xl/workbook.xml",
        "workbook removal after replacement output plan should classify omitted workbook");
    check_output_entry_plan(output_plan, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "workbook removal after replacement output plan should omit workbook relationships");
    check_output_entry_part_context(output_plan, "xl/_rels/workbook.xml.rels", false, "",
        "workbook removal after replacement output plan should classify owner relationships");
    const auto* output_workbook_relationships_plan =
        find_output_entry_plan(output_plan, "xl/_rels/workbook.xml.rels");
    check(output_workbook_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "workbook removal after replacement output plan should classify owner relationships omission");
    check(output_workbook_relationships_plan->owner_part == workbook_part.value(),
        "workbook removal after replacement output plan should keep owner relationship context");
    check_output_entry_plan(output_plan, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "workbook removal after replacement output plan should rewrite content types");
    check_output_entry_part_context(output_plan, "[Content_Types].xml", false, "",
        "workbook removal after replacement output plan should classify content types metadata");
    check_output_entry_plan(output_plan, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "workbook removal after replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "workbook removal after replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "workbook removal after replacement output plan should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/workbook.xml") == entries.end(),
        "workbook removal after replacement output should omit workbook part");
    check(entries.find("xl/_rels/workbook.xml.rels") == entries.end(),
        "workbook removal after replacement output should omit workbook owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(workbook_part) == nullptr,
        "workbook removal after replacement output should remove workbook content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/workbook.xml",
        "workbook removal after replacement content types XML should omit workbook override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "workbook removal after replacement should preserve package relationships bytes");
    check(output_reader.relationships_for(workbook_part) == nullptr,
        "workbook removal after replacement should not keep owner relationships for absent workbook");

    const auto& package_relationships = output_reader.package_relationships();
    const auto* workbook_link = package_relationships.find_by_id("rId1");
    check(workbook_link != nullptr,
        "workbook removal after replacement should keep inbound workbook relationship id");
    check(workbook_link->target == "xl/workbook.xml",
        "workbook removal after replacement should not rewrite inbound workbook target");
    check(workbook_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "workbook removal after replacement should keep inbound workbook target mode");

    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "workbook removal after replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "workbook removal after replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "workbook removal after replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "workbook removal after replacement should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "workbook removal after replacement should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "workbook removal after replacement should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "workbook removal after replacement should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "workbook removal after replacement should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "workbook removal after replacement should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "workbook removal after replacement should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "workbook removal after replacement should preserve VBA bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "workbook removal after replacement should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "workbook removal after replacement should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "workbook removal after replacement should preserve unknown extension relationships bytes");

    check(output_reader.content_types().override_for(worksheet_part) != nullptr,
        "workbook removal after replacement should keep worksheet content type override");
    check(output_reader.content_types().override_for(drawing_part) != nullptr,
        "workbook removal after replacement should keep drawing content type override");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "workbook removal after replacement should keep chart content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "workbook removal after replacement should keep table content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "workbook removal after replacement should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "workbook removal after replacement should keep styles content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "workbook removal after replacement should keep VBA content type override");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "workbook removal after replacement should keep calcChain content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "workbook removal after replacement should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "workbook removal after replacement should not promote PNG media default to override");
}

void test_package_editor_worksheet_replacement_restores_prior_removal()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-replace-after-remove-worksheet-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-worksheet-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    editor.remove_part(worksheet_part, "temporary worksheet removal");
    check(editor.edit_plan().find_removed_part(worksheet_part) != nullptr,
        "setup should record removed worksheet before replacement restore");
    check(editor.edit_plan().find_removed_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
            != nullptr,
        "setup should omit worksheet owner relationships before replacement restore");
    const auto* removal_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(removal_content_types != nullptr,
        "setup should record content types rewrite before worksheet restore");
    check(removal_content_types->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "setup content types audit should be local-DOM-rewrite after worksheet removal");
    check(editor.manifest().find_part(worksheet_part) == nullptr,
        "setup should remove worksheet from manifest before replacement restore");

    const std::string restored_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>42</v></c></row></sheetData>)"
        R"(<drawing r:id="rId1"/>)"
        R"(<tableParts count="1"><tablePart r:id="rId3"/></tableParts>)"
        R"(</worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, restored_worksheet,
        fastxlsx::detail::ReferencePolicy {}, "restored worksheet after removal");

    check(editor.edit_plan().find_removed_part(worksheet_part) == nullptr,
        "worksheet replacement after removal should clear stale removed-part audit");
    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "worksheet replacement after removal should restore active edit-plan part");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "worksheet replacement after removal should keep final write mode");
    check(worksheet_plan->reason.find("after removal") != std::string::npos,
        "worksheet replacement after removal should keep final replacement reason");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "worksheet replacement after removal should restore manifest part write mode");
    check(editor.edit_plan().full_calculation_on_load(),
        "worksheet replacement after removal should request workbook recalculation");
    check(editor.edit_plan().calc_chain_action()
            == fastxlsx::detail::CalcChainAction::Remove,
        "worksheet replacement after removal should use worksheet rewrite calcChain cleanup");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "worksheet replacement after removal should record stale calcChain removal");
    const auto* restored_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(restored_content_types != nullptr,
        "worksheet replacement after removal should keep content types audit");
    check(restored_content_types->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "worksheet replacement after removal should rewrite content types for calcChain cleanup");
    check(restored_content_types->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "worksheet replacement after removal should keep content types audit role");
    check(editor.edit_plan().find_removed_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == nullptr,
        "worksheet replacement after removal should clear stale owner relationships omission");
    const auto* worksheet_relationships_audit =
        editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels");
    check(worksheet_relationships_audit != nullptr,
        "worksheet replacement after removal should restore worksheet relationships copy audit");
    check(worksheet_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet replacement after removal should preserve worksheet relationships bytes");
    check(worksheet_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "worksheet replacement after removal should keep source relationship audit role");
    check(worksheet_relationships_audit->owner_part == worksheet_part.value(),
        "worksheet replacement after removal should keep owner part on relationship audit");
    const auto* workbook_relationships_entry =
        editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels");
    check(workbook_relationships_entry != nullptr,
        "worksheet replacement after removal should rewrite workbook relationships for calcChain cleanup");
    check(workbook_relationships_entry->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "worksheet replacement after removal workbook relationships should be local-DOM-rewrite");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "worksheet replacement after removal should not rewrite package relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "worksheet replacement after removal should update workbook calc metadata");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet replacement after removal should keep drawing copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet replacement after removal should keep table copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet replacement after removal should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet replacement after removal should keep styles copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet replacement after removal should keep unknown extension copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/worksheets/sheet1.xml") != entries.end(),
        "worksheet replacement after removal output should restore worksheet part entry");
    check(entries.find("xl/worksheets/_rels/sheet1.xml.rels") != entries.end(),
        "worksheet replacement after removal output should restore worksheet relationships entry");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == restored_worksheet,
        "worksheet replacement after removal should write restored worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "worksheet replacement after removal should restore worksheet relationships bytes");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "calcChain+xml",
        "worksheet replacement after removal should remove calcChain content type");
    check(output_reader.content_types().override_for(calc_chain_part) == nullptr,
        "worksheet replacement after removal should omit parsed calcChain content type");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "worksheet replacement after removal should preserve package relationships bytes");
    const std::string output_workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_not_contains(output_workbook_relationships, "relationships/calcChain",
        "worksheet replacement after removal should remove calcChain workbook relationship");
    check_contains(output_reader.read_entry("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "worksheet replacement after removal should write workbook fullCalcOnLoad");
    check(output_reader.find_entry(calc_chain_part.zip_path()) == nullptr,
        "worksheet replacement after removal should omit stale calcChain payload");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "worksheet replacement after removal should keep workbook relationships readable");
    const auto* worksheet_link = workbook_relationships->find_by_id("rId1");
    check(worksheet_link != nullptr,
        "worksheet replacement after removal should keep inbound worksheet relationship id");
    check(worksheet_link->target == "worksheets/sheet1.xml",
        "worksheet replacement after removal should not rewrite inbound worksheet target");
    check(workbook_relationships->find_by_id("rId5") == nullptr,
        "worksheet replacement after removal should remove calcChain relationship id");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "worksheet replacement after removal should make worksheet relationships readable again");
    check(worksheet_relationships->find_by_id("rId1") != nullptr,
        "worksheet replacement after removal should keep drawing relationship id");
    check(worksheet_relationships->find_by_id("rId3") != nullptr,
        "worksheet replacement after removal should keep table relationship id");
    check(worksheet_relationships->find_by_id("rId9") != nullptr,
        "worksheet replacement after removal should keep unknown extension relationship id");
    check(output_reader.content_types().override_for(worksheet_part) != nullptr,
        "worksheet replacement after removal should restore worksheet content type override");
}

void test_package_editor_worksheet_removal_overrides_prior_replacement()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-after-replace-worksheet-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-worksheet-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName vba_part("/xl/vbaProject.bin");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string stale_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>7</v></c></row></sheetData>)"
        R"(<drawing r:id="rId1"/>)"
        R"(<tableParts count="1"><tablePart r:id="rId3"/></tableParts>)"
        R"(</worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, stale_worksheet,
        fastxlsx::detail::ReferencePolicy {}, "stale worksheet replacement before removal");
    check(editor.edit_plan().find_part(worksheet_part) != nullptr,
        "setup should record active worksheet replacement before removal");
    check(editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
            != nullptr,
        "setup should preserve owner relationships before final worksheet removal");
    check(editor.edit_plan().full_calculation_on_load(),
        "setup worksheet replacement should request recalculation before final removal");
    check(editor.edit_plan().calc_chain_action()
            == fastxlsx::detail::CalcChainAction::Remove,
        "setup worksheet replacement should request calcChain cleanup before final removal");

    editor.remove_part(worksheet_part, "final worksheet removal after replacement");

    check(editor.edit_plan().find_part(worksheet_part) == nullptr,
        "worksheet removal after replacement should clear active replacement");
    const auto* removed_worksheet = editor.edit_plan().find_removed_part(worksheet_part);
    check(removed_worksheet != nullptr,
        "worksheet removal after replacement should record removed-part audit");
    check(removed_worksheet->reason.find("after replacement") != std::string::npos,
        "worksheet removal after replacement should keep final removal reason");
    check(removed_worksheet->inbound_relationships.size() == 1,
        "worksheet removal after replacement should keep workbook inbound audit");
    const auto& inbound = removed_worksheet->inbound_relationships.front();
    check(inbound.owner_part == workbook_part.value(),
        "worksheet removal after replacement should audit workbook owner part");
    check(inbound.owner_entry == "xl/_rels/workbook.xml.rels",
        "worksheet removal after replacement should audit workbook relationships entry");
    check(inbound.relationship_id == "rId1",
        "worksheet removal after replacement should audit workbook relationship id");
    check(inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet",
        "worksheet removal after replacement should audit worksheet relationship type");
    check(inbound.relationship_target == "worksheets/sheet1.xml",
        "worksheet removal after replacement should audit raw worksheet target");
    check(inbound.target_part == worksheet_part,
        "worksheet removal after replacement should audit normalized worksheet target");
    check(editor.manifest().find_part(worksheet_part) == nullptr,
        "worksheet removal after replacement should remove manifest part");
    check(editor.manifest().content_types().override_for(worksheet_part) == nullptr,
        "worksheet removal after replacement should remove manifest content type override");

    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "worksheet removal after replacement should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "worksheet removal after replacement content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "worksheet removal after replacement content types audit should keep structured role");

    const auto* removed_worksheet_relationships =
        editor.edit_plan().find_removed_package_entry("xl/worksheets/_rels/sheet1.xml.rels");
    check(removed_worksheet_relationships != nullptr,
        "worksheet removal after replacement should omit source-owned worksheet relationships");
    check(removed_worksheet_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "worksheet removal after replacement owner relationships omission should keep source role");
    check(removed_worksheet_relationships->owner_part == worksheet_part.value(),
        "worksheet removal after replacement owner relationships omission should keep owner part");
    check(editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == nullptr,
        "worksheet removal after replacement should clear active owner relationships audit");
    const auto* workbook_relationships_entry =
        editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels");
    check(workbook_relationships_entry != nullptr,
        "worksheet removal after replacement should keep prior calcChain workbook relationships rewrite");
    check(workbook_relationships_entry->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "worksheet removal after replacement workbook relationships should remain local-DOM-rewrite");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "worksheet removal after replacement should not rewrite package relationships");
    check(editor.edit_plan().full_calculation_on_load(),
        "worksheet removal after replacement should keep prior recalculation request");
    check(editor.edit_plan().calc_chain_action()
            == fastxlsx::detail::CalcChainAction::Remove,
        "worksheet removal after replacement should keep prior calcChain cleanup policy");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "worksheet removal after replacement should keep stale calcChain removal audit");

    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "worksheet removal after replacement should keep workbook calc metadata rewrite");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet removal after replacement should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet removal after replacement should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet removal after replacement should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet removal after replacement should keep table copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet removal after replacement should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet removal after replacement should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet removal after replacement should keep VBA copy-original");
    check(editor.edit_plan().find_part(calc_chain_part) == nullptr,
        "worksheet removal after replacement should keep stale calcChain as removed");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet removal after replacement should keep unknown extension copy-original");

    const std::vector<fastxlsx::detail::PackageEditorOutputEntryPlan> output_plan =
        editor.planned_output_entries();
    check_output_entry_plan(output_plan, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "worksheet removal after replacement output plan should omit worksheet part");
    check_output_entry_part_context(output_plan, "xl/worksheets/sheet1.xml",
        true, worksheet_part.value(),
        "worksheet removal after replacement output plan should classify omitted worksheet");
    check_output_entry_plan(output_plan, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "worksheet removal after replacement output plan should omit worksheet relationships");
    check_output_entry_part_context(output_plan, "xl/worksheets/_rels/sheet1.xml.rels",
        false, "",
        "worksheet removal after replacement output plan should classify owner relationships");
    const auto* output_worksheet_relationships_plan =
        find_output_entry_plan(output_plan, "xl/worksheets/_rels/sheet1.xml.rels");
    check(output_worksheet_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "worksheet removal after replacement output plan should classify owner relationships omission");
    check(output_worksheet_relationships_plan->owner_part == worksheet_part.value(),
        "worksheet removal after replacement output plan should keep owner relationship context");
    check_output_entry_plan(output_plan, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "worksheet removal after replacement output plan should rewrite content types");
    check_output_entry_plan(output_plan, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "worksheet removal after replacement output plan should keep workbook relationships rewrite");
    check_output_entry_plan(output_plan, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "worksheet removal after replacement output plan should keep workbook calc metadata rewrite");
    check_output_entry_plan(output_plan, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "worksheet removal after replacement output plan should omit stale calcChain");
    check_output_entry_plan(output_plan, "xl/drawings/drawing1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "worksheet removal after replacement output plan should preserve drawing");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/worksheets/sheet1.xml") == entries.end(),
        "worksheet removal after replacement output should omit worksheet part");
    check(entries.find("xl/worksheets/_rels/sheet1.xml.rels") == entries.end(),
        "worksheet removal after replacement output should omit worksheet owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(worksheet_part) == nullptr,
        "worksheet removal after replacement output should remove worksheet content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/worksheets/sheet1.xml",
        "worksheet removal after replacement content types XML should omit worksheet override");
    check_not_contains(output_content_types, "calcChain+xml",
        "worksheet removal after replacement should keep calcChain content type removed");
    check(output_reader.content_types().override_for(calc_chain_part) == nullptr,
        "worksheet removal after replacement should remove parsed calcChain content type");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "worksheet removal after replacement should preserve package relationships bytes");
    check_contains(output_reader.read_entry("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "worksheet removal after replacement should keep workbook recalculation metadata");
    const std::string output_workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_not_contains(output_workbook_relationships, "relationships/calcChain",
        "worksheet removal after replacement should keep calcChain relationship removed");
    check(output_reader.relationships_for(worksheet_part) == nullptr,
        "worksheet removal after replacement should not keep owner relationships for absent worksheet");

    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "worksheet removal after replacement should keep workbook relationships readable");
    const auto* worksheet_link = workbook_relationships->find_by_id("rId1");
    check(worksheet_link != nullptr,
        "worksheet removal after replacement should keep inbound worksheet relationship id");
    check(worksheet_link->target == "worksheets/sheet1.xml",
        "worksheet removal after replacement should not rewrite inbound worksheet target");
    check(worksheet_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "worksheet removal after replacement should keep inbound worksheet target mode");
    check(workbook_relationships->find_by_id("rId5") == nullptr,
        "worksheet removal after replacement should keep calcChain relationship id removed");
    check(output_reader.find_entry(calc_chain_part.zip_path()) == nullptr,
        "worksheet removal after replacement should omit stale calcChain payload");

    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "worksheet removal after replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "worksheet removal after replacement should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "worksheet removal after replacement should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "worksheet removal after replacement should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "worksheet removal after replacement should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "worksheet removal after replacement should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "worksheet removal after replacement should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "worksheet removal after replacement should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "worksheet removal after replacement should preserve VBA bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "worksheet removal after replacement should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "worksheet removal after replacement should preserve unknown extension relationships bytes");

    check(output_reader.content_types().override_for(drawing_part) != nullptr,
        "worksheet removal after replacement should keep drawing content type override");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "worksheet removal after replacement should keep chart content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "worksheet removal after replacement should keep table content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "worksheet removal after replacement should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "worksheet removal after replacement should keep styles content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "worksheet removal after replacement should keep VBA content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "worksheet removal after replacement should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "worksheet removal after replacement should not promote PNG media default to override");
}

void test_package_editor_drawing_replacement_restores_prior_removal()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-replace-after-remove-drawing-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-drawing-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    editor.remove_part(drawing_part, "temporary drawing removal");
    check(editor.edit_plan().find_removed_part(drawing_part) != nullptr,
        "setup should record removed drawing before replacement restore");
    check(editor.edit_plan().find_removed_package_entry("xl/drawings/_rels/drawing1.xml.rels")
            != nullptr,
        "setup should omit drawing owner relationships before replacement restore");
    const auto* removal_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(removal_content_types != nullptr,
        "setup should record content types rewrite before drawing restore");
    check(removal_content_types->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "setup content types audit should be local-DOM-rewrite after drawing removal");
    check(editor.manifest().find_part(drawing_part) == nullptr,
        "setup should remove drawing from manifest before replacement restore");

    const std::string restored_drawing =
        R"(<xdr:wsDr xmlns:xdr="http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing" )"
        R"(xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" )"
        R"(xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<xdr:twoCellAnchor><xdr:pic><xdr:blipFill><a:blip r:embed="rId1"/></xdr:blipFill></xdr:pic></xdr:twoCellAnchor>)"
        R"(</xdr:wsDr>)";
    replace_part_with_memory_chunks(editor, drawing_part, restored_drawing,
        "restored drawing after removal");

    check(editor.edit_plan().find_removed_part(drawing_part) == nullptr,
        "drawing replacement after removal should clear stale removed-part audit");
    const auto* drawing_plan = editor.edit_plan().find_part(drawing_part);
    check(drawing_plan != nullptr,
        "drawing replacement after removal should restore active edit-plan part");
    check(drawing_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "drawing replacement after removal should keep final write mode");
    check(drawing_plan->reason.find("after removal") != std::string::npos,
        "drawing replacement after removal should keep final replacement reason");
    check_manifest_write_mode(editor, drawing_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "drawing replacement after removal should restore manifest part write mode");
    const auto* restored_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(restored_content_types != nullptr,
        "drawing replacement after removal should keep content types audit");
    check(restored_content_types->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing replacement after removal should restore source content types audit");
    check(restored_content_types->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "drawing replacement after removal should keep content types audit role");
    check(editor.edit_plan().find_removed_package_entry("xl/drawings/_rels/drawing1.xml.rels")
            == nullptr,
        "drawing replacement after removal should clear stale owner relationships omission");
    const auto* drawing_relationships_audit =
        editor.edit_plan().find_package_entry("xl/drawings/_rels/drawing1.xml.rels");
    check(drawing_relationships_audit != nullptr,
        "drawing replacement after removal should restore drawing relationships copy audit");
    check(drawing_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing replacement after removal should preserve drawing relationships bytes");
    check(drawing_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "drawing replacement after removal should keep source relationship audit role");
    check(drawing_relationships_audit->owner_part == drawing_part.value(),
        "drawing replacement after removal should keep owner part on relationship audit");
    check(editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == nullptr,
        "drawing replacement after removal should not rewrite worksheet relationships");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "drawing replacement after removal should not rewrite package relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing replacement after removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing replacement after removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing replacement after removal should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing replacement after removal should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing replacement after removal should keep table copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing replacement after removal should keep unknown extension copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/drawings/drawing1.xml") != entries.end(),
        "drawing replacement after removal output should restore drawing part entry");
    check(entries.find("xl/drawings/_rels/drawing1.xml.rels") != entries.end(),
        "drawing replacement after removal output should restore drawing relationships entry");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, drawing_part.zip_path());
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == restored_drawing,
        "drawing replacement after removal should write restored drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "drawing replacement after removal should restore drawing relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "drawing replacement after removal should restore source content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "drawing replacement after removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "drawing replacement after removal should preserve inbound worksheet relationships bytes");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "drawing replacement after removal should keep worksheet relationships readable");
    const auto* drawing_link = worksheet_relationships->find_by_id("rId1");
    check(drawing_link != nullptr,
        "drawing replacement after removal should keep inbound drawing relationship id");
    check(drawing_link->target == "../drawings/drawing1.xml",
        "drawing replacement after removal should not rewrite inbound drawing target");
    const auto* fragment_link = worksheet_relationships->find_by_id("rId5");
    check(fragment_link != nullptr,
        "drawing replacement after removal should keep URI-qualified inbound drawing relationship id");
    check(fragment_link->target == "../drawings/drawing1.xml#shape1",
        "drawing replacement after removal should not rewrite URI-qualified inbound drawing target");
    const auto* drawing_relationships = output_reader.relationships_for(drawing_part);
    check(drawing_relationships != nullptr,
        "drawing replacement after removal should make drawing relationships readable again");
    check(drawing_relationships->find_by_id("rId1") != nullptr,
        "drawing replacement after removal should keep image relationship id");
    check(drawing_relationships->find_by_id("rId2") != nullptr,
        "drawing replacement after removal should keep chart relationship id");
    check(drawing_relationships->find_by_id("rId3") != nullptr,
        "drawing replacement after removal should keep external relationship id");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "drawing replacement after removal should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "drawing replacement after removal should preserve media bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "drawing replacement after removal should preserve unknown extension bytes");
    check(output_reader.content_types().override_for(drawing_part) != nullptr,
        "drawing replacement after removal should restore drawing content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "drawing replacement after removal should preserve PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "drawing replacement after removal should not promote PNG media to an override");
}

void test_package_editor_drawing_removal_overrides_prior_replacement()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-after-replace-drawing-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-drawing-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
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
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string stale_drawing =
        R"(<xdr:wsDr xmlns:xdr="http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing" )"
        R"(xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" )"
        R"(xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<xdr:twoCellAnchor><xdr:pic><xdr:blipFill><a:blip r:embed="rId1"/></xdr:blipFill></xdr:pic></xdr:twoCellAnchor>)"
        R"(</xdr:wsDr>)";
    replace_part_with_memory_chunks(editor, drawing_part, stale_drawing,
        "prior drawing replacement before removal");

    const auto* prior_drawing_plan = editor.edit_plan().find_part(drawing_part);
    check(prior_drawing_plan != nullptr,
        "setup should record active drawing replacement before removal override");
    check(prior_drawing_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "setup drawing replacement should be local-DOM-rewrite before removal override");
    check_manifest_write_mode(editor, drawing_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "setup drawing replacement should mirror write mode into manifest");
    const auto* prior_drawing_relationships =
        editor.edit_plan().find_package_entry("xl/drawings/_rels/drawing1.xml.rels");
    check(prior_drawing_relationships != nullptr,
        "setup should preserve drawing owner relationships before final removal");
    check(prior_drawing_relationships->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "setup drawing relationships audit should be copy-original");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "setup drawing replacement should not rewrite content types audit");
    check(editor.manifest().content_types().override_for(drawing_part) != nullptr,
        "setup drawing replacement should keep drawing content type override");

    editor.remove_part(drawing_part, "explicit drawing removal after replacement");

    check(editor.edit_plan().find_part(drawing_part) == nullptr,
        "drawing removal after replacement should clear active replacement entry");
    const auto* removed_drawing = editor.edit_plan().find_removed_part(drawing_part);
    check(removed_drawing != nullptr,
        "drawing removal after replacement should record removed-part audit");
    check(removed_drawing->reason.find("after replacement") != std::string::npos,
        "drawing removal after replacement should keep final removal reason");
    check(removed_drawing->reason.find("inbound relationship preserved")
            != std::string::npos,
        "drawing removal after replacement should keep inbound relationship audit");
    check(removed_drawing->inbound_relationships.size() == 2,
        "drawing removal after replacement should keep structured inbound audits");
    const fastxlsx::detail::RemovedPartInboundRelationshipAudit* direct_inbound = nullptr;
    const fastxlsx::detail::RemovedPartInboundRelationshipAudit* fragment_inbound = nullptr;
    for (const auto& audit : removed_drawing->inbound_relationships) {
        if (audit.relationship_id == "rId1") {
            direct_inbound = &audit;
        } else if (audit.relationship_id == "rId5") {
            fragment_inbound = &audit;
        }
    }
    check(direct_inbound != nullptr,
        "drawing removal after replacement should keep direct drawing inbound audit");
    check(direct_inbound->owner_part == worksheet_part.value(),
        "drawing removal after replacement should keep direct inbound owner part");
    check(direct_inbound->owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "drawing removal after replacement should keep direct inbound owner entry");
    check(direct_inbound->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
        "drawing removal after replacement should keep direct inbound relationship type");
    check(direct_inbound->relationship_target == "../drawings/drawing1.xml",
        "drawing removal after replacement should keep direct inbound raw target");
    check(direct_inbound->target_part == drawing_part,
        "drawing removal after replacement should keep direct normalized target");
    check(fragment_inbound != nullptr,
        "drawing removal after replacement should keep URI-qualified drawing inbound audit");
    check(fragment_inbound->owner_part == worksheet_part.value(),
        "drawing removal after replacement should keep URI-qualified inbound owner part");
    check(fragment_inbound->owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "drawing removal after replacement should keep URI-qualified inbound owner entry");
    check(fragment_inbound->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
        "drawing removal after replacement should keep URI-qualified inbound relationship type");
    check(fragment_inbound->relationship_target == "../drawings/drawing1.xml#shape1",
        "drawing removal after replacement should keep URI-qualified raw target");
    check(fragment_inbound->target_part == drawing_part,
        "drawing removal after replacement should keep URI-qualified normalized target");
    check(editor.manifest().find_part(drawing_part) == nullptr,
        "drawing removal after replacement should remove manifest part");
    check(editor.manifest().content_types().override_for(drawing_part) == nullptr,
        "drawing removal after replacement should remove manifest content type override");

    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "drawing removal after replacement should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "drawing removal after replacement content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "drawing removal after replacement content types audit should keep structured role");

    const auto* removed_drawing_relationships =
        editor.edit_plan().find_removed_package_entry("xl/drawings/_rels/drawing1.xml.rels");
    check(removed_drawing_relationships != nullptr,
        "drawing removal after replacement should omit source-owned drawing relationships");
    check(removed_drawing_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "drawing removal after replacement owner relationships omission should keep source role");
    check(removed_drawing_relationships->owner_part == drawing_part.value(),
        "drawing removal after replacement owner relationships omission should keep owner part");
    check(editor.edit_plan().find_package_entry("xl/drawings/_rels/drawing1.xml.rels")
            == nullptr,
        "drawing removal after replacement should clear active owner relationships audit");
    check(editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == nullptr,
        "drawing removal after replacement should not rewrite worksheet relationships");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "drawing removal after replacement should not rewrite package relationships");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "drawing removal after replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "drawing removal after replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "drawing removal after replacement output plan should not invent dependency audits");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "drawing removal after replacement output plan should omit drawing part");
    check_output_entry_part_context(output_plan.entries, "xl/drawings/drawing1.xml",
        true, drawing_part.value(),
        "drawing removal after replacement output plan should classify omitted drawing");
    const auto* output_drawing_plan =
        find_output_entry_plan(output_plan.entries, "xl/drawings/drawing1.xml");
    check(output_drawing_plan->reason.find("after replacement") != std::string::npos,
        "drawing removal after replacement output plan should keep final removal reason");
    check(output_drawing_plan->inbound_relationships.size() == 2,
        "drawing removal after replacement output plan should expose inbound audits");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "xl/drawings/drawing1.xml", worksheet_part.value(),
        "xl/worksheets/_rels/sheet1.xml.rels", "rId1",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
        "../drawings/drawing1.xml", drawing_part,
        "drawing removal after replacement output plan should keep direct inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "xl/drawings/drawing1.xml", worksheet_part.value(),
        "xl/worksheets/_rels/sheet1.xml.rels", "rId5",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
        "../drawings/drawing1.xml#shape1", drawing_part,
        "drawing removal after replacement output plan should keep URI-qualified inbound audit");
    check_output_entry_plan(output_plan.entries,
        "xl/drawings/_rels/drawing1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "drawing removal after replacement output plan should omit owner relationships");
    check_output_entry_part_context(output_plan.entries,
        "xl/drawings/_rels/drawing1.xml.rels", false, "",
        "drawing removal after replacement output plan should classify owner relationships as metadata");
    const auto* output_drawing_relationships_plan =
        find_output_entry_plan(output_plan.entries, "xl/drawings/_rels/drawing1.xml.rels");
    check(output_drawing_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "drawing removal after replacement output plan should classify owner relationships metadata");
    check(output_drawing_relationships_plan->owner_part == drawing_part.value(),
        "drawing removal after replacement output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "drawing removal after replacement output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "drawing removal after replacement output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "drawing removal after replacement output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "drawing removal after replacement output plan should preserve inbound worksheet relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing removal after replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing removal after replacement should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing removal after replacement should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing removal after replacement should keep table copy-original");
    check(editor.edit_plan().find_part(vml_drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing removal after replacement should keep VML drawing copy-original");
    check(editor.edit_plan().find_part(percent_encoded_drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing removal after replacement should keep percent-decoded drawing copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing removal after replacement should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing removal after replacement should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing removal after replacement should keep VBA copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing removal after replacement should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing removal after replacement should keep unknown extension copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/drawings/drawing1.xml") == entries.end(),
        "drawing removal after replacement output should omit drawing part");
    check(entries.find("xl/drawings/_rels/drawing1.xml.rels") == entries.end(),
        "drawing removal after replacement output should omit drawing owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(drawing_part) == nullptr,
        "drawing removal after replacement should remove drawing content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/drawings/drawing1.xml",
        "drawing removal after replacement content types XML should omit drawing override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "drawing removal after replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "drawing removal after replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "drawing removal after replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "drawing removal after replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "drawing removal after replacement should preserve inbound worksheet relationships");
    check(output_reader.relationships_for(drawing_part) == nullptr,
        "drawing removal after replacement should not keep owner relationships for absent drawing");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "drawing removal after replacement should keep worksheet relationships readable");
    const auto* drawing_link = worksheet_relationships->find_by_id("rId1");
    check(drawing_link != nullptr,
        "drawing removal after replacement should keep inbound drawing relationship id");
    check(drawing_link->target == "../drawings/drawing1.xml",
        "drawing removal after replacement should not rewrite inbound drawing target");
    const auto* fragment_link = worksheet_relationships->find_by_id("rId5");
    check(fragment_link != nullptr,
        "drawing removal after replacement should keep URI-qualified inbound drawing relationship id");
    check(fragment_link->target == "../drawings/drawing1.xml#shape1",
        "drawing removal after replacement should not rewrite URI-qualified inbound target");
    check(worksheet_relationships->find_by_id("rId3") != nullptr,
        "drawing removal after replacement should keep table relationship");
    check(worksheet_relationships->find_by_id("rId7") != nullptr,
        "drawing removal after replacement should keep VML relationship");
    check(worksheet_relationships->find_by_id("rId8") != nullptr,
        "drawing removal after replacement should keep percent-encoded drawing relationship");

    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "drawing removal after replacement should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "drawing removal after replacement should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "drawing removal after replacement should preserve table bytes");
    check(output_reader.read_entry("xl/drawings/vmlDrawing1.vml") == source.vml_drawing,
        "drawing removal after replacement should preserve VML drawing bytes");
    check(output_reader.read_entry("xl/drawings/drawing space.xml")
            == source.percent_encoded_drawing,
        "drawing removal after replacement should preserve percent-decoded drawing bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "drawing removal after replacement should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "drawing removal after replacement should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "drawing removal after replacement should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "drawing removal after replacement should preserve VBA bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "drawing removal after replacement should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "drawing removal after replacement should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "drawing removal after replacement should preserve unknown extension relationships bytes");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "drawing removal after replacement should keep chart content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "drawing removal after replacement should keep table content type override");
    check(output_reader.content_types().override_for(vml_drawing_part) != nullptr,
        "drawing removal after replacement should keep VML content type override");
    check(output_reader.content_types().override_for(percent_encoded_drawing_part) != nullptr,
        "drawing removal after replacement should keep percent-decoded drawing content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "drawing removal after replacement should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "drawing removal after replacement should keep styles content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "drawing removal after replacement should keep VBA content type override");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "drawing removal after replacement should keep calcChain content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "drawing removal after replacement should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "drawing removal after replacement should not promote PNG media to an override");
}

void test_package_editor_shared_strings_replacement_restores_prior_removal()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-replace-after-remove-sharedstrings-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-sharedstrings-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    editor.remove_part(shared_strings_part, "temporary sharedStrings removal");
    check(editor.edit_plan().find_removed_part(shared_strings_part) != nullptr,
        "setup should record removed sharedStrings before replacement restore");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/sharedStrings.xml.rels")
            != nullptr,
        "setup should omit sharedStrings owner relationships before replacement restore");
    const auto* removal_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(removal_content_types != nullptr,
        "setup should record content types rewrite before sharedStrings restore");
    check(removal_content_types->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "setup content types audit should be local-DOM-rewrite after sharedStrings removal");
    check(editor.manifest().find_part(shared_strings_part) == nullptr,
        "setup should remove sharedStrings from manifest before replacement restore");

    const std::string restored_shared_strings =
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="2" uniqueCount="2">)"
        R"(<si><t>Restored</t></si><si><t>Shared</t></si>)"
        R"(</sst>)";
    replace_part_with_memory_chunks(editor, shared_strings_part,
        restored_shared_strings, "restored sharedStrings after removal");

    check(editor.edit_plan().find_removed_part(shared_strings_part) == nullptr,
        "sharedStrings replacement after removal should clear stale removed-part audit");
    const auto* shared_strings_plan =
        editor.edit_plan().find_part(shared_strings_part);
    check(shared_strings_plan != nullptr,
        "sharedStrings replacement after removal should restore active edit-plan part");
    check(shared_strings_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "sharedStrings replacement after removal should keep final write mode");
    check(shared_strings_plan->reason.find("after removal") != std::string::npos,
        "sharedStrings replacement after removal should keep final replacement reason");
    check_manifest_write_mode(editor, shared_strings_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "sharedStrings replacement after removal should restore manifest part write mode");
    check(editor.manifest().content_types().override_for(shared_strings_part) != nullptr,
        "sharedStrings replacement after removal should restore manifest content type override");
    const auto* restored_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(restored_content_types != nullptr,
        "sharedStrings replacement after removal should keep content types audit");
    check(restored_content_types->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings replacement after removal should restore source content types audit");
    check(restored_content_types->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "sharedStrings replacement after removal should keep content types audit role");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/sharedStrings.xml.rels")
            == nullptr,
        "sharedStrings replacement after removal should clear stale owner relationships omission");
    const auto* shared_strings_relationships_audit =
        editor.edit_plan().find_package_entry("xl/_rels/sharedStrings.xml.rels");
    check(shared_strings_relationships_audit != nullptr,
        "sharedStrings replacement after removal should restore owner relationships copy audit");
    check(shared_strings_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings replacement after removal should preserve owner relationships bytes");
    check(shared_strings_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "sharedStrings replacement after removal should keep source relationship audit role");
    check(shared_strings_relationships_audit->owner_part == shared_strings_part.value(),
        "sharedStrings replacement after removal should keep owner part on relationship audit");
    check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels") == nullptr,
        "sharedStrings replacement after removal should not rewrite workbook relationships");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "sharedStrings replacement after removal should not rewrite package relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings replacement after removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings replacement after removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings replacement after removal should keep table copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings replacement after removal should keep styles copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings replacement after removal should keep unknown extension copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "sharedStrings replacement after removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "sharedStrings replacement after removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "sharedStrings replacement after removal output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "sharedStrings replacement after removal output plan should clear stale removed-part audits");
    check(output_plan.removed_package_entries.empty(),
        "sharedStrings replacement after removal output plan should clear stale removed package-entry audits");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "sharedStrings replacement after removal output plan should rewrite sharedStrings");
    check_output_entry_part_context(output_plan.entries, "xl/sharedStrings.xml",
        true, shared_strings_part.value(),
        "sharedStrings replacement after removal output plan should classify rewritten sharedStrings");
    const auto* output_shared_strings_plan =
        find_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml");
    check(output_shared_strings_plan->reason.find("after removal") != std::string::npos,
        "sharedStrings replacement after removal output plan should keep replacement reason");
    check_output_entry_plan(output_plan.entries, "xl/_rels/sharedStrings.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sharedStrings replacement after removal output plan should preserve owner relationships");
    check_output_entry_part_context(output_plan.entries, "xl/_rels/sharedStrings.xml.rels",
        false, "",
        "sharedStrings replacement after removal output plan should classify owner relationships as metadata");
    const auto* output_shared_strings_relationships_plan =
        find_output_entry_plan(output_plan.entries, "xl/_rels/sharedStrings.xml.rels");
    check(output_shared_strings_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "sharedStrings replacement after removal output plan should classify owner relationships metadata");
    check(output_shared_strings_relationships_plan->owner_part
            == shared_strings_part.value(),
        "sharedStrings replacement after removal output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sharedStrings replacement after removal output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "sharedStrings replacement after removal output plan should keep content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "sharedStrings replacement after removal output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sharedStrings replacement after removal output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sharedStrings replacement after removal output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sharedStrings replacement after removal output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sharedStrings replacement after removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sharedStrings replacement after removal output plan should preserve styles");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sharedStrings replacement after removal output plan should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/sharedStrings.xml") != entries.end(),
        "sharedStrings replacement after removal output should restore sharedStrings part entry");
    check(entries.find("xl/_rels/sharedStrings.xml.rels") != entries.end(),
        "sharedStrings replacement after removal output should restore owner relationships entry");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(
        editor.reader(), output_reader, shared_strings_part.zip_path());
    check(output_reader.read_entry("xl/sharedStrings.xml") == restored_shared_strings,
        "sharedStrings replacement after removal should write restored sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "sharedStrings replacement after removal should restore owner relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "sharedStrings replacement after removal should restore source content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "sharedStrings replacement after removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "sharedStrings replacement after removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "sharedStrings replacement after removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "sharedStrings replacement after removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "sharedStrings replacement after removal should preserve table bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "sharedStrings replacement after removal should preserve styles bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "sharedStrings replacement after removal should preserve unknown extension bytes");

    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "sharedStrings replacement after removal should keep workbook relationships readable");
    const auto* shared_strings_link = workbook_relationships->find_by_id("rId3");
    check(shared_strings_link != nullptr,
        "sharedStrings replacement after removal should keep inbound sharedStrings relationship id");
    check(shared_strings_link->target == "sharedStrings.xml",
        "sharedStrings replacement after removal should not rewrite inbound sharedStrings target");
    check(shared_strings_link->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "sharedStrings replacement after removal should keep inbound sharedStrings target mode");
    const auto* shared_strings_relationships =
        output_reader.relationships_for(shared_strings_part);
    check(shared_strings_relationships != nullptr,
        "sharedStrings replacement after removal should make owner relationships readable again");
    const auto* shared_external =
        shared_strings_relationships->find_by_id("rIdSharedExternal");
    check(shared_external != nullptr,
        "sharedStrings replacement after removal should keep owner relationship id");
    check(shared_external->target == "https://example.invalid/sharedStrings-audit",
        "sharedStrings replacement after removal should keep owner relationship target");
    check(shared_external->target_mode == fastxlsx::detail::Relationship::TargetMode::External,
        "sharedStrings replacement after removal should keep owner relationship target mode");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "sharedStrings replacement after removal should restore sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "sharedStrings replacement after removal should keep styles content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "sharedStrings replacement after removal should preserve PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "sharedStrings replacement after removal should not promote PNG media to an override");
}

void test_package_editor_shared_strings_removal_overrides_prior_replacement()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-after-replace-sharedstrings-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-sharedstrings-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName vba_part("/xl/vbaProject.bin");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string replacement_shared_strings =
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
        R"(<si><t>Stale replacement</t></si>)"
        R"(</sst>)";
    replace_part_with_memory_chunks(editor, shared_strings_part,
        replacement_shared_strings, "prior sharedStrings replacement before removal");

    const auto* prior_shared_strings_plan =
        editor.edit_plan().find_part(shared_strings_part);
    check(prior_shared_strings_plan != nullptr,
        "setup should record active sharedStrings replacement before removal override");
    check(prior_shared_strings_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "setup sharedStrings replacement should be stream-rewrite before removal override");
    const auto* prior_shared_strings_relationships =
        editor.edit_plan().find_package_entry("xl/_rels/sharedStrings.xml.rels");
    check(prior_shared_strings_relationships != nullptr,
        "setup sharedStrings replacement should preserve owner relationships audit");
    check(prior_shared_strings_relationships->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "setup sharedStrings owner relationships audit should be copy-original");
    check(prior_shared_strings_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "setup sharedStrings owner relationships audit should keep source relationship role");
    check(prior_shared_strings_relationships->owner_part == shared_strings_part.value(),
        "setup sharedStrings owner relationships audit should keep owner part");

    editor.remove_part(shared_strings_part,
        "explicit sharedStrings removal after replacement");

    check(editor.edit_plan().find_part(shared_strings_part) == nullptr,
        "sharedStrings removal after replacement should clear active replacement entry");
    const auto* removed_shared_strings =
        editor.edit_plan().find_removed_part(shared_strings_part);
    check(removed_shared_strings != nullptr,
        "sharedStrings removal after replacement should record removed-part audit");
    check(removed_shared_strings->reason.find("after replacement") != std::string::npos,
        "sharedStrings removal after replacement should keep final removal reason");
    check(removed_shared_strings->reason.find("inbound relationship preserved")
            != std::string::npos,
        "sharedStrings removal after replacement should keep inbound relationship audit");
    check(removed_shared_strings->inbound_relationships.size() == 1,
        "sharedStrings removal after replacement should keep structured inbound audit");
    const auto& shared_strings_inbound =
        removed_shared_strings->inbound_relationships.front();
    check(shared_strings_inbound.owner_part == workbook_part.value(),
        "sharedStrings removal after replacement should keep inbound owner part");
    check(shared_strings_inbound.owner_entry == "xl/_rels/workbook.xml.rels",
        "sharedStrings removal after replacement should keep inbound owner relationship entry");
    check(shared_strings_inbound.relationship_id == "rId3",
        "sharedStrings removal after replacement should keep inbound relationship id");
    check(shared_strings_inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings",
        "sharedStrings removal after replacement should keep inbound relationship type");
    check(shared_strings_inbound.relationship_target == "sharedStrings.xml",
        "sharedStrings removal after replacement should keep inbound raw target");
    check(shared_strings_inbound.target_part == shared_strings_part,
        "sharedStrings removal after replacement should keep normalized inbound target");
    check(editor.manifest().find_part(shared_strings_part) == nullptr,
        "sharedStrings removal after replacement should remove manifest part");
    check(editor.manifest().content_types().override_for(shared_strings_part) == nullptr,
        "sharedStrings removal after replacement should remove manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "sharedStrings removal after replacement should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "sharedStrings removal after replacement content types audit should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "sharedStrings removal after replacement content types audit should keep structured role");
    const auto* removed_shared_strings_relationships =
        editor.edit_plan().find_removed_package_entry("xl/_rels/sharedStrings.xml.rels");
    check(removed_shared_strings_relationships != nullptr,
        "sharedStrings removal after replacement should omit owner relationships");
    check(removed_shared_strings_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "sharedStrings removal after replacement owner relationships omission should keep source relationship role");
    check(removed_shared_strings_relationships->owner_part == shared_strings_part.value(),
        "sharedStrings removal after replacement owner relationships omission should keep owner part");
    check(editor.edit_plan().find_package_entry("xl/_rels/sharedStrings.xml.rels")
            == nullptr,
        "sharedStrings removal after replacement should clear active owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings removal after replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings removal after replacement should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings removal after replacement should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings removal after replacement should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings removal after replacement should keep table copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings removal after replacement should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings removal after replacement should keep VBA copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings removal after replacement should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings removal after replacement should keep unknown extension copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "sharedStrings removal after replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "sharedStrings removal after replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "sharedStrings removal after replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.size() == 1,
        "sharedStrings removal after replacement output plan should expose one removed part");
    check(output_plan.removed_parts.front().part_name == shared_strings_part,
        "sharedStrings removal after replacement output plan should expose removed sharedStrings");
    check(output_plan.removed_parts.front().reason.find("after replacement")
            != std::string::npos,
        "sharedStrings removal after replacement output plan should keep removed-part reason");
    check(output_plan.removed_parts.front().inbound_relationships.size() == 1,
        "sharedStrings removal after replacement output plan should keep removed-part inbound audit");
    check(output_plan.removed_package_entries.size() == 1,
        "sharedStrings removal after replacement output plan should expose owner relationships omission");
    check(output_plan.removed_package_entries.front().entry_name
            == "xl/_rels/sharedStrings.xml.rels",
        "sharedStrings removal after replacement output plan should expose omitted owner relationships");
    check(output_plan.removed_package_entries.front().audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "sharedStrings removal after replacement output plan should keep owner relationships omission role");
    check(output_plan.removed_package_entries.front().owner_part
            == shared_strings_part.value(),
        "sharedStrings removal after replacement output plan should keep owner relationships omission owner");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "sharedStrings removal after replacement output plan should omit sharedStrings part");
    check_output_entry_part_context(output_plan.entries, "xl/sharedStrings.xml",
        true, shared_strings_part.value(),
        "sharedStrings removal after replacement output plan should classify omitted sharedStrings");
    const auto* output_shared_strings_plan =
        find_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml");
    check(output_shared_strings_plan->reason.find("after replacement") != std::string::npos,
        "sharedStrings removal after replacement output plan should keep final removal reason");
    check(output_shared_strings_plan->inbound_relationships.size() == 1,
        "sharedStrings removal after replacement output plan should expose inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "xl/sharedStrings.xml", workbook_part.value(),
        "xl/_rels/workbook.xml.rels", "rId3",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings",
        "sharedStrings.xml", shared_strings_part,
        "sharedStrings removal after replacement output plan should keep workbook inbound audit");
    check_output_entry_plan(output_plan.entries, "xl/_rels/sharedStrings.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "sharedStrings removal after replacement output plan should omit owner relationships");
    check_output_entry_part_context(output_plan.entries, "xl/_rels/sharedStrings.xml.rels",
        false, "",
        "sharedStrings removal after replacement output plan should keep owner relationships as metadata");
    const auto* output_shared_strings_relationships_plan =
        find_output_entry_plan(output_plan.entries, "xl/_rels/sharedStrings.xml.rels");
    check(output_shared_strings_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "sharedStrings removal after replacement output plan should classify owner relationships metadata");
    check(output_shared_strings_relationships_plan->owner_part
            == shared_strings_part.value(),
        "sharedStrings removal after replacement output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "sharedStrings removal after replacement output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false,
        "",
        "sharedStrings removal after replacement output plan should keep content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "sharedStrings removal after replacement output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sharedStrings removal after replacement output plan should preserve inbound workbook relationships");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/sharedStrings.xml") == entries.end(),
        "sharedStrings removal after replacement output should omit sharedStrings part");
    check(entries.find("xl/_rels/sharedStrings.xml.rels") == entries.end(),
        "sharedStrings removal after replacement output should omit owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(shared_strings_part) == nullptr,
        "sharedStrings removal after replacement output should remove content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/sharedStrings.xml",
        "sharedStrings removal after replacement content types XML should omit sharedStrings override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "sharedStrings removal after replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "sharedStrings removal after replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "sharedStrings removal after replacement should preserve inbound workbook relationships");
    check(output_reader.relationships_for(shared_strings_part) == nullptr,
        "sharedStrings removal after replacement should not keep owner relationships for absent sharedStrings");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "sharedStrings removal after replacement should keep workbook relationships readable");
    const auto* shared_strings_link = workbook_relationships->find_by_id("rId3");
    check(shared_strings_link != nullptr,
        "sharedStrings removal after replacement should keep inbound sharedStrings relationship id");
    check(shared_strings_link->target == "sharedStrings.xml",
        "sharedStrings removal after replacement should not rewrite inbound sharedStrings target");
    check(shared_strings_link->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "sharedStrings removal after replacement should keep inbound sharedStrings target mode");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "sharedStrings removal after replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "sharedStrings removal after replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "sharedStrings removal after replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "sharedStrings removal after replacement should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "sharedStrings removal after replacement should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "sharedStrings removal after replacement should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "sharedStrings removal after replacement should preserve table bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "sharedStrings removal after replacement should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "sharedStrings removal after replacement should preserve VBA bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "sharedStrings removal after replacement should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "sharedStrings removal after replacement should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "sharedStrings removal after replacement should preserve unknown extension relationships bytes");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "sharedStrings removal after replacement should keep styles content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "sharedStrings removal after replacement should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "sharedStrings removal after replacement should not promote PNG media to an override");
}

void test_package_editor_styles_replacement_restores_prior_removal()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-replace-after-remove-styles-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-styles-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    editor.remove_part(styles_part, "temporary styles removal");
    check(editor.edit_plan().find_removed_part(styles_part) != nullptr,
        "setup should record removed styles before replacement restore");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/styles.xml.rels")
            == nullptr,
        "setup should not invent styles owner relationships omission");
    const auto* removal_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(removal_content_types != nullptr,
        "setup should record content types rewrite before styles restore");
    check(removal_content_types->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "setup content types audit should be local-DOM-rewrite after styles removal");
    check(editor.manifest().find_part(styles_part) == nullptr,
        "setup should remove styles from manifest before replacement restore");

    const std::string restored_styles =
        R"(<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<fonts count="1"><font><i/></font></fonts>)"
        R"(<fills count="2"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill></fills>)"
        R"(<borders count="1"><border/></borders>)"
        R"(<cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs>)"
        R"(<cellXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0" applyFont="1"/></cellXfs>)"
        R"(</styleSheet>)";
    replace_part_with_memory_chunks(editor, styles_part, restored_styles,
        "restored styles after removal");

    check(editor.edit_plan().find_removed_part(styles_part) == nullptr,
        "styles replacement after removal should clear stale removed-part audit");
    const auto* styles_plan = editor.edit_plan().find_part(styles_part);
    check(styles_plan != nullptr,
        "styles replacement after removal should restore active edit-plan part");
    check(styles_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "styles replacement after removal should keep final staged write mode");
    check(styles_plan->reason.find("after removal") != std::string::npos,
        "styles replacement after removal should keep final replacement reason");
    check_manifest_write_mode(editor, styles_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "styles replacement after removal should restore manifest part write mode");
    check(editor.manifest().content_types().override_for(styles_part) != nullptr,
        "styles replacement after removal should restore manifest content type override");
    const auto* restored_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(restored_content_types != nullptr,
        "styles replacement after removal should keep content types audit");
    check(restored_content_types->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles replacement after removal should restore source content types audit");
    check(restored_content_types->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "styles replacement after removal should keep content types audit role");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/styles.xml.rels")
            == nullptr,
        "styles replacement after removal should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/_rels/styles.xml.rels") == nullptr,
        "styles replacement after removal should not invent owner relationships audit");
    check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels") == nullptr,
        "styles replacement after removal should not rewrite workbook relationships");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "styles replacement after removal should not rewrite package relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles replacement after removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles replacement after removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles replacement after removal should keep table copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles replacement after removal should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles replacement after removal should keep unknown extension copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "styles replacement after removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "styles replacement after removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "styles replacement after removal output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "styles replacement after removal output plan should clear stale removed-part audits");
    check(output_plan.removed_package_entries.empty(),
        "styles replacement after removal output plan should not omit metadata entries");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "styles replacement after removal output plan should rewrite styles");
    check_output_entry_part_context(output_plan.entries, "xl/styles.xml",
        true, styles_part.value(),
        "styles replacement after removal output plan should classify rewritten styles");
    const auto* output_styles_plan =
        find_output_entry_plan(output_plan.entries, "xl/styles.xml");
    check(output_styles_plan->reason.find("after removal") != std::string::npos,
        "styles replacement after removal output plan should keep replacement reason");
    check(find_output_entry_plan(output_plan.entries, "xl/_rels/styles.xml.rels") == nullptr,
        "styles replacement after removal output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "styles replacement after removal output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "styles replacement after removal output plan should keep content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "styles replacement after removal output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "styles replacement after removal output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "styles replacement after removal output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "styles replacement after removal output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "styles replacement after removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "styles replacement after removal output plan should preserve sharedStrings");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "styles replacement after removal output plan should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/styles.xml") != entries.end(),
        "styles replacement after removal output should restore styles part entry");
    check(entries.find("xl/_rels/styles.xml.rels") == entries.end(),
        "styles replacement after removal output should not invent owner relationships entry");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, styles_part.zip_path());
    check(output_reader.read_entry("xl/styles.xml") == restored_styles,
        "styles replacement after removal should write restored styles bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "styles replacement after removal should restore source content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "styles replacement after removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "styles replacement after removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "styles replacement after removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "styles replacement after removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "styles replacement after removal should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "styles replacement after removal should preserve sharedStrings bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "styles replacement after removal should preserve unknown extension bytes");

    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "styles replacement after removal should keep workbook relationships readable");
    const auto* styles_link = workbook_relationships->find_by_id("rId4");
    check(styles_link != nullptr,
        "styles replacement after removal should keep inbound styles relationship id");
    check(styles_link->target == "styles.xml",
        "styles replacement after removal should not rewrite inbound styles target");
    check(styles_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "styles replacement after removal should keep inbound styles target mode");
    check(output_reader.relationships_for(styles_part) == nullptr,
        "styles replacement after removal should not create owner relationships");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "styles replacement after removal should restore styles content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "styles replacement after removal should keep sharedStrings content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "styles replacement after removal should preserve PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "styles replacement after removal should not promote PNG media to an override");
}

void test_package_editor_styles_removal_overrides_prior_replacement()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-after-replace-styles-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-styles-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName vba_part("/xl/vbaProject.bin");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string replacement_styles =
        R"(<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<fonts count="1"><font><b/></font></fonts>)"
        R"(<fills count="2"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill></fills>)"
        R"(<borders count="1"><border/></borders>)"
        R"(<cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs>)"
        R"(<cellXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0" applyFont="1"/></cellXfs>)"
        R"(</styleSheet>)";
    replace_part_with_memory_chunks(editor, styles_part, replacement_styles,
        "prior styles replacement before removal");

    const auto* prior_styles_plan = editor.edit_plan().find_part(styles_part);
    check(prior_styles_plan != nullptr,
        "setup should record active styles replacement before removal override");
    check(prior_styles_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "setup styles replacement should be staged before removal override");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "setup styles replacement should not rewrite content types before removal override");
    check(editor.edit_plan().find_package_entry("xl/_rels/styles.xml.rels") == nullptr,
        "setup styles replacement should not invent owner relationships audit");

    editor.remove_part(styles_part, "explicit styles removal after replacement");

    check(editor.edit_plan().find_part(styles_part) == nullptr,
        "styles removal after replacement should clear active replacement entry");
    const auto* removed_styles = editor.edit_plan().find_removed_part(styles_part);
    check(removed_styles != nullptr,
        "styles removal after replacement should record removed-part audit");
    check(removed_styles->reason.find("after replacement") != std::string::npos,
        "styles removal after replacement should keep final removal reason");
    check(removed_styles->reason.find("inbound relationship preserved")
            != std::string::npos,
        "styles removal after replacement should keep inbound relationship audit");
    check(removed_styles->inbound_relationships.size() == 1,
        "styles removal after replacement should keep structured inbound audit");
    const auto& styles_inbound = removed_styles->inbound_relationships.front();
    check(styles_inbound.owner_part == workbook_part.value(),
        "styles removal after replacement should keep inbound owner part");
    check(styles_inbound.owner_entry == "xl/_rels/workbook.xml.rels",
        "styles removal after replacement should keep inbound owner relationship entry");
    check(styles_inbound.relationship_id == "rId4",
        "styles removal after replacement should keep inbound relationship id");
    check(styles_inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles",
        "styles removal after replacement should keep inbound relationship type");
    check(styles_inbound.relationship_target == "styles.xml",
        "styles removal after replacement should keep inbound raw target");
    check(styles_inbound.target_part == styles_part,
        "styles removal after replacement should keep normalized inbound target");
    check(editor.manifest().find_part(styles_part) == nullptr,
        "styles removal after replacement should remove manifest part");
    check(editor.manifest().content_types().override_for(styles_part) == nullptr,
        "styles removal after replacement should remove manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "styles removal after replacement should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "styles removal after replacement content types audit should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "styles removal after replacement content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/styles.xml.rels")
            == nullptr,
        "styles removal after replacement should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/_rels/styles.xml.rels") == nullptr,
        "styles removal after replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles removal after replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles removal after replacement should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles removal after replacement should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles removal after replacement should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles removal after replacement should keep table copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles removal after replacement should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles removal after replacement should keep VBA copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles removal after replacement should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles removal after replacement should keep unknown extension copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "styles removal after replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "styles removal after replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "styles removal after replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.size() == 1,
        "styles removal after replacement output plan should expose one removed part");
    check(output_plan.removed_parts.front().part_name == styles_part,
        "styles removal after replacement output plan should expose removed styles");
    check(output_plan.removed_parts.front().reason.find("after replacement")
            != std::string::npos,
        "styles removal after replacement output plan should keep removed-part reason");
    check(output_plan.removed_parts.front().inbound_relationships.size() == 1,
        "styles removal after replacement output plan should keep removed-part inbound audit");
    check(output_plan.removed_package_entries.empty(),
        "styles removal after replacement output plan should not omit metadata entries");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "styles removal after replacement output plan should omit styles part");
    check_output_entry_part_context(output_plan.entries, "xl/styles.xml",
        true, styles_part.value(),
        "styles removal after replacement output plan should classify omitted styles");
    const auto* output_styles_plan =
        find_output_entry_plan(output_plan.entries, "xl/styles.xml");
    check(output_styles_plan->reason.find("after replacement") != std::string::npos,
        "styles removal after replacement output plan should keep final removal reason");
    check(output_styles_plan->inbound_relationships.size() == 1,
        "styles removal after replacement output plan should expose inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "xl/styles.xml", workbook_part.value(), "xl/_rels/workbook.xml.rels",
        "rId4",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles",
        "styles.xml", styles_part,
        "styles removal after replacement output plan should keep workbook inbound audit");
    check(find_output_entry_plan(output_plan.entries, "xl/_rels/styles.xml.rels")
            == nullptr,
        "styles removal after replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "styles removal after replacement output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "styles removal after replacement output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "styles removal after replacement output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "styles removal after replacement output plan should preserve inbound workbook relationships");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/styles.xml") == entries.end(),
        "styles removal after replacement output should omit styles part");
    check(entries.find("xl/_rels/styles.xml.rels") == entries.end(),
        "styles removal after replacement output should not invent owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(styles_part) == nullptr,
        "styles removal after replacement output should remove styles content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/styles.xml",
        "styles removal after replacement content types XML should omit styles override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "styles removal after replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "styles removal after replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "styles removal after replacement should preserve inbound workbook relationships");
    check(output_reader.relationships_for(styles_part) == nullptr,
        "styles removal after replacement should not create owner relationships for absent styles");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "styles removal after replacement should keep workbook relationships readable");
    const auto* styles_link = workbook_relationships->find_by_id("rId4");
    check(styles_link != nullptr,
        "styles removal after replacement should keep inbound styles relationship id");
    check(styles_link->target == "styles.xml",
        "styles removal after replacement should not rewrite inbound styles target");
    check(styles_link->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "styles removal after replacement should keep inbound styles target mode");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "styles removal after replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "styles removal after replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "styles removal after replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "styles removal after replacement should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "styles removal after replacement should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "styles removal after replacement should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "styles removal after replacement should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "styles removal after replacement should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "styles removal after replacement should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "styles removal after replacement should preserve VBA bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "styles removal after replacement should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "styles removal after replacement should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "styles removal after replacement should preserve unknown extension relationships bytes");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "styles removal after replacement should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "styles removal after replacement should keep table content type override");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "styles removal after replacement should keep chart content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "styles removal after replacement should keep VBA content type override");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "styles removal after replacement should keep calcChain content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "styles removal after replacement should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "styles removal after replacement should not promote PNG media to an override");
}

void test_package_editor_vba_project_replacement_restores_prior_removal()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-replace-after-remove-vba-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-vba-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName vba_part("/xl/vbaProject.bin");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    editor.remove_part(vba_part, "temporary VBA project removal");
    check(editor.edit_plan().find_removed_part(vba_part) != nullptr,
        "setup should record removed VBA before replacement restore");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/vbaProject.bin.rels")
            == nullptr,
        "setup should not invent VBA owner relationships omission");
    const auto* removal_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(removal_content_types != nullptr,
        "setup should record content types rewrite before VBA restore");
    check(removal_content_types->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "setup content types audit should be local-DOM-rewrite after VBA removal");
    check(editor.manifest().find_part(vba_part) == nullptr,
        "setup should remove VBA from manifest before replacement restore");

    std::string restored_vba = "VBA";
    restored_vba.push_back('\0');
    restored_vba += "PROJECT";
    restored_vba.push_back('\0');
    restored_vba += "restored";
    replace_part_with_memory_chunks(
        editor, vba_part, restored_vba, "restored VBA project after removal");

    check(editor.edit_plan().find_removed_part(vba_part) == nullptr,
        "VBA replacement after removal should clear stale removed-part audit");
    const auto* vba_plan = editor.edit_plan().find_part(vba_part);
    check(vba_plan != nullptr,
        "VBA replacement after removal should restore active edit-plan part");
    check(vba_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "VBA replacement after removal should keep final write mode");
    check(vba_plan->reason.find("after removal") != std::string::npos,
        "VBA replacement after removal should keep final replacement reason");
    check_manifest_write_mode(editor, vba_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "VBA replacement after removal should restore manifest part write mode");
    check(editor.manifest().content_types().override_for(vba_part) != nullptr,
        "VBA replacement after removal should restore manifest content type override");
    const auto* restored_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(restored_content_types != nullptr,
        "VBA replacement after removal should keep content types audit");
    check(restored_content_types->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA replacement after removal should restore source content types audit");
    check(restored_content_types->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "VBA replacement after removal should keep content types audit role");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/vbaProject.bin.rels")
            == nullptr,
        "VBA replacement after removal should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/_rels/vbaProject.bin.rels") == nullptr,
        "VBA replacement after removal should not invent owner relationships audit");
    check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels") == nullptr,
        "VBA replacement after removal should not rewrite workbook relationships");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "VBA replacement after removal should not rewrite package relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA replacement after removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA replacement after removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA replacement after removal should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA replacement after removal should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA replacement after removal should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA replacement after removal should keep table copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA replacement after removal should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA replacement after removal should keep styles copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA replacement after removal should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA replacement after removal should keep unknown extension copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "VBA replacement after removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "VBA replacement after removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "VBA replacement after removal output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "VBA replacement after removal output plan should clear stale removed-part audits");
    check(output_plan.removed_package_entries.empty(),
        "VBA replacement after removal output plan should clear stale removed package-entry audits");
    check_output_entry_plan(output_plan.entries, "xl/vbaProject.bin",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "VBA replacement after removal output plan should rewrite VBA project");
    check_output_entry_part_context(output_plan.entries, "xl/vbaProject.bin",
        true, vba_part.value(),
        "VBA replacement after removal output plan should classify rewritten VBA project");
    const auto* output_vba_plan =
        find_output_entry_plan(output_plan.entries, "xl/vbaProject.bin");
    check(output_vba_plan->reason.find("after removal") != std::string::npos,
        "VBA replacement after removal output plan should keep replacement reason");
    check(find_output_entry_plan(output_plan.entries, "xl/_rels/vbaProject.bin.rels")
            == nullptr,
        "VBA replacement after removal output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA replacement after removal output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "VBA replacement after removal output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "VBA replacement after removal output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA replacement after removal output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA replacement after removal output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA replacement after removal output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA replacement after removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA replacement after removal output plan should preserve drawing");
    check_output_entry_plan(output_plan.entries, "xl/charts/chart1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA replacement after removal output plan should preserve chart");
    check_output_entry_plan(output_plan.entries, "xl/media/image1.png",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA replacement after removal output plan should preserve media");
    check_output_entry_plan(output_plan.entries, "xl/tables/table1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA replacement after removal output plan should preserve table");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA replacement after removal output plan should preserve sharedStrings");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA replacement after removal output plan should preserve styles");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA replacement after removal output plan should preserve calcChain");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA replacement after removal output plan should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/vbaProject.bin") != entries.end(),
        "VBA replacement after removal output should restore VBA project entry");
    check(entries.find("xl/_rels/vbaProject.bin.rels") == entries.end(),
        "VBA replacement after removal output should not invent owner relationships entry");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, vba_part.zip_path());
    check(output_reader.read_entry("xl/vbaProject.bin") == restored_vba,
        "VBA replacement after removal should write restored VBA bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "VBA replacement after removal should restore source content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "VBA replacement after removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "VBA replacement after removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "VBA replacement after removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "VBA replacement after removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "VBA replacement after removal should preserve drawing bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "VBA replacement after removal should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "VBA replacement after removal should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "VBA replacement after removal should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "VBA replacement after removal should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "VBA replacement after removal should preserve styles bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "VBA replacement after removal should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "VBA replacement after removal should preserve unknown extension bytes");

    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "VBA replacement after removal should keep workbook relationships readable");
    const auto* vba_link = workbook_relationships->find_by_id("rId2");
    check(vba_link != nullptr,
        "VBA replacement after removal should keep inbound VBA relationship id");
    check(vba_link->target == "vbaProject.bin",
        "VBA replacement after removal should not rewrite inbound VBA target");
    check(vba_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "VBA replacement after removal should keep inbound VBA target mode");
    check(output_reader.relationships_for(vba_part) == nullptr,
        "VBA replacement after removal should not create owner relationships");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "VBA replacement after removal should restore VBA content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "VBA replacement after removal should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "VBA replacement after removal should keep styles content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "VBA replacement after removal should preserve PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "VBA replacement after removal should not promote PNG media to an override");
}

void test_package_editor_vba_project_removal_overrides_prior_replacement()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-after-replace-vba-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-vba-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName vba_part("/xl/vbaProject.bin");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string replacement_vba = std::string("VBA\0PROJECT\0stale", 18);
    replace_part_with_memory_chunks(
        editor, vba_part, replacement_vba, "prior VBA project replacement before removal");
    const auto* prior_vba_plan = editor.edit_plan().find_part(vba_part);
    check(prior_vba_plan != nullptr,
        "setup should record active VBA replacement before removal override");
    check(prior_vba_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "setup VBA replacement should be stream-rewrite before removal override");
    check(editor.edit_plan().find_package_entry("xl/_rels/vbaProject.bin.rels")
            == nullptr,
        "setup VBA replacement should not invent owner relationships audit");

    editor.remove_part(vba_part, "explicit VBA project removal after replacement");

    check(editor.edit_plan().find_part(vba_part) == nullptr,
        "VBA removal after replacement should clear active replacement entry");
    const auto* removed_vba = editor.edit_plan().find_removed_part(vba_part);
    check(removed_vba != nullptr,
        "VBA removal after replacement should record removed-part audit");
    check(removed_vba->reason.find("after replacement") != std::string::npos,
        "VBA removal after replacement should keep final removal reason");
    check(removed_vba->reason.find("inbound relationship preserved")
            != std::string::npos,
        "VBA removal after replacement should keep inbound relationship audit");
    check(removed_vba->inbound_relationships.size() == 1,
        "VBA removal after replacement should keep structured inbound audit");
    const auto& vba_inbound = removed_vba->inbound_relationships.front();
    check(vba_inbound.owner_part == workbook_part.value(),
        "VBA removal after replacement should keep inbound owner part");
    check(vba_inbound.owner_entry == "xl/_rels/workbook.xml.rels",
        "VBA removal after replacement should keep inbound owner relationship entry");
    check(vba_inbound.relationship_id == "rId2",
        "VBA removal after replacement should keep inbound relationship id");
    check(vba_inbound.relationship_target == "vbaProject.bin",
        "VBA removal after replacement should keep inbound raw target");
    check(vba_inbound.target_part == vba_part,
        "VBA removal after replacement should keep normalized inbound target");
    check(editor.manifest().find_part(vba_part) == nullptr,
        "VBA removal after replacement should remove manifest part");
    check(editor.manifest().content_types().override_for(vba_part) == nullptr,
        "VBA removal after replacement should remove manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "VBA removal after replacement should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "VBA removal after replacement content types audit should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "VBA removal after replacement content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/vbaProject.bin.rels")
            == nullptr,
        "VBA removal after replacement should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/_rels/vbaProject.bin.rels") == nullptr,
        "VBA removal after replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA removal after replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA removal after replacement should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA removal after replacement should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA removal after replacement should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA removal after replacement should keep table copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA removal after replacement should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA removal after replacement should keep styles copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA removal after replacement should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA removal after replacement should keep unknown extension copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "VBA removal after replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "VBA removal after replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "VBA removal after replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.size() == 1,
        "VBA removal after replacement output plan should expose one removed part");
    check(output_plan.removed_parts.front().part_name == vba_part,
        "VBA removal after replacement output plan should expose removed VBA project");
    check(output_plan.removed_parts.front().reason.find("after replacement")
            != std::string::npos,
        "VBA removal after replacement output plan should keep removed-part reason");
    check(output_plan.removed_parts.front().inbound_relationships.size() == 1,
        "VBA removal after replacement output plan should keep removed-part inbound audit");
    check(output_plan.removed_package_entries.empty(),
        "VBA removal after replacement output plan should not omit metadata entries");
    check_output_entry_plan(output_plan.entries, "xl/vbaProject.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "VBA removal after replacement output plan should omit target part");
    check_output_entry_part_context(output_plan.entries, "xl/vbaProject.bin",
        true, vba_part.value(),
        "VBA removal after replacement output plan should classify omitted target");
    const auto* output_vba_plan =
        find_output_entry_plan(output_plan.entries, "xl/vbaProject.bin");
    check(output_vba_plan->reason.find("after replacement") != std::string::npos,
        "VBA removal after replacement output plan should keep final removal reason");
    check(output_vba_plan->inbound_relationships.size() == 1,
        "VBA removal after replacement output plan should expose inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "xl/vbaProject.bin", workbook_part.value(), "xl/_rels/workbook.xml.rels",
        "rId2",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vbaProject",
        "vbaProject.bin", vba_part,
        "VBA removal after replacement output plan should keep workbook inbound audit");
    check(find_output_entry_plan(output_plan.entries, "xl/_rels/vbaProject.bin.rels")
            == nullptr,
        "VBA removal after replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "VBA removal after replacement output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false,
        "",
        "VBA removal after replacement output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "VBA removal after replacement output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA removal after replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA removal after replacement output plan should preserve inbound workbook relationships");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/vbaProject.bin") == entries.end(),
        "VBA removal after replacement output should omit VBA project part");
    check(entries.find("xl/_rels/vbaProject.bin.rels") == entries.end(),
        "VBA removal after replacement output should not invent VBA owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(vba_part) == nullptr,
        "VBA removal after replacement output should remove VBA content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/vbaProject.bin",
        "VBA removal after replacement content types XML should omit VBA override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "VBA removal after replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "VBA removal after replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "VBA removal after replacement should preserve inbound workbook relationships");
    check(output_reader.relationships_for(vba_part) == nullptr,
        "VBA removal after replacement should not create owner relationships");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "VBA removal after replacement should keep workbook relationships readable");
    const auto* vba_link = workbook_relationships->find_by_id("rId2");
    check(vba_link != nullptr,
        "VBA removal after replacement should keep inbound VBA relationship id");
    check(vba_link->target == "vbaProject.bin",
        "VBA removal after replacement should not rewrite inbound VBA target");
    check(vba_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "VBA removal after replacement should keep inbound VBA target mode");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "VBA removal after replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "VBA removal after replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "VBA removal after replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "VBA removal after replacement should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "VBA removal after replacement should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "VBA removal after replacement should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "VBA removal after replacement should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "VBA removal after replacement should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "VBA removal after replacement should preserve styles bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "VBA removal after replacement should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "VBA removal after replacement should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "VBA removal after replacement should preserve unknown extension relationships bytes");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "VBA removal after replacement should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "VBA removal after replacement should keep styles content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "VBA removal after replacement should keep table content type override");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "VBA removal after replacement should keep chart content type override");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "VBA removal after replacement should keep calcChain content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "VBA removal after replacement should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "VBA removal after replacement should not promote PNG media to an override");
}


} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor preservation shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "preservation-core")) {
            test_package_editor_replaces_drawing_and_preserves_linked_media_entries();
            test_package_editor_replaces_unknown_extension_and_preserves_owner_relationships();
            test_package_editor_repeated_unknown_extension_replacement_upserts_owner_audit();
            test_package_editor_unknown_extension_replacement_restores_prior_removal();
            test_package_editor_unknown_extension_removal_overrides_prior_replacement();
            test_package_editor_media_replacement_restores_prior_removal();
            test_package_editor_media_removal_overrides_prior_replacement();
            test_package_editor_chart_replacement_restores_prior_removal();
            test_package_editor_table_replacement_restores_prior_removal();
            test_package_editor_table_removal_overrides_prior_replacement();
            test_package_editor_vml_drawing_replacement_restores_prior_removal();
            test_package_editor_percent_decoded_drawing_replacement_restores_prior_removal();
            test_package_editor_vml_drawing_removal_overrides_prior_replacement();
            test_package_editor_percent_decoded_drawing_removal_overrides_prior_replacement();
            test_package_editor_workbook_replacement_restores_prior_removal();
            test_package_editor_workbook_removal_overrides_prior_replacement();
            test_package_editor_worksheet_replacement_restores_prior_removal();
            test_package_editor_worksheet_removal_overrides_prior_replacement();
            test_package_editor_drawing_replacement_restores_prior_removal();
            test_package_editor_drawing_removal_overrides_prior_replacement();
            test_package_editor_shared_strings_replacement_restores_prior_removal();
            test_package_editor_shared_strings_removal_overrides_prior_replacement();
            test_package_editor_styles_replacement_restores_prior_removal();
            test_package_editor_styles_removal_overrides_prior_replacement();
            test_package_editor_vba_project_replacement_restores_prior_removal();
            test_package_editor_vba_project_removal_overrides_prior_replacement();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
