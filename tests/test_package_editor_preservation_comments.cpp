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
    return shard == "all" || shard == "preservation-comments";
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
void test_package_editor_replaces_comments_and_preserves_worksheet_links()
{
    const CommentsSourcePackage source =
        write_comments_source_package("fastxlsx-package-editor-linked-comments-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-linked-comments-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName comments_part("/xl/comments/comment1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string replacement_comments =
        R"(<comments xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<authors><author>Patch</author></authors>)"
        R"(<commentList><comment ref="A1" authorId="0"><text><t>patched comment</t></text></comment></commentList>)"
        R"(</comments>)";
    replace_part_with_memory_chunks(editor, comments_part, replacement_comments,
        "linked fixture comments local-DOM rewrite");

    const auto* comments_plan = editor.edit_plan().find_part(comments_part);
    check(comments_plan != nullptr,
        "linked fixture comments replacement should be present in the edit plan");
    check(comments_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "linked fixture comments replacement should be local-DOM-rewrite");
    check_manifest_write_mode(editor, comments_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "linked fixture comments replacement should mirror write mode into manifest");
    check(editor.edit_plan().find_package_entry("xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "linked fixture comments replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture workbook should remain copy-original after comments replacement");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture worksheet should remain copy-original after comments replacement");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture unknown part should remain copy-original after comments replacement");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "linked fixture comments replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "linked fixture comments replacement output plan should keep calcChain preserve state");
    check(output_plan.notes.empty(),
        "linked fixture comments replacement output plan should not invent audit notes");
    check(output_plan.relationship_target_audits.empty(),
        "linked fixture comments replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "linked fixture comments replacement output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "linked fixture comments replacement output plan should not expose removed package entries");
    check_output_entry_plan(output_plan.entries, "xl/comments/comment1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "linked fixture comments replacement output plan should rewrite comments part");
    check_output_entry_part_context(output_plan.entries, "xl/comments/comment1.xml",
        true, comments_part.value(),
        "linked fixture comments replacement output plan should classify comments part");
    const auto* output_comments_plan =
        find_output_entry_plan(output_plan.entries, "xl/comments/comment1.xml");
    check(output_comments_plan->reason.find("comments") != std::string::npos,
        "linked fixture comments replacement output plan should keep replacement reason");
    check(find_output_entry_plan(output_plan.entries, "xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "linked fixture comments replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture comments replacement output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml",
        false, "",
        "linked fixture comments replacement output plan should classify content types as metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture comments replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture comments replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture comments replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture comments replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture comments replacement output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture comments replacement output plan should preserve unknown entry");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, comments_part.zip_path());
    check(output_reader.read_entry("xl/comments/comment1.xml") == replacement_comments,
        "linked fixture comments replacement should write replacement comments XML");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "linked fixture comments replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "linked fixture comments replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "linked fixture comments replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "linked fixture comments replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "linked fixture comments replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "linked fixture comments replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "linked fixture comments replacement should preserve unknown bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "linked fixture comments replacement should keep worksheet relationships readable");
    const auto* comments_relationship = worksheet_relationships->find_by_id("rId1");
    check(comments_relationship != nullptr,
        "linked fixture comments replacement should keep worksheet comments relationship id");
    check(comments_relationship->target == "../comments/comment1.xml",
        "linked fixture comments replacement should keep worksheet comments relationship target");
    check(comments_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "linked fixture comments replacement should keep worksheet comments target mode");
    check(output_reader.relationships_for(comments_part) == nullptr,
        "linked fixture comments replacement should not invent comments owner relationships");
    check(output_reader.content_types().override_for(comments_part) != nullptr,
        "linked fixture comments replacement should keep comments content type override");
}

void test_package_editor_repeated_comments_replacement_updates_final_state()
{
    const CommentsSourcePackage source =
        write_comments_source_package("fastxlsx-package-editor-repeat-comments-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-repeat-comments-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName comments_part("/xl/comments/comment1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string stale_comments =
        R"(<comments xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<authors><author>Stale</author></authors>)"
        R"(<commentList><comment ref="A1" authorId="0"><text><t>stale comment</t></text></comment></commentList>)"
        R"(</comments>)";
    const std::string final_comments =
        R"(<comments xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<authors><author>Final</author></authors>)"
        R"(<commentList><comment ref="A1" authorId="0"><text><t>final comment</t></text></comment></commentList>)"
        R"(</comments>)";

    replace_part_with_memory_chunks(editor, comments_part, stale_comments,
        "stale repeated comments local-DOM rewrite");
    replace_part_with_memory_chunks(editor, comments_part, final_comments,
        "final repeated comments local-DOM rewrite");

    const auto* comments_plan = editor.edit_plan().find_part(comments_part);
    check(comments_plan != nullptr,
        "repeated comments replacement should keep an active edit-plan part");
    check(comments_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated comments replacement should keep final local-DOM-rewrite mode");
    check(comments_plan->reason.find("final repeated") != std::string::npos,
        "repeated comments replacement should keep final reason");
    check(comments_plan->reason.find("stale repeated") == std::string::npos,
        "repeated comments replacement should drop stale reason");
    check_manifest_write_mode(editor, comments_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated comments replacement should mirror final write mode into manifest");
    check(editor.manifest().content_types().override_for(comments_part) != nullptr,
        "repeated comments replacement should keep comments content type override");
    check(editor.edit_plan().find_removed_part(comments_part) == nullptr,
        "repeated comments replacement should not leave removed-part audit");
    check(editor.edit_plan().find_removed_package_entry("xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "repeated comments replacement should not leave owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "repeated comments replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "repeated comments replacement should not rewrite content types audit");
    check(editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == nullptr,
        "repeated comments replacement should not rewrite inbound worksheet relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated comments replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated comments replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated comments replacement should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "repeated comments replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "repeated comments replacement output plan should preserve calcChain policy");
    check(output_plan.notes.empty(),
        "repeated comments replacement output plan should not invent audit notes");
    check(output_plan.relationship_target_audits.empty(),
        "repeated comments replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "repeated comments replacement output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "repeated comments replacement output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "xl/comments/comment1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "repeated comments replacement output plan should rewrite comments");
    const auto* output_comments_plan =
        find_output_entry_plan(output_plan.entries, "xl/comments/comment1.xml");
    check(output_comments_plan->reason.find("final repeated") != std::string::npos,
        "repeated comments replacement output plan should keep final reason");
    check(output_comments_plan->reason.find("stale repeated") == std::string::npos,
        "repeated comments replacement output plan should drop stale reason");
    check(find_output_entry_plan(output_plan.entries, "xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "repeated comments replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated comments replacement output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated comments replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated comments replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated comments replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated comments replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated comments replacement output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated comments replacement output plan should preserve unknown entry");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, comments_part.zip_path());
    check(output_reader.read_entry("xl/comments/comment1.xml") == final_comments,
        "repeated comments replacement should write final comments payload");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "repeated comments replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "repeated comments replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "repeated comments replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "repeated comments replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "repeated comments replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "repeated comments replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "repeated comments replacement should preserve unknown bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "repeated comments replacement should keep worksheet relationships readable");
    const auto* comments_relationship = worksheet_relationships->find_by_id("rId1");
    check(comments_relationship != nullptr,
        "repeated comments replacement should keep inbound comments relationship id");
    check(comments_relationship->target == "../comments/comment1.xml",
        "repeated comments replacement should not rewrite inbound comments target");
    check(output_reader.relationships_for(comments_part) == nullptr,
        "repeated comments replacement should not invent comments owner relationships");
    check(output_reader.content_types().override_for(comments_part) != nullptr,
        "repeated comments replacement should keep comments content type override");
}

void test_package_editor_removes_comments_and_preserves_worksheet_links()
{
    const CommentsSourcePackage source =
        write_comments_source_package("fastxlsx-package-editor-remove-comments-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-comments-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName comments_part("/xl/comments/comment1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    editor.remove_part(comments_part, "explicit comments part removal");

    check(editor.edit_plan().find_part(comments_part) == nullptr,
        "explicit comments removal should clear the active edit-plan part");
    const auto* removed_comments = editor.edit_plan().find_removed_part(comments_part);
    check(removed_comments != nullptr,
        "explicit comments removal should record removed-part audit");
    check(removed_comments->reason.find("comments part") != std::string::npos,
        "explicit comments removal should retain the removal reason");
    check(removed_comments->reason.find("inbound relationship preserved")
            != std::string::npos,
        "explicit comments removal should audit preserved inbound relationships");
    check(removed_comments->reason.find("/xl/worksheets/sheet1.xml")
            != std::string::npos,
        "explicit comments removal inbound audit should include owner part");
    check(removed_comments->reason.find("rId1") != std::string::npos,
        "explicit comments removal inbound audit should include relationship id");
    check(removed_comments->reason.find("../comments/comment1.xml")
            != std::string::npos,
        "explicit comments removal inbound audit should include original target");
    check(removed_comments->inbound_relationships.size() == 1,
        "explicit comments removal should keep structured inbound audit");
    const auto& comments_inbound = removed_comments->inbound_relationships.front();
    check(comments_inbound.owner_part == worksheet_part.value(),
        "explicit comments removal should keep inbound owner part");
    check(comments_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "explicit comments removal should keep inbound owner relationship entry");
    check(comments_inbound.relationship_id == "rId1",
        "explicit comments removal should keep inbound relationship id");
    check(comments_inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments",
        "explicit comments removal should keep inbound relationship type");
    check(comments_inbound.relationship_target == "../comments/comment1.xml",
        "explicit comments removal should keep inbound raw target");
    check(comments_inbound.target_part == comments_part,
        "explicit comments removal should keep normalized target part");
    check(editor.manifest().find_part(comments_part) == nullptr,
        "explicit comments removal should remove the part from the manifest");
    check(editor.manifest().content_types().override_for(comments_part) == nullptr,
        "explicit comments removal should remove the manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "explicit comments removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "explicit comments removal content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "explicit comments removal content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry("xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "explicit comments removal should not invent comments owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "explicit comments removal should not invent comments owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit comments removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit comments removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit comments removal should keep unknown part copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/comments/comment1.xml") == entries.end(),
        "explicit comments removal output should omit comments part");
    check(entries.find("xl/worksheets/_rels/sheet1.xml.rels") != entries.end(),
        "explicit comments removal output should keep worksheet relationships");
    check(entries.find("xl/comments/_rels/comment1.xml.rels") == entries.end(),
        "explicit comments removal output should not invent comments owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(comments_part) == nullptr,
        "explicit comments removal output should remove comments content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/comments/comment1.xml",
        "explicit comments removal content types XML should omit comments override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "explicit comments removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "explicit comments removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "explicit comments removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "explicit comments removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "explicit comments removal should not prune inbound worksheet relationships");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "explicit comments removal should preserve unknown bytes");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "explicit comments removal should keep worksheet relationships readable");
    const auto* comments_relationship = worksheet_relationships->find_by_id("rId1");
    check(comments_relationship != nullptr,
        "explicit comments removal should keep inbound comments relationship id");
    check(comments_relationship->target == "../comments/comment1.xml",
        "explicit comments removal should not rewrite inbound comments target");
    check(comments_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "explicit comments removal should keep inbound comments target mode");
    check(output_reader.relationships_for(comments_part) == nullptr,
        "explicit comments removal should not keep owner relationships for absent comments");
}

void test_package_editor_comments_replacement_restores_prior_removal()
{
    const CommentsSourcePackage source =
        write_comments_source_package("fastxlsx-package-editor-replace-after-remove-comments-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-comments-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName comments_part("/xl/comments/comment1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    editor.remove_part(comments_part, "temporary comments removal");
    check(editor.edit_plan().find_removed_part(comments_part) != nullptr,
        "setup should record removed comments before replacement restore");
    check(editor.edit_plan().find_removed_package_entry("xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "setup should not invent comments owner relationships omission");
    const auto* removal_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(removal_content_types != nullptr,
        "setup should record content types rewrite before comments restore");
    check(removal_content_types->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "setup content types audit should be local-DOM-rewrite after comments removal");
    check(editor.manifest().find_part(comments_part) == nullptr,
        "setup should remove comments from manifest before replacement restore");

    const std::string restored_comments =
        R"(<comments xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<authors><author>Restored</author></authors>)"
        R"(<commentList><comment ref="A1" authorId="0"><text><t>restored comment</t></text></comment></commentList>)"
        R"(</comments>)";
    replace_part_with_memory_chunks(editor, comments_part, restored_comments,
        "restored comments after removal");

    check(editor.edit_plan().find_removed_part(comments_part) == nullptr,
        "comments replacement after removal should clear stale removed-part audit");
    const auto* comments_plan = editor.edit_plan().find_part(comments_part);
    check(comments_plan != nullptr,
        "comments replacement after removal should restore active edit-plan part");
    check(comments_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "comments replacement after removal should keep final write mode");
    check(comments_plan->reason.find("after removal") != std::string::npos,
        "comments replacement after removal should keep final replacement reason");
    check_manifest_write_mode(editor, comments_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "comments replacement after removal should restore manifest part write mode");
    check(editor.manifest().content_types().override_for(comments_part) != nullptr,
        "comments replacement after removal should restore manifest content type override");
    const auto* restored_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(restored_content_types != nullptr,
        "comments replacement after removal should keep content types audit");
    check(restored_content_types->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "comments replacement after removal should restore source content types audit");
    check(restored_content_types->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "comments replacement after removal should keep content types audit role");
    check(editor.edit_plan().find_removed_package_entry("xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "comments replacement after removal should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "comments replacement after removal should not invent owner relationships audit");
    check(editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == nullptr,
        "comments replacement after removal should not rewrite inbound worksheet relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "comments replacement after removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "comments replacement after removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "comments replacement after removal should keep unknown part copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "comments replacement after removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "comments replacement after removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "comments replacement after removal output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "comments replacement after removal output plan should clear removed parts");
    check(output_plan.removed_package_entries.empty(),
        "comments replacement after removal output plan should clear removed package entries");
    check_output_entry_plan(output_plan.entries, "xl/comments/comment1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "comments replacement after removal output plan should rewrite comments part");
    check_output_entry_part_context(output_plan.entries, "xl/comments/comment1.xml",
        true, comments_part.value(),
        "comments replacement after removal output plan should classify rewritten comments");
    const auto* output_comments_plan =
        find_output_entry_plan(output_plan.entries, "xl/comments/comment1.xml");
    check(output_comments_plan->reason.find("after removal") != std::string::npos,
        "comments replacement after removal output plan should keep comments replacement reason");
    check(find_output_entry_plan(output_plan.entries, "xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "comments replacement after removal output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "comments replacement after removal output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false,
        "",
        "comments replacement after removal output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "comments replacement after removal output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "comments replacement after removal output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "comments replacement after removal output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "comments replacement after removal output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "comments replacement after removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "comments replacement after removal output plan should preserve inbound worksheet relationships");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "comments replacement after removal output plan should preserve unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/comments/comment1.xml") != entries.end(),
        "comments replacement after removal output should restore comments part entry");
    check(entries.find("xl/worksheets/_rels/sheet1.xml.rels") != entries.end(),
        "comments replacement after removal output should keep worksheet relationships");
    check(entries.find("xl/comments/_rels/comment1.xml.rels") == entries.end(),
        "comments replacement after removal output should not invent owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, comments_part.zip_path());
    check(output_reader.read_entry("xl/comments/comment1.xml") == restored_comments,
        "comments replacement after removal should write restored comments bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "comments replacement after removal should restore source content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "comments replacement after removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "comments replacement after removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "comments replacement after removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "comments replacement after removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "comments replacement after removal should preserve inbound worksheet relationships");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "comments replacement after removal should preserve unknown bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "comments replacement after removal should keep worksheet relationships readable");
    const auto* comments_relationship = worksheet_relationships->find_by_id("rId1");
    check(comments_relationship != nullptr,
        "comments replacement after removal should keep inbound comments relationship id");
    check(comments_relationship->target == "../comments/comment1.xml",
        "comments replacement after removal should not rewrite inbound comments target");
    check(comments_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "comments replacement after removal should keep inbound comments target mode");
    check(output_reader.relationships_for(comments_part) == nullptr,
        "comments replacement after removal should not create owner relationships");
    check(output_reader.content_types().override_for(comments_part) != nullptr,
        "comments replacement after removal should restore comments content type override");
}

void test_package_editor_comments_removal_overrides_prior_replacement()
{
    const CommentsSourcePackage source =
        write_comments_source_package("fastxlsx-package-editor-remove-after-replace-comments-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-comments-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName comments_part("/xl/comments/comment1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string replacement_comments =
        R"(<comments xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<authors><author>Stale</author></authors>)"
        R"(<commentList><comment ref="A1" authorId="0"><text><t>stale comment</t></text></comment></commentList>)"
        R"(</comments>)";
    replace_part_with_memory_chunks(editor, comments_part, replacement_comments,
        "prior comments replacement before removal");
    const auto* prior_comments_plan = editor.edit_plan().find_part(comments_part);
    check(prior_comments_plan != nullptr,
        "setup should record active comments replacement before removal override");
    check(prior_comments_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "setup comments replacement should be local-DOM-rewrite before removal override");
    check(editor.edit_plan().find_package_entry("xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "setup comments replacement should not invent owner relationships audit");

    editor.remove_part(comments_part, "explicit comments removal after replacement");

    check(editor.edit_plan().find_part(comments_part) == nullptr,
        "comments removal after replacement should clear active replacement entry");
    const auto* removed_comments = editor.edit_plan().find_removed_part(comments_part);
    check(removed_comments != nullptr,
        "comments removal after replacement should record removed-part audit");
    check(removed_comments->reason.find("after replacement") != std::string::npos,
        "comments removal after replacement should keep final removal reason");
    check(removed_comments->reason.find("inbound relationship preserved")
            != std::string::npos,
        "comments removal after replacement should keep inbound relationship audit");
    check(removed_comments->inbound_relationships.size() == 1,
        "comments removal after replacement should keep structured inbound audit");
    check(removed_comments->inbound_relationships.front().target_part == comments_part,
        "comments removal after replacement should keep normalized inbound target");
    check(editor.manifest().find_part(comments_part) == nullptr,
        "comments removal after replacement should remove manifest part");
    check(editor.manifest().content_types().override_for(comments_part) == nullptr,
        "comments removal after replacement should remove manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "comments removal after replacement should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "comments removal after replacement content types audit should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "comments removal after replacement content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry("xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "comments removal after replacement should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "comments removal after replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "comments removal after replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "comments removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "comments removal after replacement should keep unknown part copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "comments removal after replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "comments removal after replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "comments removal after replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.size() == 1,
        "comments removal after replacement output plan should expose one removed part");
    check(output_plan.removed_parts.front().part_name == comments_part,
        "comments removal after replacement output plan should expose removed comments part");
    check(output_plan.removed_parts.front().reason.find("after replacement")
            != std::string::npos,
        "comments removal after replacement output plan should keep removed-part reason");
    check(output_plan.removed_parts.front().inbound_relationships.size() == 1,
        "comments removal after replacement output plan should keep removed-part inbound audit");
    check(output_plan.removed_package_entries.empty(),
        "comments removal after replacement output plan should not omit metadata entries");
    check_output_entry_plan(output_plan.entries, "xl/comments/comment1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "comments removal after replacement output plan should omit comments part");
    check_output_entry_part_context(output_plan.entries, "xl/comments/comment1.xml",
        true, comments_part.value(),
        "comments removal after replacement output plan should classify omitted comments");
    const auto* output_comments_plan =
        find_output_entry_plan(output_plan.entries, "xl/comments/comment1.xml");
    check(output_comments_plan->reason.find("after replacement") != std::string::npos,
        "comments removal after replacement output plan should keep final removal reason");
    check(output_comments_plan->inbound_relationships.size() == 1,
        "comments removal after replacement output plan should expose inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "xl/comments/comment1.xml", worksheet_part.value(),
        "xl/worksheets/_rels/sheet1.xml.rels", "rId1",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments",
        "../comments/comment1.xml", comments_part,
        "comments removal after replacement output plan should keep worksheet inbound audit");
    check(find_output_entry_plan(output_plan.entries, "xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "comments removal after replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "comments removal after replacement output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false,
        "",
        "comments removal after replacement output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "comments removal after replacement output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "comments removal after replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "comments removal after replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "comments removal after replacement output plan should preserve inbound worksheet relationships");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/comments/comment1.xml") == entries.end(),
        "comments removal after replacement output should omit comments part");
    check(entries.find("xl/worksheets/_rels/sheet1.xml.rels") != entries.end(),
        "comments removal after replacement output should keep worksheet relationships");
    check(entries.find("xl/comments/_rels/comment1.xml.rels") == entries.end(),
        "comments removal after replacement output should not invent owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(comments_part) == nullptr,
        "comments removal after replacement output should remove comments content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/comments/comment1.xml",
        "comments removal after replacement content types XML should omit comments override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "comments removal after replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "comments removal after replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "comments removal after replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "comments removal after replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "comments removal after replacement should preserve inbound worksheet relationships");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "comments removal after replacement should preserve unknown bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "comments removal after replacement should keep worksheet relationships readable");
    const auto* comments_relationship = worksheet_relationships->find_by_id("rId1");
    check(comments_relationship != nullptr,
        "comments removal after replacement should keep inbound comments relationship id");
    check(comments_relationship->target == "../comments/comment1.xml",
        "comments removal after replacement should not rewrite inbound comments target");
    check(comments_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "comments removal after replacement should keep inbound comments target mode");
    check(output_reader.relationships_for(comments_part) == nullptr,
        "comments removal after replacement should not keep owner relationships for absent comments");
}

void test_package_editor_replaces_threaded_comments_and_preserves_person_links()
{
    const ThreadedCommentsSourcePackage source =
        write_threaded_comments_source_package(
            "fastxlsx-package-editor-linked-threaded-comments-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-linked-threaded-comments-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName comments_part("/xl/comments/comment1.xml");
    const fastxlsx::detail::PartName threaded_comments_part(
        "/xl/threadedComments/threadedComment1.xml");
    const fastxlsx::detail::PartName persons_part("/xl/persons/person.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string replacement_threaded_comments =
        R"(<ThreadedComments xmlns="http://schemas.microsoft.com/office/spreadsheetml/2018/threadedcomments">)"
        R"(<threadedComment ref="A1" id="{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}" personId="{22222222-2222-2222-2222-222222222222}" dT="2026-06-08T01:00:00Z">)"
        R"(<text>Patched threaded comment</text>)"
        R"(</threadedComment>)"
        R"(</ThreadedComments>)";
    replace_part_with_memory_chunks(editor, threaded_comments_part, replacement_threaded_comments,
        "threaded comments local-DOM rewrite");

    const auto* threaded_plan = editor.edit_plan().find_part(threaded_comments_part);
    check(threaded_plan != nullptr,
        "threaded comments replacement should be present in the edit plan");
    check(threaded_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "threaded comments replacement should be local-DOM-rewrite");
    check_manifest_write_mode(editor, threaded_comments_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "threaded comments replacement should mirror write mode into manifest");
    check(editor.edit_plan().find_package_entry(
              "xl/threadedComments/_rels/threadedComment1.xml.rels")
            == nullptr,
        "threaded comments replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "threaded comments replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "threaded comments replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(comments_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "threaded comments replacement should keep legacy comments copy-original");
    check(editor.edit_plan().find_part(persons_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "threaded comments replacement should keep persons copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "threaded comments replacement should keep unknown part copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "threaded comments replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "threaded comments replacement output plan should keep calcChain preserve state");
    check(output_plan.notes.empty(),
        "threaded comments replacement output plan should not invent audit notes");
    check(output_plan.relationship_target_audits.empty(),
        "threaded comments replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "threaded comments replacement output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "threaded comments replacement output plan should not expose removed package entries");
    check_output_entry_plan(output_plan.entries, "xl/threadedComments/threadedComment1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "threaded comments replacement output plan should rewrite threaded comments part");
    check_output_entry_part_context(output_plan.entries,
        "xl/threadedComments/threadedComment1.xml", true, threaded_comments_part.value(),
        "threaded comments replacement output plan should classify threaded comments part");
    const auto* output_threaded_comments_plan =
        find_output_entry_plan(output_plan.entries, "xl/threadedComments/threadedComment1.xml");
    check(output_threaded_comments_plan->reason.find("threaded comments") != std::string::npos,
        "threaded comments replacement output plan should keep replacement reason");
    check(find_output_entry_plan(
              output_plan.entries, "xl/threadedComments/_rels/threadedComment1.xml.rels")
            == nullptr,
        "threaded comments replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "threaded comments replacement output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml",
        false, "",
        "threaded comments replacement output plan should classify content types as metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "threaded comments replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "threaded comments replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "threaded comments replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "threaded comments replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "threaded comments replacement output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/comments/comment1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "threaded comments replacement output plan should preserve legacy comments");
    check_output_entry_plan(output_plan.entries, "xl/persons/person.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "threaded comments replacement output plan should preserve persons");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "threaded comments replacement output plan should preserve unknown entry");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(
        editor.reader(), output_reader, threaded_comments_part.zip_path());
    check(output_reader.read_entry("xl/threadedComments/threadedComment1.xml")
            == replacement_threaded_comments,
        "threaded comments replacement should write replacement threaded comments XML");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "threaded comments replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "threaded comments replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "threaded comments replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "threaded comments replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "threaded comments replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "threaded comments replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/comments/comment1.xml") == source.comments,
        "threaded comments replacement should preserve legacy comments bytes");
    check(output_reader.read_entry("xl/persons/person.xml") == source.persons,
        "threaded comments replacement should preserve persons bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "threaded comments replacement should preserve unknown bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "threaded comments replacement should keep worksheet relationships readable");
    const auto* threaded_relationship =
        worksheet_relationships->find_by_id("rIdThreaded");
    check(threaded_relationship != nullptr,
        "threaded comments replacement should keep threaded comments relationship id");
    check(threaded_relationship->target == "../threadedComments/threadedComment1.xml",
        "threaded comments replacement should keep threaded comments relationship target");
    check(threaded_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "threaded comments replacement should keep threaded comments target mode");
    check(worksheet_relationships->find_by_id("rIdLegacy") != nullptr,
        "threaded comments replacement should keep legacy comments relationship");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "threaded comments replacement should keep workbook relationships readable");
    const auto* persons_relationship = workbook_relationships->find_by_id("rIdPerson");
    check(persons_relationship != nullptr,
        "threaded comments replacement should keep persons relationship id");
    check(persons_relationship->target == "persons/person.xml",
        "threaded comments replacement should keep persons relationship target");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(worksheet_part)->find_by_id("rIdThreaded")
            != nullptr,
        "relationship graph should keep threaded comments relationship after replacement");
    check(output_graph.relationships_for(workbook_part)->find_by_id("rIdPerson")
            != nullptr,
        "relationship graph should keep persons relationship after threaded comments replacement");
    check(output_reader.relationships_for(threaded_comments_part) == nullptr,
        "threaded comments replacement should not invent threaded comments owner relationships");
    check(output_reader.content_types().override_for(threaded_comments_part) != nullptr,
        "threaded comments replacement should keep threaded comments content type override");
    check(output_reader.content_types().override_for(persons_part) != nullptr,
        "threaded comments replacement should keep persons content type override");
}

void test_package_editor_repeated_threaded_comments_replacement_updates_final_state()
{
    const ThreadedCommentsSourcePackage source =
        write_threaded_comments_source_package(
            "fastxlsx-package-editor-repeat-threaded-comments-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-repeat-threaded-comments-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName comments_part("/xl/comments/comment1.xml");
    const fastxlsx::detail::PartName threaded_comments_part(
        "/xl/threadedComments/threadedComment1.xml");
    const fastxlsx::detail::PartName persons_part("/xl/persons/person.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string stale_threaded_comments =
        R"(<ThreadedComments xmlns="http://schemas.microsoft.com/office/spreadsheetml/2018/threadedcomments">)"
        R"(<threadedComment ref="A1" id="{aaaaaaaa-bbbb-cccc-dddd-000000000001}" personId="{22222222-2222-2222-2222-222222222222}" dT="2026-06-08T03:00:00Z">)"
        R"(<text>Stale threaded comment</text>)"
        R"(</threadedComment>)"
        R"(</ThreadedComments>)";
    const std::string final_threaded_comments =
        R"(<ThreadedComments xmlns="http://schemas.microsoft.com/office/spreadsheetml/2018/threadedcomments">)"
        R"(<threadedComment ref="A1" id="{aaaaaaaa-bbbb-cccc-dddd-000000000002}" personId="{22222222-2222-2222-2222-222222222222}" dT="2026-06-08T04:00:00Z">)"
        R"(<text>Final threaded comment</text>)"
        R"(</threadedComment>)"
        R"(</ThreadedComments>)";

    replace_part_with_memory_chunks(editor, threaded_comments_part, stale_threaded_comments,
        "stale repeated threaded comments local-DOM rewrite");
    replace_part_with_memory_chunks(editor, threaded_comments_part, final_threaded_comments,
        "final repeated threaded comments local-DOM rewrite");

    const auto* threaded_plan = editor.edit_plan().find_part(threaded_comments_part);
    check(threaded_plan != nullptr,
        "repeated threaded comments replacement should keep an active edit-plan part");
    check(threaded_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated threaded comments replacement should keep final local-DOM-rewrite mode");
    check(threaded_plan->reason.find("final repeated") != std::string::npos,
        "repeated threaded comments replacement should keep final reason");
    check(threaded_plan->reason.find("stale repeated") == std::string::npos,
        "repeated threaded comments replacement should drop stale reason");
    check_manifest_write_mode(editor, threaded_comments_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated threaded comments replacement should mirror final write mode into manifest");
    check(editor.manifest().content_types().override_for(threaded_comments_part) != nullptr,
        "repeated threaded comments replacement should keep content type override");
    check(editor.edit_plan().find_removed_part(threaded_comments_part) == nullptr,
        "repeated threaded comments replacement should not leave removed-part audit");
    check(editor.edit_plan().find_removed_package_entry(
              "xl/threadedComments/_rels/threadedComment1.xml.rels")
            == nullptr,
        "repeated threaded comments replacement should not leave owner relationships omission");
    check(editor.edit_plan().find_package_entry(
              "xl/threadedComments/_rels/threadedComment1.xml.rels")
            == nullptr,
        "repeated threaded comments replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "repeated threaded comments replacement should not rewrite content types audit");
    check(editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == nullptr,
        "repeated threaded comments replacement should not rewrite worksheet relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated threaded comments replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated threaded comments replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(comments_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated threaded comments replacement should keep legacy comments copy-original");
    check(editor.edit_plan().find_part(persons_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated threaded comments replacement should keep persons copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated threaded comments replacement should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "repeated threaded comments replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "repeated threaded comments replacement output plan should preserve calcChain policy");
    check(output_plan.notes.empty(),
        "repeated threaded comments replacement output plan should not invent audit notes");
    check(output_plan.relationship_target_audits.empty(),
        "repeated threaded comments replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "repeated threaded comments replacement output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "repeated threaded comments replacement output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "xl/threadedComments/threadedComment1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "repeated threaded comments replacement output plan should rewrite threaded comments");
    const auto* output_threaded_plan =
        find_output_entry_plan(output_plan.entries, "xl/threadedComments/threadedComment1.xml");
    check(output_threaded_plan->reason.find("final repeated") != std::string::npos,
        "repeated threaded comments replacement output plan should keep final reason");
    check(output_threaded_plan->reason.find("stale repeated") == std::string::npos,
        "repeated threaded comments replacement output plan should drop stale reason");
    check(find_output_entry_plan(
              output_plan.entries, "xl/threadedComments/_rels/threadedComment1.xml.rels")
            == nullptr,
        "repeated threaded comments replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated threaded comments replacement output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated threaded comments replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated threaded comments replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated threaded comments replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated threaded comments replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated threaded comments replacement output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/comments/comment1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated threaded comments replacement output plan should preserve legacy comments");
    check_output_entry_plan(output_plan.entries, "xl/persons/person.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated threaded comments replacement output plan should preserve persons");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated threaded comments replacement output plan should preserve unknown entry");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(
        editor.reader(), output_reader, threaded_comments_part.zip_path());
    check(output_reader.read_entry("xl/threadedComments/threadedComment1.xml")
            == final_threaded_comments,
        "repeated threaded comments replacement should write final threaded comments payload");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "repeated threaded comments replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "repeated threaded comments replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "repeated threaded comments replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "repeated threaded comments replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "repeated threaded comments replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "repeated threaded comments replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/comments/comment1.xml") == source.comments,
        "repeated threaded comments replacement should preserve legacy comments bytes");
    check(output_reader.read_entry("xl/persons/person.xml") == source.persons,
        "repeated threaded comments replacement should preserve persons bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "repeated threaded comments replacement should preserve unknown bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "repeated threaded comments replacement should keep worksheet relationships readable");
    check(worksheet_relationships->find_by_id("rIdThreaded") != nullptr,
        "repeated threaded comments replacement should keep threaded comments relationship");
    check(worksheet_relationships->find_by_id("rIdLegacy") != nullptr,
        "repeated threaded comments replacement should keep legacy comments relationship");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "repeated threaded comments replacement should keep workbook relationships readable");
    check(workbook_relationships->find_by_id("rIdPerson") != nullptr,
        "repeated threaded comments replacement should keep persons relationship");
    check(output_reader.relationships_for(threaded_comments_part) == nullptr,
        "repeated threaded comments replacement should not invent owner relationships");
    check(output_reader.content_types().override_for(threaded_comments_part) != nullptr,
        "repeated threaded comments replacement should keep threaded comments content type override");
    check(output_reader.content_types().override_for(persons_part) != nullptr,
        "repeated threaded comments replacement should keep persons content type override");
}

void test_package_editor_removes_threaded_comments_and_preserves_person_links()
{
    const ThreadedCommentsSourcePackage source =
        write_threaded_comments_source_package(
            "fastxlsx-package-editor-remove-threaded-comments-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-threaded-comments-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName comments_part("/xl/comments/comment1.xml");
    const fastxlsx::detail::PartName threaded_comments_part(
        "/xl/threadedComments/threadedComment1.xml");
    const fastxlsx::detail::PartName persons_part("/xl/persons/person.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    editor.remove_part(threaded_comments_part, "explicit threaded comments part removal");

    check(editor.edit_plan().find_part(threaded_comments_part) == nullptr,
        "threaded comments removal should clear the active edit-plan part");
    const auto* removed_threaded =
        editor.edit_plan().find_removed_part(threaded_comments_part);
    check(removed_threaded != nullptr,
        "threaded comments removal should record removed-part audit");
    check(removed_threaded->reason.find("threaded comments part") != std::string::npos,
        "threaded comments removal should retain the removal reason");
    check(removed_threaded->reason.find("inbound relationship preserved")
            != std::string::npos,
        "threaded comments removal should audit preserved inbound relationships");
    check(removed_threaded->inbound_relationships.size() == 1,
        "threaded comments removal should keep structured inbound audit");
    const auto& threaded_inbound = removed_threaded->inbound_relationships.front();
    check(threaded_inbound.owner_part == worksheet_part.value(),
        "threaded comments removal should keep inbound owner part");
    check(threaded_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "threaded comments removal should keep inbound owner relationship entry");
    check(threaded_inbound.relationship_id == "rIdThreaded",
        "threaded comments removal should keep inbound relationship id");
    check(threaded_inbound.relationship_type
            == "http://schemas.microsoft.com/office/2017/10/relationships/threadedComment",
        "threaded comments removal should keep inbound relationship type");
    check(threaded_inbound.relationship_target == "../threadedComments/threadedComment1.xml",
        "threaded comments removal should keep inbound raw target");
    check(threaded_inbound.target_part == threaded_comments_part,
        "threaded comments removal should keep normalized target part");
    check(editor.manifest().find_part(threaded_comments_part) == nullptr,
        "threaded comments removal should remove the part from the manifest");
    check(editor.manifest().content_types().override_for(threaded_comments_part) == nullptr,
        "threaded comments removal should remove the manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "threaded comments removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "threaded comments removal content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "threaded comments removal content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry(
              "xl/threadedComments/_rels/threadedComment1.xml.rels")
            == nullptr,
        "threaded comments removal should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry(
              "xl/threadedComments/_rels/threadedComment1.xml.rels")
            == nullptr,
        "threaded comments removal should not invent owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "threaded comments removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "threaded comments removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(comments_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "threaded comments removal should keep legacy comments copy-original");
    check(editor.edit_plan().find_part(persons_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "threaded comments removal should keep persons copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "threaded comments removal should keep unknown part copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/threadedComments/threadedComment1.xml") == entries.end(),
        "threaded comments removal output should omit threaded comments part");
    check(entries.find("xl/worksheets/_rels/sheet1.xml.rels") != entries.end(),
        "threaded comments removal output should keep worksheet relationships");
    check(entries.find("xl/persons/person.xml") != entries.end(),
        "threaded comments removal output should keep persons part");
    check(entries.find("xl/threadedComments/_rels/threadedComment1.xml.rels")
            == entries.end(),
        "threaded comments removal output should not invent owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(threaded_comments_part) == nullptr,
        "threaded comments removal output should remove threaded comments content type override");
    check(output_reader.content_types().override_for(persons_part) != nullptr,
        "threaded comments removal output should keep persons content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/threadedComments/threadedComment1.xml",
        "threaded comments removal content types XML should omit threaded comments override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "threaded comments removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "threaded comments removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "threaded comments removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "threaded comments removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "threaded comments removal should not prune inbound worksheet relationships");
    check(output_reader.read_entry("xl/comments/comment1.xml") == source.comments,
        "threaded comments removal should preserve legacy comments bytes");
    check(output_reader.read_entry("xl/persons/person.xml") == source.persons,
        "threaded comments removal should preserve persons bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "threaded comments removal should preserve unknown bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "threaded comments removal should keep worksheet relationships readable");
    const auto* threaded_relationship =
        worksheet_relationships->find_by_id("rIdThreaded");
    check(threaded_relationship != nullptr,
        "threaded comments removal should keep inbound threaded comments relationship id");
    check(threaded_relationship->target == "../threadedComments/threadedComment1.xml",
        "threaded comments removal should not rewrite inbound threaded comments target");
    check(threaded_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "threaded comments removal should keep inbound threaded comments target mode");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "threaded comments removal should keep workbook relationships readable");
    check(workbook_relationships->find_by_id("rIdPerson") != nullptr,
        "threaded comments removal should keep persons relationship");
    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(worksheet_part)->find_by_id("rIdThreaded")
            != nullptr,
        "relationship graph should keep inbound threaded comments relationship after removal");
    check(output_graph.relationships_for(workbook_part)->find_by_id("rIdPerson")
            != nullptr,
        "relationship graph should keep persons relationship after threaded comments removal");
    check(output_reader.relationships_for(threaded_comments_part) == nullptr,
        "threaded comments removal should not keep owner relationships for absent part");
}

void test_package_editor_replaces_persons_and_preserves_threaded_comments_links()
{
    const ThreadedCommentsSourcePackage source =
        write_threaded_comments_source_package(
            "fastxlsx-package-editor-replace-persons-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-persons-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName comments_part("/xl/comments/comment1.xml");
    const fastxlsx::detail::PartName threaded_comments_part(
        "/xl/threadedComments/threadedComment1.xml");
    const fastxlsx::detail::PartName persons_part("/xl/persons/person.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string replacement_persons =
        R"(<personList xmlns="http://schemas.microsoft.com/office/spreadsheetml/2018/threadedcomments">)"
        R"(<person displayName="Patched Person" id="{22222222-2222-2222-2222-222222222222}" providerId="None" userId="patched@example.invalid"/>)"
        R"(</personList>)";
    replace_part_with_memory_chunks(editor, persons_part, replacement_persons,
        "persons local-DOM rewrite");

    const auto* persons_plan = editor.edit_plan().find_part(persons_part);
    check(persons_plan != nullptr,
        "persons replacement should be present in the edit plan");
    check(persons_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "persons replacement should be local-DOM-rewrite");
    check_manifest_write_mode(editor, persons_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "persons replacement should mirror write mode into manifest");
    check(editor.edit_plan().find_package_entry("xl/persons/_rels/person.xml.rels")
            == nullptr,
        "persons replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "persons replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "persons replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(comments_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "persons replacement should keep legacy comments copy-original");
    check(editor.edit_plan().find_part(threaded_comments_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "persons replacement should keep threaded comments copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "persons replacement should keep unknown part copy-original");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, persons_part.zip_path());
    check(output_reader.read_entry("xl/persons/person.xml") == replacement_persons,
        "persons replacement should write replacement persons XML");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "persons replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "persons replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "persons replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "persons replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "persons replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "persons replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/comments/comment1.xml") == source.comments,
        "persons replacement should preserve legacy comments bytes");
    check(output_reader.read_entry("xl/threadedComments/threadedComment1.xml")
            == source.threaded_comments,
        "persons replacement should preserve threaded comments bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "persons replacement should preserve unknown bytes");

    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "persons replacement should keep workbook relationships readable");
    const auto* persons_relationship = workbook_relationships->find_by_id("rIdPerson");
    check(persons_relationship != nullptr,
        "persons replacement should keep persons relationship id");
    check(persons_relationship->target == "persons/person.xml",
        "persons replacement should keep persons relationship target");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "persons replacement should keep worksheet relationships readable");
    check(worksheet_relationships->find_by_id("rIdThreaded") != nullptr,
        "persons replacement should keep threaded comments relationship");
    check(worksheet_relationships->find_by_id("rIdLegacy") != nullptr,
        "persons replacement should keep legacy comments relationship");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(workbook_part)->find_by_id("rIdPerson")
            != nullptr,
        "relationship graph should keep persons relationship after persons replacement");
    check(output_graph.relationships_for(worksheet_part)->find_by_id("rIdThreaded")
            != nullptr,
        "relationship graph should keep threaded comments relationship after persons replacement");
    check(output_reader.relationships_for(persons_part) == nullptr,
        "persons replacement should not invent persons owner relationships");
    check(output_reader.content_types().override_for(persons_part) != nullptr,
        "persons replacement should keep persons content type override");
    check(output_reader.content_types().override_for(threaded_comments_part) != nullptr,
        "persons replacement should keep threaded comments content type override");
}

void test_package_editor_repeated_persons_replacement_updates_final_state()
{
    const ThreadedCommentsSourcePackage source =
        write_threaded_comments_source_package(
            "fastxlsx-package-editor-repeat-persons-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-repeat-persons-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName comments_part("/xl/comments/comment1.xml");
    const fastxlsx::detail::PartName threaded_comments_part(
        "/xl/threadedComments/threadedComment1.xml");
    const fastxlsx::detail::PartName persons_part("/xl/persons/person.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string stale_persons =
        R"(<personList xmlns="http://schemas.microsoft.com/office/spreadsheetml/2018/threadedcomments">)"
        R"(<person displayName="Stale Person" id="{22222222-2222-2222-2222-222222222222}" providerId="None" userId="stale@example.invalid"/>)"
        R"(</personList>)";
    const std::string final_persons =
        R"(<personList xmlns="http://schemas.microsoft.com/office/spreadsheetml/2018/threadedcomments">)"
        R"(<person displayName="Final Person" id="{22222222-2222-2222-2222-222222222222}" providerId="None" userId="final@example.invalid"/>)"
        R"(</personList>)";

    replace_part_with_memory_chunks(editor, persons_part, stale_persons,
        "stale repeated persons local-DOM rewrite");
    replace_part_with_memory_chunks(editor, persons_part, final_persons,
        "final repeated persons local-DOM rewrite");

    const auto* persons_plan = editor.edit_plan().find_part(persons_part);
    check(persons_plan != nullptr,
        "repeated persons replacement should keep an active edit-plan part");
    check(persons_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated persons replacement should keep final local-DOM-rewrite mode");
    check(persons_plan->reason.find("final repeated") != std::string::npos,
        "repeated persons replacement should keep final reason");
    check(persons_plan->reason.find("stale repeated") == std::string::npos,
        "repeated persons replacement should drop stale reason");
    check_manifest_write_mode(editor, persons_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated persons replacement should mirror final write mode into manifest");
    check(editor.manifest().content_types().override_for(persons_part) != nullptr,
        "repeated persons replacement should keep content type override");
    check(editor.edit_plan().find_removed_part(persons_part) == nullptr,
        "repeated persons replacement should not leave removed-part audit");
    check(editor.edit_plan().find_removed_package_entry("xl/persons/_rels/person.xml.rels")
            == nullptr,
        "repeated persons replacement should not leave owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/persons/_rels/person.xml.rels")
            == nullptr,
        "repeated persons replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "repeated persons replacement should not rewrite content types audit");
    check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels") == nullptr,
        "repeated persons replacement should not rewrite workbook relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated persons replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated persons replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(comments_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated persons replacement should keep legacy comments copy-original");
    check(editor.edit_plan().find_part(threaded_comments_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated persons replacement should keep threaded comments copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated persons replacement should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "repeated persons replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "repeated persons replacement output plan should preserve calcChain policy");
    check(output_plan.notes.empty(),
        "repeated persons replacement output plan should not invent audit notes");
    check(output_plan.relationship_target_audits.empty(),
        "repeated persons replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "repeated persons replacement output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "repeated persons replacement output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "xl/persons/person.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "repeated persons replacement output plan should rewrite persons");
    const auto* output_persons_plan =
        find_output_entry_plan(output_plan.entries, "xl/persons/person.xml");
    check(output_persons_plan->reason.find("final repeated") != std::string::npos,
        "repeated persons replacement output plan should keep final reason");
    check(output_persons_plan->reason.find("stale repeated") == std::string::npos,
        "repeated persons replacement output plan should drop stale reason");
    check(find_output_entry_plan(output_plan.entries, "xl/persons/_rels/person.xml.rels")
            == nullptr,
        "repeated persons replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated persons replacement output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated persons replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated persons replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated persons replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated persons replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated persons replacement output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/comments/comment1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated persons replacement output plan should preserve legacy comments");
    check_output_entry_plan(output_plan.entries, "xl/threadedComments/threadedComment1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated persons replacement output plan should preserve threaded comments");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated persons replacement output plan should preserve unknown entry");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, persons_part.zip_path());
    check(output_reader.read_entry("xl/persons/person.xml") == final_persons,
        "repeated persons replacement should write final persons payload");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "repeated persons replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "repeated persons replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "repeated persons replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "repeated persons replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "repeated persons replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "repeated persons replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/comments/comment1.xml") == source.comments,
        "repeated persons replacement should preserve legacy comments bytes");
    check(output_reader.read_entry("xl/threadedComments/threadedComment1.xml")
            == source.threaded_comments,
        "repeated persons replacement should preserve threaded comments bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "repeated persons replacement should preserve unknown bytes");

    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "repeated persons replacement should keep workbook relationships readable");
    check(workbook_relationships->find_by_id("rIdPerson") != nullptr,
        "repeated persons replacement should keep persons relationship");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "repeated persons replacement should keep worksheet relationships readable");
    check(worksheet_relationships->find_by_id("rIdThreaded") != nullptr,
        "repeated persons replacement should keep threaded comments relationship");
    check(worksheet_relationships->find_by_id("rIdLegacy") != nullptr,
        "repeated persons replacement should keep legacy comments relationship");
    check(output_reader.relationships_for(persons_part) == nullptr,
        "repeated persons replacement should not invent owner relationships");
    check(output_reader.content_types().override_for(persons_part) != nullptr,
        "repeated persons replacement should keep persons content type override");
    check(output_reader.content_types().override_for(threaded_comments_part) != nullptr,
        "repeated persons replacement should keep threaded comments content type override");
}

void test_package_editor_removes_persons_and_preserves_threaded_comments_links()
{
    const ThreadedCommentsSourcePackage source =
        write_threaded_comments_source_package(
            "fastxlsx-package-editor-remove-persons-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-persons-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName comments_part("/xl/comments/comment1.xml");
    const fastxlsx::detail::PartName threaded_comments_part(
        "/xl/threadedComments/threadedComment1.xml");
    const fastxlsx::detail::PartName persons_part("/xl/persons/person.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    editor.remove_part(persons_part, "explicit persons part removal");

    check(editor.edit_plan().find_part(persons_part) == nullptr,
        "persons removal should clear the active edit-plan part");
    const auto* removed_persons = editor.edit_plan().find_removed_part(persons_part);
    check(removed_persons != nullptr,
        "persons removal should record removed-part audit");
    check(removed_persons->reason.find("persons part") != std::string::npos,
        "persons removal should retain the removal reason");
    check(removed_persons->reason.find("inbound relationship preserved")
            != std::string::npos,
        "persons removal should audit preserved inbound relationships");
    check(removed_persons->inbound_relationships.size() == 1,
        "persons removal should keep structured workbook inbound audit");
    const auto& persons_inbound = removed_persons->inbound_relationships.front();
    check(persons_inbound.owner_part == workbook_part.value(),
        "persons removal should keep inbound workbook owner part");
    check(persons_inbound.owner_entry == "xl/_rels/workbook.xml.rels",
        "persons removal should keep inbound workbook relationships entry");
    check(persons_inbound.relationship_id == "rIdPerson",
        "persons removal should keep inbound relationship id");
    check(persons_inbound.relationship_type
            == "http://schemas.microsoft.com/office/2017/10/relationships/person",
        "persons removal should keep inbound relationship type");
    check(persons_inbound.relationship_target == "persons/person.xml",
        "persons removal should keep inbound raw target");
    check(persons_inbound.target_part == persons_part,
        "persons removal should keep normalized target part");
    check(editor.manifest().find_part(persons_part) == nullptr,
        "persons removal should remove the part from the manifest");
    check(editor.manifest().content_types().override_for(persons_part) == nullptr,
        "persons removal should remove the manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "persons removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "persons removal content types rewrite should be local-DOM-rewrite");
    check(editor.edit_plan().find_removed_package_entry("xl/persons/_rels/person.xml.rels")
            == nullptr,
        "persons removal should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/persons/_rels/person.xml.rels")
            == nullptr,
        "persons removal should not invent owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "persons removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "persons removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(comments_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "persons removal should keep legacy comments copy-original");
    check(editor.edit_plan().find_part(threaded_comments_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "persons removal should keep threaded comments copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "persons removal should keep unknown part copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/persons/person.xml") == entries.end(),
        "persons removal output should omit persons part");
    check(entries.find("xl/threadedComments/threadedComment1.xml") != entries.end(),
        "persons removal output should keep threaded comments part");
    check(entries.find("xl/comments/comment1.xml") != entries.end(),
        "persons removal output should keep legacy comments part");
    check(entries.find("xl/persons/_rels/person.xml.rels") == entries.end(),
        "persons removal output should not invent owner relationships omission");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(persons_part) == nullptr,
        "persons removal output should remove persons content type override");
    check(output_reader.content_types().override_for(threaded_comments_part) != nullptr,
        "persons removal output should keep threaded comments content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/persons/person.xml",
        "persons removal content types XML should omit persons override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "persons removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "persons removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "persons removal should not prune inbound workbook relationships");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "persons removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "persons removal should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/comments/comment1.xml") == source.comments,
        "persons removal should preserve legacy comments bytes");
    check(output_reader.read_entry("xl/threadedComments/threadedComment1.xml")
            == source.threaded_comments,
        "persons removal should preserve threaded comments bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "persons removal should preserve unknown bytes");

    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "persons removal should keep workbook relationships readable");
    const auto* persons_relationship = workbook_relationships->find_by_id("rIdPerson");
    check(persons_relationship != nullptr,
        "persons removal should keep inbound persons relationship id");
    check(persons_relationship->target == "persons/person.xml",
        "persons removal should not rewrite inbound persons target");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "persons removal should keep worksheet relationships readable");
    check(worksheet_relationships->find_by_id("rIdThreaded") != nullptr,
        "persons removal should keep threaded comments relationship");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(workbook_part)->find_by_id("rIdPerson")
            != nullptr,
        "relationship graph should keep inbound persons relationship after removal");
    check(output_graph.relationships_for(worksheet_part)->find_by_id("rIdThreaded")
            != nullptr,
        "relationship graph should keep threaded comments relationship after persons removal");
    check(output_reader.relationships_for(persons_part) == nullptr,
        "persons removal should not keep owner relationships for absent part");
}

void test_package_editor_threaded_comments_same_path_ordering()
{
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName comments_part("/xl/comments/comment1.xml");
    const fastxlsx::detail::PartName threaded_comments_part(
        "/xl/threadedComments/threadedComment1.xml");
    const fastxlsx::detail::PartName persons_part("/xl/persons/person.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    {
        const ThreadedCommentsSourcePackage source =
            write_threaded_comments_source_package(
                "fastxlsx-package-editor-replace-after-remove-threaded-comments-source.xlsx");
        const std::filesystem::path output =
            output_path("fastxlsx-package-editor-replace-after-remove-threaded-comments-output.xlsx");

        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);

        editor.remove_part(threaded_comments_part, "temporary threaded comments removal");
        check(editor.edit_plan().find_removed_part(threaded_comments_part) != nullptr,
            "setup should record removed threaded comments before replacement restore");
        check(editor.edit_plan().find_removed_package_entry(
                  "xl/threadedComments/_rels/threadedComment1.xml.rels")
                == nullptr,
            "setup should not invent threaded comments owner relationships omission");
        check(editor.manifest().find_part(threaded_comments_part) == nullptr,
            "setup should remove threaded comments from manifest before replacement restore");

        const std::string restored_threaded_comments =
            R"(<ThreadedComments xmlns="http://schemas.microsoft.com/office/spreadsheetml/2018/threadedcomments">)"
            R"(<threadedComment ref="A1" id="{aaaaaaaa-1111-2222-3333-bbbbbbbbbbbb}" personId="{22222222-2222-2222-2222-222222222222}" dT="2026-06-08T02:00:00Z">)"
            R"(<text>Restored threaded comment</text>)"
            R"(</threadedComment>)"
            R"(</ThreadedComments>)";
        replace_part_with_memory_chunks(editor, threaded_comments_part, restored_threaded_comments,
            "restored threaded comments after removal");

        check(editor.edit_plan().find_removed_part(threaded_comments_part) == nullptr,
            "threaded comments replacement after removal should clear stale removed-part audit");
        const auto* threaded_plan = editor.edit_plan().find_part(threaded_comments_part);
        check(threaded_plan != nullptr,
            "threaded comments replacement after removal should restore active edit-plan part");
        check(threaded_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
            "threaded comments replacement after removal should keep final write mode");
        check(threaded_plan->reason.find("after removal") != std::string::npos,
            "threaded comments replacement after removal should keep final replacement reason");
        check_manifest_write_mode(editor, threaded_comments_part,
            fastxlsx::detail::PartWriteMode::StreamRewrite,
            "threaded comments replacement after removal should restore manifest write mode");
        check(editor.manifest().content_types().override_for(threaded_comments_part) != nullptr,
            "threaded comments replacement after removal should restore content type override");
        const auto* content_types_entry =
            editor.edit_plan().find_package_entry("[Content_Types].xml");
        check(content_types_entry != nullptr,
            "threaded comments replacement after removal should keep content types audit");
        check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "threaded comments replacement after removal should restore source content types audit");
        check(editor.edit_plan().find_removed_package_entry(
                  "xl/threadedComments/_rels/threadedComment1.xml.rels")
                == nullptr,
            "threaded comments replacement after removal should not invent owner omission");
        check(editor.edit_plan().find_package_entry(
                  "xl/threadedComments/_rels/threadedComment1.xml.rels")
                == nullptr,
            "threaded comments replacement after removal should not invent owner audit");
        check(editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == nullptr,
            "threaded comments replacement after removal should not rewrite inbound worksheet relationships");
        check(editor.edit_plan().find_part(workbook_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "threaded comments replacement after removal should keep workbook copy-original");
        check(editor.edit_plan().find_part(worksheet_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "threaded comments replacement after removal should keep worksheet copy-original");
        check(editor.edit_plan().find_part(comments_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "threaded comments replacement after removal should keep legacy comments copy-original");
        check(editor.edit_plan().find_part(persons_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "threaded comments replacement after removal should keep persons copy-original");
        check(editor.edit_plan().find_part(unknown_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "threaded comments replacement after removal should keep unknown copy-original");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
        check(!output_plan.full_calculation_on_load,
            "threaded comments replacement after removal output plan should not request full calculation");
        check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
            "threaded comments replacement after removal output plan should keep calcChain preserve state");
        check(output_plan.relationship_target_audits.empty(),
            "threaded comments replacement after removal output plan should not invent dependency audits");
        check(output_plan.removed_parts.empty(),
            "threaded comments replacement after removal output plan should clear removed parts");
        check(output_plan.removed_package_entries.empty(),
            "threaded comments replacement after removal output plan should clear removed package entries");
        check_output_entry_plan(output_plan.entries,
            "xl/threadedComments/threadedComment1.xml",
            fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
            "threaded comments replacement after removal output plan should rewrite threaded comments");
        check_output_entry_part_context(output_plan.entries,
            "xl/threadedComments/threadedComment1.xml", true, threaded_comments_part.value(),
            "threaded comments replacement after removal output plan should classify rewritten threaded comments");
        const auto* output_threaded_plan = find_output_entry_plan(
            output_plan.entries, "xl/threadedComments/threadedComment1.xml");
        check(output_threaded_plan->reason.find("after removal") != std::string::npos,
            "threaded comments replacement after removal output plan should keep replacement reason");
        check(find_output_entry_plan(
                  output_plan.entries, "xl/threadedComments/_rels/threadedComment1.xml.rels")
                == nullptr,
            "threaded comments replacement after removal output plan should not invent owner relationships");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "threaded comments replacement after removal output plan should preserve content types");
        check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false,
            "",
            "threaded comments replacement after removal output plan should classify content types as metadata");
        const auto* output_content_types_plan =
            find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
        check(output_content_types_plan->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
            "threaded comments replacement after removal output plan should classify content types metadata");
        check_output_entry_plan(output_plan.entries, "_rels/.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "threaded comments replacement after removal output plan should preserve package relationships");
        check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "threaded comments replacement after removal output plan should preserve workbook");
        check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "threaded comments replacement after removal output plan should preserve workbook relationships");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "threaded comments replacement after removal output plan should preserve worksheet");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "threaded comments replacement after removal output plan should preserve inbound worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/comments/comment1.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "threaded comments replacement after removal output plan should preserve legacy comments");
        check_output_entry_plan(output_plan.entries, "xl/persons/person.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "threaded comments replacement after removal output plan should preserve persons");
        check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "threaded comments replacement after removal output plan should preserve unknown entry");

        editor.save_as(output);

        const auto entries = fastxlsx::test::read_zip_entries(output);
        check(entries.find("xl/threadedComments/threadedComment1.xml") != entries.end(),
            "threaded comments replacement after removal output should restore threaded comments");
        check(entries.find("xl/threadedComments/_rels/threadedComment1.xml.rels")
                == entries.end(),
            "threaded comments replacement after removal output should not invent owner relationships");

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(output);
        check_preserved_source_entries(
            editor.reader(), output_reader, threaded_comments_part.zip_path());
        check(output_reader.read_entry("xl/threadedComments/threadedComment1.xml")
                == restored_threaded_comments,
            "threaded comments replacement after removal should write restored bytes");
        check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
            "threaded comments replacement after removal should restore source content types bytes");
        check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == source.worksheet_relationships,
            "threaded comments replacement after removal should preserve worksheet relationships");
        check(output_reader.read_entry("xl/comments/comment1.xml") == source.comments,
            "threaded comments replacement after removal should preserve legacy comments");
        check(output_reader.read_entry("xl/persons/person.xml") == source.persons,
            "threaded comments replacement after removal should preserve persons");
        check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
            "threaded comments replacement after removal should preserve unknown bytes");

        const auto* worksheet_relationships =
            output_reader.relationships_for(worksheet_part);
        check(worksheet_relationships != nullptr,
            "threaded comments replacement after removal should keep worksheet relationships");
        check(worksheet_relationships->find_by_id("rIdThreaded") != nullptr,
            "threaded comments replacement after removal should keep threaded inbound relationship");
        const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
        check(workbook_relationships != nullptr,
            "threaded comments replacement after removal should keep workbook relationships");
        check(workbook_relationships->find_by_id("rIdPerson") != nullptr,
            "threaded comments replacement after removal should keep persons relationship");
        check(output_reader.relationships_for(threaded_comments_part) == nullptr,
            "threaded comments replacement after removal should not create owner relationships");
        check(output_reader.content_types().override_for(threaded_comments_part) != nullptr,
            "threaded comments replacement after removal should restore threaded content type");
        check(output_reader.content_types().override_for(persons_part) != nullptr,
            "threaded comments replacement after removal should keep persons content type");
    }

    {
        const ThreadedCommentsSourcePackage source =
            write_threaded_comments_source_package(
                "fastxlsx-package-editor-remove-after-replace-threaded-comments-source.xlsx");
        const std::filesystem::path output =
            output_path("fastxlsx-package-editor-remove-after-replace-threaded-comments-output.xlsx");

        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);

        const std::string stale_threaded_comments =
            R"(<ThreadedComments xmlns="http://schemas.microsoft.com/office/spreadsheetml/2018/threadedcomments">)"
            R"(<threadedComment ref="A1" id="{bbbbbbbb-1111-2222-3333-aaaaaaaaaaaa}" personId="{22222222-2222-2222-2222-222222222222}" dT="2026-06-08T03:00:00Z">)"
            R"(<text>Stale threaded comment</text>)"
            R"(</threadedComment>)"
            R"(</ThreadedComments>)";
        replace_part_with_memory_chunks(editor, threaded_comments_part, stale_threaded_comments,
            "stale threaded comments replacement before removal");
        check(editor.edit_plan().find_part(threaded_comments_part) != nullptr,
            "setup should record active threaded comments replacement before removal");
        check(editor.edit_plan().find_package_entry(
                  "xl/threadedComments/_rels/threadedComment1.xml.rels")
                == nullptr,
            "setup threaded comments replacement should not invent owner audit");

        editor.remove_part(threaded_comments_part,
            "explicit threaded comments removal after replacement");

        check(editor.edit_plan().find_part(threaded_comments_part) == nullptr,
            "threaded comments removal after replacement should clear active replacement");
        const auto* removed_threaded =
            editor.edit_plan().find_removed_part(threaded_comments_part);
        check(removed_threaded != nullptr,
            "threaded comments removal after replacement should record removed-part audit");
        check(removed_threaded->reason.find("after replacement") != std::string::npos,
            "threaded comments removal after replacement should keep final reason");
        check(removed_threaded->inbound_relationships.size() == 1,
            "threaded comments removal after replacement should keep inbound audit");
        check(removed_threaded->inbound_relationships.front().target_part
                == threaded_comments_part,
            "threaded comments removal after replacement should keep inbound target part");
        check(editor.manifest().find_part(threaded_comments_part) == nullptr,
            "threaded comments removal after replacement should remove manifest part");
        check(editor.manifest().content_types().override_for(threaded_comments_part) == nullptr,
            "threaded comments removal after replacement should remove content type override");
        const auto* content_types_entry =
            editor.edit_plan().find_package_entry("[Content_Types].xml");
        check(content_types_entry != nullptr,
            "threaded comments removal after replacement should record content types rewrite");
        check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
            "threaded comments removal after replacement content types should be local-DOM");
        check(editor.edit_plan().find_removed_package_entry(
                  "xl/threadedComments/_rels/threadedComment1.xml.rels")
                == nullptr,
            "threaded comments removal after replacement should not invent owner omission");
        check(editor.edit_plan().find_package_entry(
                  "xl/threadedComments/_rels/threadedComment1.xml.rels")
                == nullptr,
            "threaded comments removal after replacement should clear owner audit");
        check(editor.edit_plan().find_part(workbook_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "threaded comments removal after replacement should keep workbook copy-original");
        check(editor.edit_plan().find_part(worksheet_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "threaded comments removal after replacement should keep worksheet copy-original");
        check(editor.edit_plan().find_part(comments_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "threaded comments removal after replacement should keep legacy comments");
        check(editor.edit_plan().find_part(persons_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "threaded comments removal after replacement should keep persons");
        check(editor.edit_plan().find_part(unknown_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "threaded comments removal after replacement should keep unknown copy-original");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
        check(!output_plan.full_calculation_on_load,
            "threaded comments removal after replacement output plan should not request full calculation");
        check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
            "threaded comments removal after replacement output plan should keep calcChain preserve state");
        check(output_plan.relationship_target_audits.empty(),
            "threaded comments removal after replacement output plan should not invent dependency audits");
        check(output_plan.removed_parts.size() == 1,
            "threaded comments removal after replacement output plan should expose one removed part");
        check(output_plan.removed_parts.front().part_name == threaded_comments_part,
            "threaded comments removal after replacement output plan should expose removed threaded comments part");
        check(output_plan.removed_parts.front().reason.find("after replacement")
                != std::string::npos,
            "threaded comments removal after replacement output plan should keep removed-part reason");
        check(output_plan.removed_parts.front().inbound_relationships.size() == 1,
            "threaded comments removal after replacement output plan should keep removed-part inbound audit");
        check(output_plan.removed_package_entries.empty(),
            "threaded comments removal after replacement output plan should not omit metadata entries");
        check_output_entry_plan(output_plan.entries,
            "xl/threadedComments/threadedComment1.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
            "threaded comments removal after replacement output plan should omit threaded comments");
        check_output_entry_part_context(output_plan.entries,
            "xl/threadedComments/threadedComment1.xml", true,
            threaded_comments_part.value(),
            "threaded comments removal after replacement output plan should classify omitted threaded comments");
        const auto* output_threaded_plan =
            find_output_entry_plan(output_plan.entries,
                "xl/threadedComments/threadedComment1.xml");
        check(output_threaded_plan->reason.find("after replacement") != std::string::npos,
            "threaded comments removal after replacement output plan should keep final removal reason");
        check(output_threaded_plan->inbound_relationships.size() == 1,
            "threaded comments removal after replacement output plan should expose inbound audit");
        check_output_entry_has_inbound_relationship(output_plan.entries,
            "xl/threadedComments/threadedComment1.xml", worksheet_part.value(),
            "xl/worksheets/_rels/sheet1.xml.rels", "rIdThreaded",
            "http://schemas.microsoft.com/office/2017/10/relationships/threadedComment",
            "../threadedComments/threadedComment1.xml", threaded_comments_part,
            "threaded comments removal after replacement output plan should keep worksheet inbound audit");
        check(find_output_entry_plan(output_plan.entries,
                  "xl/threadedComments/_rels/threadedComment1.xml.rels")
                == nullptr,
            "threaded comments removal after replacement output plan should not invent owner relationships");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
            "threaded comments removal after replacement output plan should rewrite content types");
        check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false,
            "",
            "threaded comments removal after replacement output plan should classify content types as metadata");
        const auto* output_content_types_plan =
            find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
        check(output_content_types_plan->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
            "threaded comments removal after replacement output plan should classify content types metadata");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "threaded comments removal after replacement output plan should preserve inbound worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "threaded comments removal after replacement output plan should preserve persons workbook relationship");
        check_output_entry_plan(output_plan.entries, "xl/persons/person.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "threaded comments removal after replacement output plan should preserve persons part");

        editor.save_as(output);

        const auto entries = fastxlsx::test::read_zip_entries(output);
        check(entries.find("xl/threadedComments/threadedComment1.xml") == entries.end(),
            "threaded comments removal after replacement output should omit threaded comments");
        check(entries.find("xl/threadedComments/_rels/threadedComment1.xml.rels")
                == entries.end(),
            "threaded comments removal after replacement output should not invent owner relationships");

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(output);
        check(output_reader.content_types().override_for(threaded_comments_part) == nullptr,
            "threaded comments removal after replacement should remove threaded content type");
        check(output_reader.content_types().override_for(persons_part) != nullptr,
            "threaded comments removal after replacement should keep persons content type");
        const std::string output_content_types =
            output_reader.read_entry("[Content_Types].xml");
        check_not_contains(output_content_types, "/xl/threadedComments/threadedComment1.xml",
            "threaded comments removal after replacement content types should omit threaded override");
        check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == source.worksheet_relationships,
            "threaded comments removal after replacement should preserve worksheet relationships");
        check(output_reader.read_entry("xl/comments/comment1.xml") == source.comments,
            "threaded comments removal after replacement should preserve legacy comments");
        check(output_reader.read_entry("xl/persons/person.xml") == source.persons,
            "threaded comments removal after replacement should preserve persons");
        check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
            "threaded comments removal after replacement should preserve unknown bytes");

        const auto* worksheet_relationships =
            output_reader.relationships_for(worksheet_part);
        check(worksheet_relationships != nullptr,
            "threaded comments removal after replacement should keep worksheet relationships");
        check(worksheet_relationships->find_by_id("rIdThreaded") != nullptr,
            "threaded comments removal after replacement should keep inbound relationship");
        const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
        check(workbook_relationships != nullptr,
            "threaded comments removal after replacement should keep workbook relationships");
        check(workbook_relationships->find_by_id("rIdPerson") != nullptr,
            "threaded comments removal after replacement should keep persons relationship");
        check(output_reader.relationships_for(threaded_comments_part) == nullptr,
            "threaded comments removal after replacement should not keep owner relationships");
    }
}

void test_package_editor_persons_same_path_ordering()
{
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName comments_part("/xl/comments/comment1.xml");
    const fastxlsx::detail::PartName threaded_comments_part(
        "/xl/threadedComments/threadedComment1.xml");
    const fastxlsx::detail::PartName persons_part("/xl/persons/person.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    {
        const ThreadedCommentsSourcePackage source =
            write_threaded_comments_source_package(
                "fastxlsx-package-editor-replace-after-remove-persons-source.xlsx");
        const std::filesystem::path output =
            output_path("fastxlsx-package-editor-replace-after-remove-persons-output.xlsx");

        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);

        editor.remove_part(persons_part, "temporary persons removal");
        check(editor.edit_plan().find_removed_part(persons_part) != nullptr,
            "setup should record removed persons before replacement restore");
        check(editor.edit_plan().find_removed_package_entry("xl/persons/_rels/person.xml.rels")
                == nullptr,
            "setup should not invent persons owner relationships omission");
        check(editor.manifest().find_part(persons_part) == nullptr,
            "setup should remove persons from manifest before replacement restore");

        const std::string restored_persons =
            R"(<personList xmlns="http://schemas.microsoft.com/office/spreadsheetml/2018/threadedcomments">)"
            R"(<person displayName="Restored Person" id="{22222222-2222-2222-2222-222222222222}" providerId="None" userId="restored@example.invalid"/>)"
            R"(</personList>)";
        replace_part_with_memory_chunks(editor, persons_part, restored_persons,
            "restored persons after removal");

        check(editor.edit_plan().find_removed_part(persons_part) == nullptr,
            "persons replacement after removal should clear stale removed-part audit");
        const auto* persons_plan = editor.edit_plan().find_part(persons_part);
        check(persons_plan != nullptr,
            "persons replacement after removal should restore active edit-plan part");
        check(persons_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
            "persons replacement after removal should keep final write mode");
        check(persons_plan->reason.find("after removal") != std::string::npos,
            "persons replacement after removal should keep final replacement reason");
        check_manifest_write_mode(editor, persons_part,
            fastxlsx::detail::PartWriteMode::StreamRewrite,
            "persons replacement after removal should restore manifest write mode");
        check(editor.manifest().content_types().override_for(persons_part) != nullptr,
            "persons replacement after removal should restore content type override");
        const auto* content_types_entry =
            editor.edit_plan().find_package_entry("[Content_Types].xml");
        check(content_types_entry != nullptr,
            "persons replacement after removal should keep content types audit");
        check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "persons replacement after removal should restore source content types audit");
        check(editor.edit_plan().find_removed_package_entry("xl/persons/_rels/person.xml.rels")
                == nullptr,
            "persons replacement after removal should not invent owner omission");
        check(editor.edit_plan().find_package_entry("xl/persons/_rels/person.xml.rels")
                == nullptr,
            "persons replacement after removal should not invent owner audit");
        check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels") == nullptr,
            "persons replacement after removal should not rewrite inbound workbook relationships");
        check(editor.edit_plan().find_part(workbook_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "persons replacement after removal should keep workbook copy-original");
        check(editor.edit_plan().find_part(worksheet_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "persons replacement after removal should keep worksheet copy-original");
        check(editor.edit_plan().find_part(comments_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "persons replacement after removal should keep legacy comments copy-original");
        check(editor.edit_plan().find_part(threaded_comments_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "persons replacement after removal should keep threaded comments copy-original");
        check(editor.edit_plan().find_part(unknown_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "persons replacement after removal should keep unknown copy-original");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
        check(!output_plan.full_calculation_on_load,
            "persons replacement after removal output plan should not request full calculation");
        check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
            "persons replacement after removal output plan should keep calcChain preserve state");
        check(output_plan.relationship_target_audits.empty(),
            "persons replacement after removal output plan should not invent dependency audits");
        check(output_plan.removed_parts.empty(),
            "persons replacement after removal output plan should clear removed parts");
        check(output_plan.removed_package_entries.empty(),
            "persons replacement after removal output plan should clear removed package entries");
        check_output_entry_plan(output_plan.entries, "xl/persons/person.xml",
            fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
            "persons replacement after removal output plan should rewrite persons");
        check_output_entry_part_context(output_plan.entries, "xl/persons/person.xml",
            true, persons_part.value(),
            "persons replacement after removal output plan should classify rewritten persons");
        const auto* output_persons_plan =
            find_output_entry_plan(output_plan.entries, "xl/persons/person.xml");
        check(output_persons_plan->reason.find("after removal") != std::string::npos,
            "persons replacement after removal output plan should keep replacement reason");
        check(find_output_entry_plan(output_plan.entries, "xl/persons/_rels/person.xml.rels")
                == nullptr,
            "persons replacement after removal output plan should not invent owner relationships");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "persons replacement after removal output plan should preserve content types");
        check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false,
            "",
            "persons replacement after removal output plan should classify content types as metadata");
        const auto* output_content_types_plan =
            find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
        check(output_content_types_plan->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
            "persons replacement after removal output plan should classify content types metadata");
        check_output_entry_plan(output_plan.entries, "_rels/.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "persons replacement after removal output plan should preserve package relationships");
        check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "persons replacement after removal output plan should preserve workbook");
        check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "persons replacement after removal output plan should preserve workbook relationships");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "persons replacement after removal output plan should preserve worksheet");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "persons replacement after removal output plan should preserve worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/comments/comment1.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "persons replacement after removal output plan should preserve legacy comments");
        check_output_entry_plan(output_plan.entries,
            "xl/threadedComments/threadedComment1.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "persons replacement after removal output plan should preserve threaded comments");
        check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "persons replacement after removal output plan should preserve unknown entry");

        editor.save_as(output);

        const auto entries = fastxlsx::test::read_zip_entries(output);
        check(entries.find("xl/persons/person.xml") != entries.end(),
            "persons replacement after removal output should restore persons");
        check(entries.find("xl/persons/_rels/person.xml.rels") == entries.end(),
            "persons replacement after removal output should not invent owner relationships");

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(output);
        check_preserved_source_entries(editor.reader(), output_reader, persons_part.zip_path());
        check(output_reader.read_entry("xl/persons/person.xml") == restored_persons,
            "persons replacement after removal should write restored persons bytes");
        check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
            "persons replacement after removal should restore source content types bytes");
        check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
                == source.workbook_relationships,
            "persons replacement after removal should preserve workbook relationships");
        check(output_reader.read_entry("xl/threadedComments/threadedComment1.xml")
                == source.threaded_comments,
            "persons replacement after removal should preserve threaded comments");
        check(output_reader.read_entry("xl/comments/comment1.xml") == source.comments,
            "persons replacement after removal should preserve legacy comments");
        check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
            "persons replacement after removal should preserve unknown bytes");

        const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
        check(workbook_relationships != nullptr,
            "persons replacement after removal should keep workbook relationships");
        check(workbook_relationships->find_by_id("rIdPerson") != nullptr,
            "persons replacement after removal should keep persons inbound relationship");
        const auto* worksheet_relationships =
            output_reader.relationships_for(worksheet_part);
        check(worksheet_relationships != nullptr,
            "persons replacement after removal should keep worksheet relationships");
        check(worksheet_relationships->find_by_id("rIdThreaded") != nullptr,
            "persons replacement after removal should keep threaded relationship");
        check(output_reader.relationships_for(persons_part) == nullptr,
            "persons replacement after removal should not create owner relationships");
        check(output_reader.content_types().override_for(persons_part) != nullptr,
            "persons replacement after removal should restore persons content type");
        check(output_reader.content_types().override_for(threaded_comments_part) != nullptr,
            "persons replacement after removal should keep threaded comments content type");
    }

    {
        const ThreadedCommentsSourcePackage source =
            write_threaded_comments_source_package(
                "fastxlsx-package-editor-remove-after-replace-persons-source.xlsx");
        const std::filesystem::path output =
            output_path("fastxlsx-package-editor-remove-after-replace-persons-output.xlsx");

        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);

        const std::string stale_persons =
            R"(<personList xmlns="http://schemas.microsoft.com/office/spreadsheetml/2018/threadedcomments">)"
            R"(<person displayName="Stale Person" id="{22222222-2222-2222-2222-222222222222}" providerId="None" userId="stale@example.invalid"/>)"
            R"(</personList>)";
        replace_part_with_memory_chunks(editor, persons_part, stale_persons,
            "stale persons replacement before removal");
        check(editor.edit_plan().find_part(persons_part) != nullptr,
            "setup should record active persons replacement before removal");
        check(editor.edit_plan().find_package_entry("xl/persons/_rels/person.xml.rels")
                == nullptr,
            "setup persons replacement should not invent owner audit");

        editor.remove_part(persons_part, "explicit persons removal after replacement");

        check(editor.edit_plan().find_part(persons_part) == nullptr,
            "persons removal after replacement should clear active replacement");
        const auto* removed_persons = editor.edit_plan().find_removed_part(persons_part);
        check(removed_persons != nullptr,
            "persons removal after replacement should record removed-part audit");
        check(removed_persons->reason.find("after replacement") != std::string::npos,
            "persons removal after replacement should keep final reason");
        check(removed_persons->inbound_relationships.size() == 1,
            "persons removal after replacement should keep inbound audit");
        check(removed_persons->inbound_relationships.front().target_part == persons_part,
            "persons removal after replacement should keep inbound target part");
        check(editor.manifest().find_part(persons_part) == nullptr,
            "persons removal after replacement should remove manifest part");
        check(editor.manifest().content_types().override_for(persons_part) == nullptr,
            "persons removal after replacement should remove content type override");
        const auto* content_types_entry =
            editor.edit_plan().find_package_entry("[Content_Types].xml");
        check(content_types_entry != nullptr,
            "persons removal after replacement should record content types rewrite");
        check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
            "persons removal after replacement content types should be local-DOM");
        check(editor.edit_plan().find_removed_package_entry("xl/persons/_rels/person.xml.rels")
                == nullptr,
            "persons removal after replacement should not invent owner omission");
        check(editor.edit_plan().find_package_entry("xl/persons/_rels/person.xml.rels")
                == nullptr,
            "persons removal after replacement should clear owner audit");
        check(editor.edit_plan().find_part(workbook_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "persons removal after replacement should keep workbook copy-original");
        check(editor.edit_plan().find_part(worksheet_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "persons removal after replacement should keep worksheet copy-original");
        check(editor.edit_plan().find_part(comments_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "persons removal after replacement should keep legacy comments");
        check(editor.edit_plan().find_part(threaded_comments_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "persons removal after replacement should keep threaded comments");
        check(editor.edit_plan().find_part(unknown_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "persons removal after replacement should keep unknown copy-original");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
        check(!output_plan.full_calculation_on_load,
            "persons removal after replacement output plan should not request full calculation");
        check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
            "persons removal after replacement output plan should keep calcChain preserve state");
        check(output_plan.relationship_target_audits.empty(),
            "persons removal after replacement output plan should not invent dependency audits");
        check(output_plan.removed_parts.size() == 1,
            "persons removal after replacement output plan should expose one removed part");
        check(output_plan.removed_parts.front().part_name == persons_part,
            "persons removal after replacement output plan should expose removed persons part");
        check(output_plan.removed_parts.front().reason.find("after replacement")
                != std::string::npos,
            "persons removal after replacement output plan should keep removed-part reason");
        check(output_plan.removed_parts.front().inbound_relationships.size() == 1,
            "persons removal after replacement output plan should keep removed-part inbound audit");
        check(output_plan.removed_package_entries.empty(),
            "persons removal after replacement output plan should not omit metadata entries");
        check_output_entry_plan(output_plan.entries, "xl/persons/person.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
            "persons removal after replacement output plan should omit persons part");
        check_output_entry_part_context(output_plan.entries, "xl/persons/person.xml",
            true, persons_part.value(),
            "persons removal after replacement output plan should classify omitted persons");
        const auto* output_persons_plan =
            find_output_entry_plan(output_plan.entries, "xl/persons/person.xml");
        check(output_persons_plan->reason.find("after replacement") != std::string::npos,
            "persons removal after replacement output plan should keep final removal reason");
        check(output_persons_plan->inbound_relationships.size() == 1,
            "persons removal after replacement output plan should expose inbound audit");
        check_output_entry_has_inbound_relationship(output_plan.entries,
            "xl/persons/person.xml", workbook_part.value(),
            "xl/_rels/workbook.xml.rels", "rIdPerson",
            "http://schemas.microsoft.com/office/2017/10/relationships/person",
            "persons/person.xml", persons_part,
            "persons removal after replacement output plan should keep workbook inbound audit");
        check(find_output_entry_plan(output_plan.entries, "xl/persons/_rels/person.xml.rels")
                == nullptr,
            "persons removal after replacement output plan should not invent owner relationships");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
            "persons removal after replacement output plan should rewrite content types");
        check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false,
            "",
            "persons removal after replacement output plan should classify content types as metadata");
        const auto* output_content_types_plan =
            find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
        check(output_content_types_plan->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
            "persons removal after replacement output plan should classify content types metadata");
        check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "persons removal after replacement output plan should preserve inbound workbook relationships");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "persons removal after replacement output plan should preserve worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/threadedComments/threadedComment1.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "persons removal after replacement output plan should preserve threaded comments");

        editor.save_as(output);

        const auto entries = fastxlsx::test::read_zip_entries(output);
        check(entries.find("xl/persons/person.xml") == entries.end(),
            "persons removal after replacement output should omit persons");
        check(entries.find("xl/persons/_rels/person.xml.rels") == entries.end(),
            "persons removal after replacement output should not invent owner relationships");

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(output);
        check(output_reader.content_types().override_for(persons_part) == nullptr,
            "persons removal after replacement should remove persons content type");
        check(output_reader.content_types().override_for(threaded_comments_part) != nullptr,
            "persons removal after replacement should keep threaded comments content type");
        const std::string output_content_types =
            output_reader.read_entry("[Content_Types].xml");
        check_not_contains(output_content_types, "/xl/persons/person.xml",
            "persons removal after replacement content types should omit persons override");
        check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
                == source.workbook_relationships,
            "persons removal after replacement should preserve workbook relationships");
        check(output_reader.read_entry("xl/threadedComments/threadedComment1.xml")
                == source.threaded_comments,
            "persons removal after replacement should preserve threaded comments");
        check(output_reader.read_entry("xl/comments/comment1.xml") == source.comments,
            "persons removal after replacement should preserve legacy comments");
        check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
            "persons removal after replacement should preserve unknown bytes");

        const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
        check(workbook_relationships != nullptr,
            "persons removal after replacement should keep workbook relationships");
        check(workbook_relationships->find_by_id("rIdPerson") != nullptr,
            "persons removal after replacement should keep inbound persons relationship");
        const auto* worksheet_relationships =
            output_reader.relationships_for(worksheet_part);
        check(worksheet_relationships != nullptr,
            "persons removal after replacement should keep worksheet relationships");
        check(worksheet_relationships->find_by_id("rIdThreaded") != nullptr,
            "persons removal after replacement should keep threaded relationship");
        check(output_reader.relationships_for(persons_part) == nullptr,
            "persons removal after replacement should not keep owner relationships");
    }
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor preservation shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "preservation-comments")) {
            test_package_editor_replaces_comments_and_preserves_worksheet_links();
            test_package_editor_repeated_comments_replacement_updates_final_state();
            test_package_editor_removes_comments_and_preserves_worksheet_links();
            test_package_editor_comments_replacement_restores_prior_removal();
            test_package_editor_comments_removal_overrides_prior_replacement();
            test_package_editor_replaces_threaded_comments_and_preserves_person_links();
            test_package_editor_repeated_threaded_comments_replacement_updates_final_state();
            test_package_editor_removes_threaded_comments_and_preserves_person_links();
            test_package_editor_replaces_persons_and_preserves_threaded_comments_links();
            test_package_editor_repeated_persons_replacement_updates_final_state();
            test_package_editor_removes_persons_and_preserves_threaded_comments_links();
            test_package_editor_threaded_comments_same_path_ordering();
            test_package_editor_persons_same_path_ordering();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
