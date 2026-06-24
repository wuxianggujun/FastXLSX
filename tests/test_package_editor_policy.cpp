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
    return shard == "all" || shard == "policy";
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
