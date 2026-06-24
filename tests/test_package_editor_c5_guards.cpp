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
    return shard == "all" || shard == "c5-guards";
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

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "c5-guards")) {
            test_package_editor_rejects_changed_planned_staged_chunk_sizes_without_state_changes();
            test_package_editor_rejects_changed_planned_staged_chunk_sizes_for_sheet_data_without_state_changes();
            test_package_editor_rejects_changed_planned_staged_chunk_crc_without_state_changes();
            test_package_editor_rejects_missing_planned_staged_chunk_file_at_read_boundary();
            test_package_editor_contextualizes_by_name_planned_staged_chunk_read_failures_without_state_changes();
            test_package_editor_rejects_changed_planned_staged_chunk_crc_for_sheet_data_without_state_changes();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
