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
    return shard == "all" || shard == "cellstore-chunks";
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


} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor cellstore shard: " << shard << '\n';

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
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
