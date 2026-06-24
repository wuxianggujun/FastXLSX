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
    return shard == "all" || shard == "preservation-resources";
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
void test_package_editor_replaces_media_and_preserves_drawing_links()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-linked-media-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-linked-media-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    std::string replacement_media("\x89PNG\r\n\x1A\n", 8);
    replacement_media += "replacement-media-bytes";
    replace_part_with_memory_chunks(
        editor, image_part, replacement_media, "linked fixture media stream rewrite");

    const auto* media_plan = editor.edit_plan().find_part(image_part);
    check(media_plan != nullptr,
        "linked fixture media replacement should be present in the edit plan");
    check(media_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "linked fixture media replacement should be stream-rewrite");
    check_manifest_write_mode(editor, image_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "linked fixture media replacement should mirror write mode into manifest");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture workbook should remain copy-original after media replacement");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture worksheet should remain copy-original after media replacement");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture drawing should remain copy-original after media replacement");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture chart should remain copy-original after media replacement");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture unknown extension should remain copy-original after media replacement");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "linked fixture media output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "linked fixture media output plan should preserve calcChain policy");
    check(output_plan.relationship_target_audits.empty(),
        "linked fixture media output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "linked fixture media output plan should not record removed parts");
    check(output_plan.removed_package_entries.empty(),
        "linked fixture media output plan should not omit metadata entries");
    check_output_entry_plan(output_plan.entries, "xl/media/image1.png",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "linked fixture media output plan should stream-rewrite media");
    check_output_entry_part_context(output_plan.entries, "xl/media/image1.png",
        true, image_part.value(),
        "linked fixture media output plan should classify media as package part");
    const auto* output_media_plan =
        find_output_entry_plan(output_plan.entries, "xl/media/image1.png");
    check(output_media_plan->reason.find("media stream rewrite") != std::string::npos,
        "linked fixture media output plan should keep replacement reason");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture media output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml",
        false, "",
        "linked fixture media output plan should keep content types as metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture media output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture media output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture media output plan should preserve workbook part");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture media output plan should preserve worksheet part");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture media output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/drawings/_rels/drawing1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture media output plan should preserve drawing relationships");
    check_output_entry_part_context(output_plan.entries,
        "xl/drawings/_rels/drawing1.xml.rels", false, "",
        "linked fixture media output plan should classify drawing relationships as metadata");
    const auto* output_drawing_relationships_plan =
        find_output_entry_plan(output_plan.entries, "xl/drawings/_rels/drawing1.xml.rels");
    check(output_drawing_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "linked fixture media output plan should classify drawing relationships metadata");
    check(output_drawing_relationships_plan->owner_part == drawing_part.value(),
        "linked fixture media output plan should keep drawing relationships owner");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture media output plan should preserve drawing part");
    check_output_entry_plan(output_plan.entries, "xl/charts/chart1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture media output plan should preserve chart part");
    check_output_entry_plan(output_plan.entries, "xl/tables/table1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture media output plan should preserve table part");
    check_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawing1.vml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture media output plan should preserve VML drawing");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing space.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture media output plan should preserve percent-decoded drawing");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture media output plan should preserve sharedStrings");
    check_output_entry_plan(output_plan.entries, "xl/_rels/sharedStrings.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture media output plan should preserve sharedStrings relationships");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture media output plan should preserve styles");
    check_output_entry_plan(output_plan.entries, "xl/vbaProject.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture media output plan should preserve VBA");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture media output plan should preserve calcChain");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture media output plan should preserve unknown extension");
    check_output_entry_plan(output_plan.entries,
        "custom/_rels/opaque-extension.bin.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture media output plan should preserve unknown extension relationships");
    check_output_entry_part_context(output_plan.entries,
        "custom/_rels/opaque-extension.bin.rels", false, "",
        "linked fixture media output plan should classify unknown relationships as metadata");
    const auto* output_opaque_relationships_plan = find_output_entry_plan(
        output_plan.entries, "custom/_rels/opaque-extension.bin.rels");
    check(output_opaque_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "linked fixture media output plan should classify unknown relationships metadata");
    check(output_opaque_relationships_plan->owner_part == opaque_extension_part.value(),
        "linked fixture media output plan should keep unknown relationships owner");
    check(find_output_entry_plan(output_plan.entries, "xl/media/_rels/image1.png.rels")
            == nullptr,
        "linked fixture media output plan should not invent media owner relationships");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, image_part.zip_path());
    check(output_reader.read_entry("xl/media/image1.png") == replacement_media,
        "linked fixture media replacement should write replacement media bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "linked fixture media replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "linked fixture media replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "linked fixture media replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "linked fixture media replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "linked fixture media replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "linked fixture media replacement should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "linked fixture media replacement should preserve chart bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "linked fixture media replacement should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "linked fixture media replacement should preserve unknown extension relationships bytes");

    const auto* drawing_relationships = output_reader.relationships_for(drawing_part);
    check(drawing_relationships != nullptr,
        "linked fixture media replacement should keep drawing relationships readable");
    const auto* image_relationship = drawing_relationships->find_by_id("rId1");
    check(image_relationship != nullptr,
        "linked fixture media replacement should keep drawing image relationship id");
    check(image_relationship->target == "../media/image1.png",
        "linked fixture media replacement should keep drawing image relationship target");
    check(image_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "linked fixture media replacement should keep drawing image relationship target mode");
    check(output_reader.content_types().default_for("png") != nullptr,
        "linked fixture media replacement should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "linked fixture media replacement should not promote PNG media to a content type override");
}

void test_package_editor_replaces_table_and_preserves_worksheet_links()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-linked-table-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-linked-table-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string replacement_table =
        R"(<table xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" )"
        R"(id="1" name="Table1" displayName="Table1" ref="A1:B3">)"
        R"(<autoFilter ref="A1:B3"/>)"
        R"(<tableColumns count="2"><tableColumn id="1" name="A"/><tableColumn id="2" name="B"/></tableColumns>)"
        R"(</table>)";
    replace_part_with_memory_chunks(editor, table_part, replacement_table,
        "linked fixture table local-DOM rewrite");

    const auto* table_plan = editor.edit_plan().find_part(table_part);
    check(table_plan != nullptr,
        "linked fixture table replacement should be present in the edit plan");
    check(table_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "linked fixture table replacement should be local-DOM-rewrite");
    check_manifest_write_mode(editor, table_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "linked fixture table replacement should mirror write mode into manifest");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture workbook should remain copy-original after table replacement");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture worksheet should remain copy-original after table replacement");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture drawing should remain copy-original after table replacement");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture chart should remain copy-original after table replacement");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture image should remain copy-original after table replacement");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture unknown extension should remain copy-original after table replacement");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "linked fixture table output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "linked fixture table output plan should preserve calcChain policy");
    check(output_plan.relationship_target_audits.empty(),
        "linked fixture table output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "linked fixture table output plan should not record removed parts");
    check(output_plan.removed_package_entries.empty(),
        "linked fixture table output plan should not omit metadata entries");
    check_output_entry_plan(output_plan.entries, "xl/tables/table1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "linked fixture table output plan should local-DOM-rewrite table");
    check_output_entry_part_context(output_plan.entries, "xl/tables/table1.xml",
        true, table_part.value(),
        "linked fixture table output plan should classify table as package part");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture table output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml",
        false, "",
        "linked fixture table output plan should keep content types as metadata");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture table output plan should preserve worksheet relationships");
    check_output_entry_part_context(output_plan.entries,
        "xl/worksheets/_rels/sheet1.xml.rels", false, "",
        "linked fixture table output plan should classify worksheet relationships as metadata");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture table output plan should preserve worksheet part");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture table output plan should preserve drawing part");
    check_output_entry_plan(output_plan.entries, "xl/media/image1.png",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture table output plan should preserve media part");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture table output plan should preserve unknown extension");
    check(find_output_entry_plan(output_plan.entries, "xl/tables/_rels/table1.xml.rels")
            == nullptr,
        "linked fixture table output plan should not invent table owner relationships");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, table_part.zip_path());
    check(output_reader.read_entry("xl/tables/table1.xml") == replacement_table,
        "linked fixture table replacement should write replacement table XML");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "linked fixture table replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "linked fixture table replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "linked fixture table replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "linked fixture table replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "linked fixture table replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "linked fixture table replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "linked fixture table replacement should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "linked fixture table replacement should preserve media bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "linked fixture table replacement should preserve unknown extension bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "linked fixture table replacement should keep worksheet relationships readable");
    const auto* table_relationship = worksheet_relationships->find_by_id("rId3");
    check(table_relationship != nullptr,
        "linked fixture table replacement should keep worksheet table relationship id");
    check(table_relationship->target == "../tables/table1.xml",
        "linked fixture table replacement should keep worksheet table relationship target");
    check(table_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "linked fixture table replacement should keep worksheet table relationship target mode");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "linked fixture table replacement should keep table content type override");
}

void test_package_editor_replaces_shared_strings_and_preserves_workbook_links()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-linked-sharedstrings-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-linked-sharedstrings-output.xlsx");

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

    const std::string replacement_shared_strings =
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="2" uniqueCount="2">)"
        R"(<si><t>Gamma</t></si><si><t>Delta</t></si>)"
        R"(</sst>)";
    replace_part_with_memory_chunks(editor, shared_strings_part,
        replacement_shared_strings, "linked fixture sharedStrings stream rewrite");

    const auto* shared_strings_plan = editor.edit_plan().find_part(shared_strings_part);
    check(shared_strings_plan != nullptr,
        "linked fixture sharedStrings replacement should be present in the edit plan");
    check(shared_strings_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "linked fixture sharedStrings replacement should be stream-rewrite");
    check_manifest_write_mode(editor, shared_strings_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "linked fixture sharedStrings replacement should mirror write mode into manifest");
    const auto* shared_strings_relationships_audit =
        editor.edit_plan().find_package_entry("xl/_rels/sharedStrings.xml.rels");
    check(shared_strings_relationships_audit != nullptr,
        "linked fixture sharedStrings replacement should audit preserved owner relationships");
    check(shared_strings_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture sharedStrings relationships audit should be copy-original");
    check(shared_strings_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "linked fixture sharedStrings relationships audit should keep source relationship role");
    check(shared_strings_relationships_audit->owner_part == shared_strings_part.value(),
        "linked fixture sharedStrings relationships audit should keep owner part");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture workbook should remain copy-original after sharedStrings replacement");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture worksheet should remain copy-original after sharedStrings replacement");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture drawing should remain copy-original after sharedStrings replacement");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture chart should remain copy-original after sharedStrings replacement");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture image should remain copy-original after sharedStrings replacement");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture table should remain copy-original after sharedStrings replacement");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture styles should remain copy-original after sharedStrings replacement");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture unknown extension should remain copy-original after sharedStrings replacement");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "linked fixture sharedStrings replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "linked fixture sharedStrings replacement output plan should preserve calcChain policy");
    check(output_plan.relationship_target_audits.empty(),
        "linked fixture sharedStrings replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "linked fixture sharedStrings replacement output plan should not remove parts");
    check(output_plan.removed_package_entries.empty(),
        "linked fixture sharedStrings replacement output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "linked fixture sharedStrings replacement output plan should rewrite sharedStrings");
    check_output_entry_part_context(output_plan.entries, "xl/sharedStrings.xml",
        true, shared_strings_part.value(),
        "linked fixture sharedStrings replacement output plan should classify sharedStrings");
    const auto* output_shared_strings_plan =
        find_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml");
    check(output_shared_strings_plan->reason.find("stream rewrite") != std::string::npos,
        "linked fixture sharedStrings replacement output plan should keep replacement reason");
    check_output_entry_plan(output_plan.entries, "xl/_rels/sharedStrings.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture sharedStrings replacement output plan should preserve owner relationships");
    check_output_entry_part_context(output_plan.entries, "xl/_rels/sharedStrings.xml.rels",
        false, "",
        "linked fixture sharedStrings replacement output plan should classify owner relationships as metadata");
    const auto* output_shared_strings_relationships_plan =
        find_output_entry_plan(output_plan.entries, "xl/_rels/sharedStrings.xml.rels");
    check(output_shared_strings_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "linked fixture sharedStrings replacement output plan should keep owner relationship audit role");
    check(output_shared_strings_relationships_plan->owner_part
            == shared_strings_part.value(),
        "linked fixture sharedStrings replacement output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture sharedStrings replacement output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "linked fixture sharedStrings replacement output plan should classify content types as metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture sharedStrings replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture sharedStrings replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture sharedStrings replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture sharedStrings replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture sharedStrings replacement output plan should preserve styles");
    check_output_entry_plan(output_plan.entries, "xl/tables/table1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture sharedStrings replacement output plan should preserve table");
    check_output_entry_plan(output_plan.entries, "xl/media/image1.png",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture sharedStrings replacement output plan should preserve media");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture sharedStrings replacement output plan should preserve unknown extension");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, shared_strings_part.zip_path());
    check(output_reader.read_entry("xl/sharedStrings.xml") == replacement_shared_strings,
        "linked fixture sharedStrings replacement should write replacement XML");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "linked fixture sharedStrings replacement should preserve owner relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "linked fixture sharedStrings replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "linked fixture sharedStrings replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "linked fixture sharedStrings replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "linked fixture sharedStrings replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "linked fixture sharedStrings replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "linked fixture sharedStrings replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "linked fixture sharedStrings replacement should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "linked fixture sharedStrings replacement should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "linked fixture sharedStrings replacement should preserve table bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "linked fixture sharedStrings replacement should preserve styles bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "linked fixture sharedStrings replacement should preserve unknown extension bytes");

    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "linked fixture sharedStrings replacement should keep workbook relationships readable");
    const auto* shared_strings_relationship = workbook_relationships->find_by_id("rId3");
    check(shared_strings_relationship != nullptr,
        "linked fixture sharedStrings replacement should keep workbook sharedStrings relationship id");
    check(shared_strings_relationship->target == "sharedStrings.xml",
        "linked fixture sharedStrings replacement should keep workbook sharedStrings target");
    check(shared_strings_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "linked fixture sharedStrings replacement should keep workbook sharedStrings target mode");
    const auto* shared_strings_relationships =
        output_reader.relationships_for(shared_strings_part);
    check(shared_strings_relationships != nullptr,
        "linked fixture sharedStrings replacement should keep owner relationships readable");
    const auto* shared_external =
        shared_strings_relationships->find_by_id("rIdSharedExternal");
    check(shared_external != nullptr,
        "linked fixture sharedStrings replacement should keep owner relationship id");
    check(shared_external->target == "https://example.invalid/sharedStrings-audit",
        "linked fixture sharedStrings replacement should keep owner relationship target");
    check(shared_external->target_mode == fastxlsx::detail::Relationship::TargetMode::External,
        "linked fixture sharedStrings replacement should keep owner relationship target mode");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "linked fixture sharedStrings replacement should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "linked fixture sharedStrings replacement should keep styles content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "linked fixture sharedStrings replacement should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "linked fixture sharedStrings replacement should not promote PNG media to an override");
}

void test_package_editor_repeated_shared_strings_replacement_updates_final_state()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-linked-sharedstrings-repeat-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-linked-sharedstrings-repeat-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");

    const std::string stale_shared_strings =
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
        R"(<si><t>Stale</t></si>)"
        R"(</sst>)";
    const std::string final_shared_strings =
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="2" uniqueCount="2">)"
        R"(<si><t>Final</t></si><si><t>Shared</t></si>)"
        R"(</sst>)";

    replace_part_with_memory_chunks(editor, shared_strings_part,
        stale_shared_strings, "stale repeated sharedStrings stream rewrite");
    replace_part_with_memory_chunks(editor, shared_strings_part,
        final_shared_strings, "final repeated sharedStrings stream rewrite");

    const auto* shared_strings_plan =
        editor.edit_plan().find_part(shared_strings_part);
    check(shared_strings_plan != nullptr,
        "repeated sharedStrings replacement should keep an active edit-plan part");
    check(shared_strings_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated sharedStrings replacement should keep final stream-rewrite mode");
    check(shared_strings_plan->reason.find("final repeated") != std::string::npos,
        "repeated sharedStrings replacement should keep final reason");
    check(shared_strings_plan->reason.find("stale repeated") == std::string::npos,
        "repeated sharedStrings replacement should drop stale reason");
    check_manifest_write_mode(editor, shared_strings_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated sharedStrings replacement should mirror final write mode into manifest");
    check(editor.manifest().content_types().override_for(shared_strings_part) != nullptr,
        "repeated sharedStrings replacement should keep sharedStrings content type override");
    check(editor.edit_plan().find_removed_part(shared_strings_part) == nullptr,
        "repeated sharedStrings replacement should not leave removed-part audit");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/sharedStrings.xml.rels")
            == nullptr,
        "repeated sharedStrings replacement should not leave owner relationships omission");
    const auto* shared_strings_relationships_audit =
        editor.edit_plan().find_package_entry("xl/_rels/sharedStrings.xml.rels");
    check(shared_strings_relationships_audit != nullptr,
        "repeated sharedStrings replacement should preserve owner relationships audit");
    check(shared_strings_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated sharedStrings replacement should preserve owner relationships bytes");
    check(shared_strings_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "repeated sharedStrings replacement should keep owner relationship audit role");
    check(shared_strings_relationships_audit->owner_part == shared_strings_part.value(),
        "repeated sharedStrings replacement should keep owner relationship context");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "repeated sharedStrings replacement should not rewrite content types audit");
    check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels") == nullptr,
        "repeated sharedStrings replacement should not rewrite workbook relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated sharedStrings replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated sharedStrings replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated sharedStrings replacement should keep styles copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "repeated sharedStrings replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "repeated sharedStrings replacement output plan should preserve calcChain policy");
    check(output_plan.relationship_target_audits.empty(),
        "repeated sharedStrings replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "repeated sharedStrings replacement output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "repeated sharedStrings replacement output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "repeated sharedStrings replacement output plan should rewrite sharedStrings");
    const auto* output_shared_strings_plan =
        find_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml");
    check(output_shared_strings_plan->reason.find("final repeated") != std::string::npos,
        "repeated sharedStrings replacement output plan should keep final reason");
    check(output_shared_strings_plan->reason.find("stale repeated") == std::string::npos,
        "repeated sharedStrings replacement output plan should drop stale reason");
    check_output_entry_plan(output_plan.entries, "xl/_rels/sharedStrings.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated sharedStrings replacement output plan should preserve owner relationships");
    const auto* output_shared_strings_relationships_plan =
        find_output_entry_plan(output_plan.entries, "xl/_rels/sharedStrings.xml.rels");
    check(output_shared_strings_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "repeated sharedStrings replacement output plan should keep owner relationship audit role");
    check(output_shared_strings_relationships_plan->owner_part
            == shared_strings_part.value(),
        "repeated sharedStrings replacement output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated sharedStrings replacement output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated sharedStrings replacement output plan should preserve workbook relationships");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/sharedStrings.xml") == final_shared_strings,
        "repeated sharedStrings replacement should write final sharedStrings XML");
    check(output_reader.read_entry("xl/sharedStrings.xml") != stale_shared_strings,
        "repeated sharedStrings replacement should not write stale sharedStrings XML");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "repeated sharedStrings replacement should preserve owner relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "repeated sharedStrings replacement should preserve content types bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "repeated sharedStrings replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "repeated sharedStrings replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "repeated sharedStrings replacement should preserve worksheet bytes");

    const auto* workbook_relationships =
        output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "repeated sharedStrings replacement should keep workbook relationships readable");
    const auto* shared_strings_relationship =
        workbook_relationships->find_by_id("rId3");
    check(shared_strings_relationship != nullptr,
        "repeated sharedStrings replacement should keep workbook sharedStrings relationship id");
    check(shared_strings_relationship->target == "sharedStrings.xml",
        "repeated sharedStrings replacement should keep workbook sharedStrings target");
    check(shared_strings_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "repeated sharedStrings replacement should keep workbook sharedStrings target mode");
    const auto* shared_strings_relationships =
        output_reader.relationships_for(shared_strings_part);
    check(shared_strings_relationships != nullptr,
        "repeated sharedStrings replacement should keep owner relationships readable");
    const auto* shared_external =
        shared_strings_relationships->find_by_id("rIdSharedExternal");
    check(shared_external != nullptr,
        "repeated sharedStrings replacement should keep owner relationship id");
    check(shared_external->target_mode == fastxlsx::detail::Relationship::TargetMode::External,
        "repeated sharedStrings replacement should keep owner relationship target mode");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "repeated sharedStrings replacement should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "repeated sharedStrings replacement should keep styles content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "repeated sharedStrings replacement should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "repeated sharedStrings replacement should not promote PNG media to an override");
}

void test_package_editor_replaces_styles_and_preserves_workbook_links()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-linked-styles-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-linked-styles-output.xlsx");

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
        "linked fixture styles staged rewrite");

    const auto* styles_plan = editor.edit_plan().find_part(styles_part);
    check(styles_plan != nullptr,
        "linked fixture styles replacement should be present in the edit plan");
    check(styles_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "linked fixture styles replacement should be staged rewrite");
    check_manifest_write_mode(editor, styles_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "linked fixture styles replacement should mirror write mode into manifest");
    check(editor.edit_plan().find_package_entry("xl/_rels/styles.xml.rels") == nullptr,
        "linked fixture styles replacement should not invent styles owner relationships audit");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "linked fixture styles replacement should not rewrite content types audit");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "linked fixture styles replacement should not rewrite package relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture workbook should remain copy-original after styles replacement");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture worksheet should remain copy-original after styles replacement");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture drawing should remain copy-original after styles replacement");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture chart should remain copy-original after styles replacement");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture image should remain copy-original after styles replacement");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture table should remain copy-original after styles replacement");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture sharedStrings should remain copy-original after styles replacement");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture unknown extension should remain copy-original after styles replacement");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "linked fixture styles replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "linked fixture styles replacement output plan should preserve calcChain policy");
    check(output_plan.relationship_target_audits.empty(),
        "linked fixture styles replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "linked fixture styles replacement output plan should not remove parts");
    check(output_plan.removed_package_entries.empty(),
        "linked fixture styles replacement output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "linked fixture styles replacement output plan should rewrite styles");
    check_output_entry_part_context(output_plan.entries, "xl/styles.xml",
        true, styles_part.value(),
        "linked fixture styles replacement output plan should classify styles");
    const auto* output_styles_plan =
        find_output_entry_plan(output_plan.entries, "xl/styles.xml");
    check(output_styles_plan->reason.find("staged rewrite") != std::string::npos,
        "linked fixture styles replacement output plan should keep replacement reason");
    check(find_output_entry_plan(output_plan.entries, "xl/_rels/styles.xml.rels")
            == nullptr,
        "linked fixture styles replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture styles replacement output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "linked fixture styles replacement output plan should classify content types as metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture styles replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture styles replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture styles replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture styles replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture styles replacement output plan should preserve sharedStrings");
    check_output_entry_plan(output_plan.entries, "xl/_rels/sharedStrings.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture styles replacement output plan should preserve sharedStrings relationships");
    check_output_entry_part_context(output_plan.entries, "xl/_rels/sharedStrings.xml.rels",
        false, "",
        "linked fixture styles replacement output plan should classify sharedStrings relationships as metadata");
    check_output_entry_plan(output_plan.entries, "xl/tables/table1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture styles replacement output plan should preserve table");
    check_output_entry_plan(output_plan.entries, "xl/media/image1.png",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture styles replacement output plan should preserve media");
    check_output_entry_plan(output_plan.entries, "xl/vbaProject.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture styles replacement output plan should preserve VBA");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture styles replacement output plan should preserve calcChain");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture styles replacement output plan should preserve unknown extension");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, styles_part.zip_path());
    check(output_reader.read_entry("xl/styles.xml") == replacement_styles,
        "linked fixture styles replacement should write replacement XML");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "linked fixture styles replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "linked fixture styles replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "linked fixture styles replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "linked fixture styles replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "linked fixture styles replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "linked fixture styles replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "linked fixture styles replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "linked fixture styles replacement should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "linked fixture styles replacement should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "linked fixture styles replacement should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "linked fixture styles replacement should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "linked fixture styles replacement should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "linked fixture styles replacement should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "linked fixture styles replacement should preserve VBA bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "linked fixture styles replacement should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "linked fixture styles replacement should preserve unknown extension relationships bytes");

    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "linked fixture styles replacement should keep workbook relationships readable");
    const auto* styles_relationship = workbook_relationships->find_by_id("rId4");
    check(styles_relationship != nullptr,
        "linked fixture styles replacement should keep workbook styles relationship id");
    check(styles_relationship->target == "styles.xml",
        "linked fixture styles replacement should keep workbook styles target");
    check(styles_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "linked fixture styles replacement should keep workbook styles target mode");
    check(workbook_relationships->find_by_id("rId3") != nullptr,
        "linked fixture styles replacement should keep workbook sharedStrings relationship");
    check(workbook_relationships->find_by_id("rId5") != nullptr,
        "linked fixture styles replacement should keep workbook calcChain relationship");
    check(output_reader.relationships_for(styles_part) == nullptr,
        "linked fixture styles replacement should not create styles owner relationships");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "linked fixture styles replacement should keep styles content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "linked fixture styles replacement should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "linked fixture styles replacement should keep table content type override");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "linked fixture styles replacement should keep chart content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "linked fixture styles replacement should keep VBA content type override");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "linked fixture styles replacement should keep calcChain content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "linked fixture styles replacement should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "linked fixture styles replacement should not promote PNG media to an override");
}

void test_package_editor_repeated_styles_replacement_updates_final_state()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-linked-styles-repeat-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-linked-styles-repeat-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");

    const std::string stale_styles =
        R"(<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<fonts count="1"><font><i/></font></fonts>)"
        R"(<fills count="2"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill></fills>)"
        R"(<borders count="1"><border/></borders>)"
        R"(<cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs>)"
        R"(<cellXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0" applyFont="1"/></cellXfs>)"
        R"(</styleSheet>)";
    const std::string final_styles =
        R"(<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<fonts count="1"><font><b/></font></fonts>)"
        R"(<fills count="2"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill></fills>)"
        R"(<borders count="1"><border/></borders>)"
        R"(<cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs>)"
        R"(<cellXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0" applyFont="1"/></cellXfs>)"
        R"(</styleSheet>)";

    replace_part_with_memory_chunks(editor, styles_part, stale_styles,
        "stale repeated styles staged rewrite");
    replace_part_with_memory_chunks(editor, styles_part, final_styles,
        "final repeated styles staged rewrite");

    const auto* styles_plan = editor.edit_plan().find_part(styles_part);
    check(styles_plan != nullptr,
        "repeated styles replacement should keep an active edit-plan part");
    check(styles_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated styles replacement should keep final staged write mode");
    check(styles_plan->reason.find("final repeated") != std::string::npos,
        "repeated styles replacement should keep final reason");
    check(styles_plan->reason.find("stale repeated") == std::string::npos,
        "repeated styles replacement should drop stale reason");
    check_manifest_write_mode(editor, styles_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated styles replacement should mirror final write mode into manifest");
    check(editor.manifest().content_types().override_for(styles_part) != nullptr,
        "repeated styles replacement should keep styles content type override");
    check(editor.edit_plan().find_removed_part(styles_part) == nullptr,
        "repeated styles replacement should not leave removed-part audit");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/styles.xml.rels")
            == nullptr,
        "repeated styles replacement should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/_rels/styles.xml.rels") == nullptr,
        "repeated styles replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "repeated styles replacement should not rewrite content types audit");
    check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels") == nullptr,
        "repeated styles replacement should not rewrite workbook relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated styles replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated styles replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated styles replacement should keep sharedStrings copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "repeated styles replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "repeated styles replacement output plan should preserve calcChain policy");
    check(output_plan.relationship_target_audits.empty(),
        "repeated styles replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "repeated styles replacement output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "repeated styles replacement output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "repeated styles replacement output plan should rewrite styles");
    const auto* output_styles_plan =
        find_output_entry_plan(output_plan.entries, "xl/styles.xml");
    check(output_styles_plan->reason.find("final repeated") != std::string::npos,
        "repeated styles replacement output plan should keep final reason");
    check(output_styles_plan->reason.find("stale repeated") == std::string::npos,
        "repeated styles replacement output plan should drop stale reason");
    check(find_output_entry_plan(output_plan.entries, "xl/_rels/styles.xml.rels")
            == nullptr,
        "repeated styles replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated styles replacement output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated styles replacement output plan should preserve workbook relationships");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/styles.xml") != entries.end(),
        "repeated styles replacement output should keep styles entry");
    check(entries.find("xl/_rels/styles.xml.rels") == entries.end(),
        "repeated styles replacement output should not invent owner relationships entry");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/styles.xml") == final_styles,
        "repeated styles replacement should write final styles XML");
    check(output_reader.read_entry("xl/styles.xml") != stale_styles,
        "repeated styles replacement should not write stale styles XML");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "repeated styles replacement should preserve content types bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "repeated styles replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "repeated styles replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "repeated styles replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "repeated styles replacement should preserve sharedStrings bytes");

    const auto* workbook_relationships =
        output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "repeated styles replacement should keep workbook relationships readable");
    const auto* styles_relationship = workbook_relationships->find_by_id("rId4");
    check(styles_relationship != nullptr,
        "repeated styles replacement should keep workbook styles relationship id");
    check(styles_relationship->target == "styles.xml",
        "repeated styles replacement should keep workbook styles target");
    check(styles_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "repeated styles replacement should keep workbook styles target mode");
    check(output_reader.relationships_for(styles_part) == nullptr,
        "repeated styles replacement should not create styles owner relationships");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "repeated styles replacement should keep styles content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "repeated styles replacement should keep sharedStrings content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "repeated styles replacement should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "repeated styles replacement should not promote PNG media to an override");
}

void test_package_editor_replaces_chart_and_preserves_drawing_links()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-linked-chart-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-linked-chart-output.xlsx");

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
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string replacement_chart =
        R"(<c:chartSpace xmlns:c="http://schemas.openxmlformats.org/drawingml/2006/chart">)"
        R"(<c:chart><c:title><c:tx><c:rich><a:p xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main"/></c:rich></c:tx></c:title></c:chart>)"
        R"(</c:chartSpace>)";
    replace_part_with_memory_chunks(editor, chart_part, replacement_chart,
        "linked fixture chart local-DOM rewrite");

    const auto* chart_plan = editor.edit_plan().find_part(chart_part);
    check(chart_plan != nullptr,
        "linked fixture chart replacement should be present in the edit plan");
    check(chart_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "linked fixture chart replacement should be local-DOM-rewrite");
    check_manifest_write_mode(editor, chart_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "linked fixture chart replacement should mirror write mode into manifest");
    check(editor.edit_plan().find_package_entry("xl/charts/_rels/chart1.xml.rels") == nullptr,
        "linked fixture chart replacement should not invent chart owner relationships audit");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "linked fixture chart replacement should not rewrite content types audit");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "linked fixture chart replacement should not rewrite package relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture workbook should remain copy-original after chart replacement");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture worksheet should remain copy-original after chart replacement");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture drawing should remain copy-original after chart replacement");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture image should remain copy-original after chart replacement");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture table should remain copy-original after chart replacement");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture sharedStrings should remain copy-original after chart replacement");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture styles should remain copy-original after chart replacement");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture unknown extension should remain copy-original after chart replacement");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "linked fixture chart replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "linked fixture chart replacement output plan should preserve calcChain policy");
    check(output_plan.relationship_target_audits.empty(),
        "linked fixture chart replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "linked fixture chart replacement output plan should not remove parts");
    check(output_plan.removed_package_entries.empty(),
        "linked fixture chart replacement output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "xl/charts/chart1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "linked fixture chart replacement output plan should rewrite chart");
    check_output_entry_part_context(output_plan.entries, "xl/charts/chart1.xml",
        true, chart_part.value(),
        "linked fixture chart replacement output plan should classify chart");
    const auto* output_chart_plan =
        find_output_entry_plan(output_plan.entries, "xl/charts/chart1.xml");
    check(output_chart_plan->reason.find("local-DOM rewrite") != std::string::npos,
        "linked fixture chart replacement output plan should keep replacement reason");
    check(find_output_entry_plan(output_plan.entries, "xl/charts/_rels/chart1.xml.rels")
            == nullptr,
        "linked fixture chart replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture chart replacement output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "linked fixture chart replacement output plan should classify content types as metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture chart replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture chart replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture chart replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture chart replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture chart replacement output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture chart replacement output plan should preserve drawing");
    check_output_entry_plan(output_plan.entries, "xl/drawings/_rels/drawing1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture chart replacement output plan should preserve drawing relationships");
    check_output_entry_plan(output_plan.entries, "xl/media/image1.png",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture chart replacement output plan should preserve media");
    check_output_entry_plan(output_plan.entries, "xl/tables/table1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture chart replacement output plan should preserve table");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture chart replacement output plan should preserve sharedStrings");
    check_output_entry_plan(output_plan.entries, "xl/_rels/sharedStrings.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture chart replacement output plan should preserve sharedStrings relationships");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture chart replacement output plan should preserve styles");
    check_output_entry_plan(output_plan.entries, "xl/vbaProject.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture chart replacement output plan should preserve VBA");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture chart replacement output plan should preserve calcChain");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture chart replacement output plan should preserve unknown extension");
    check_output_entry_plan(output_plan.entries,
        "custom/_rels/opaque-extension.bin.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture chart replacement output plan should preserve unknown extension relationships");
    check_output_entry_part_context(output_plan.entries,
        "custom/_rels/opaque-extension.bin.rels", false, "",
        "linked fixture chart replacement output plan should classify unknown relationships as metadata");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, chart_part.zip_path());
    check(output_reader.read_entry("xl/charts/chart1.xml") == replacement_chart,
        "linked fixture chart replacement should write replacement XML");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "linked fixture chart replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "linked fixture chart replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "linked fixture chart replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "linked fixture chart replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "linked fixture chart replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "linked fixture chart replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "linked fixture chart replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "linked fixture chart replacement should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "linked fixture chart replacement should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "linked fixture chart replacement should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "linked fixture chart replacement should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "linked fixture chart replacement should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "linked fixture chart replacement should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "linked fixture chart replacement should preserve VBA bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "linked fixture chart replacement should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "linked fixture chart replacement should preserve unknown extension relationships bytes");

    const auto* drawing_relationships = output_reader.relationships_for(drawing_part);
    check(drawing_relationships != nullptr,
        "linked fixture chart replacement should keep drawing relationships readable");
    const auto* chart_relationship = drawing_relationships->find_by_id("rId2");
    check(chart_relationship != nullptr,
        "linked fixture chart replacement should keep drawing chart relationship id");
    check(chart_relationship->target == "../charts/chart1.xml",
        "linked fixture chart replacement should keep drawing chart target");
    check(chart_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "linked fixture chart replacement should keep drawing chart target mode");
    const auto* chart_fragment_relationship = drawing_relationships->find_by_id("rId4");
    check(chart_fragment_relationship != nullptr,
        "linked fixture chart replacement should keep URI-qualified chart relationship id");
    check(chart_fragment_relationship->target == "../charts/chart1.xml#plotArea",
        "linked fixture chart replacement should keep URI-qualified chart target");
    check(output_reader.relationships_for(chart_part) == nullptr,
        "linked fixture chart replacement should not create chart owner relationships");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "linked fixture chart replacement should keep chart content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "linked fixture chart replacement should keep table content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "linked fixture chart replacement should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "linked fixture chart replacement should keep styles content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "linked fixture chart replacement should keep VBA content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "linked fixture chart replacement should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "linked fixture chart replacement should not promote PNG media to an override");
}

void test_package_editor_replaces_vba_project_and_preserves_workbook_links()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-linked-vba-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-linked-vba-output.xlsx");

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

    const std::string replacement_vba = std::string("VBA\0PROJECT\0replacement", 23);
    replace_part_with_memory_chunks(
        editor, vba_part, replacement_vba, "linked fixture VBA project stream rewrite");

    const auto* vba_plan = editor.edit_plan().find_part(vba_part);
    check(vba_plan != nullptr,
        "linked fixture VBA replacement should be present in the edit plan");
    check(vba_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "linked fixture VBA replacement should be stream-rewrite");
    check_manifest_write_mode(editor, vba_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "linked fixture VBA replacement should mirror write mode into manifest");
    check(editor.edit_plan().find_package_entry("xl/_rels/vbaProject.bin.rels") == nullptr,
        "linked fixture VBA replacement should not invent VBA owner relationships audit");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "linked fixture VBA replacement should not rewrite content types audit");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "linked fixture VBA replacement should not rewrite package relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture workbook should remain copy-original after VBA replacement");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture worksheet should remain copy-original after VBA replacement");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture drawing should remain copy-original after VBA replacement");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture chart should remain copy-original after VBA replacement");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture image should remain copy-original after VBA replacement");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture table should remain copy-original after VBA replacement");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture sharedStrings should remain copy-original after VBA replacement");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture styles should remain copy-original after VBA replacement");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture calcChain should remain copy-original after VBA replacement");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture unknown extension should remain copy-original after VBA replacement");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "linked fixture VBA replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "linked fixture VBA replacement output plan should preserve calcChain policy");
    check(output_plan.relationship_target_audits.empty(),
        "linked fixture VBA replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "linked fixture VBA replacement output plan should not remove parts");
    check(output_plan.removed_package_entries.empty(),
        "linked fixture VBA replacement output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "xl/vbaProject.bin",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "linked fixture VBA replacement output plan should rewrite VBA");
    check_output_entry_part_context(output_plan.entries, "xl/vbaProject.bin",
        true, vba_part.value(),
        "linked fixture VBA replacement output plan should classify VBA");
    const auto* output_vba_plan =
        find_output_entry_plan(output_plan.entries, "xl/vbaProject.bin");
    check(output_vba_plan->reason.find("stream rewrite") != std::string::npos,
        "linked fixture VBA replacement output plan should keep replacement reason");
    check(find_output_entry_plan(output_plan.entries, "xl/_rels/vbaProject.bin.rels")
            == nullptr,
        "linked fixture VBA replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VBA replacement output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "linked fixture VBA replacement output plan should classify content types as metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VBA replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VBA replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VBA replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VBA replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VBA replacement output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VBA replacement output plan should preserve drawing");
    check_output_entry_plan(output_plan.entries, "xl/drawings/_rels/drawing1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VBA replacement output plan should preserve drawing relationships");
    check_output_entry_plan(output_plan.entries, "xl/charts/chart1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VBA replacement output plan should preserve chart");
    check_output_entry_plan(output_plan.entries, "xl/media/image1.png",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VBA replacement output plan should preserve media");
    check_output_entry_plan(output_plan.entries, "xl/tables/table1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VBA replacement output plan should preserve table");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VBA replacement output plan should preserve sharedStrings");
    check_output_entry_plan(output_plan.entries, "xl/_rels/sharedStrings.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VBA replacement output plan should preserve sharedStrings relationships");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VBA replacement output plan should preserve styles");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VBA replacement output plan should preserve calcChain");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VBA replacement output plan should preserve unknown extension");
    check_output_entry_plan(output_plan.entries,
        "custom/_rels/opaque-extension.bin.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VBA replacement output plan should preserve unknown extension relationships");
    check_output_entry_part_context(output_plan.entries,
        "custom/_rels/opaque-extension.bin.rels", false, "",
        "linked fixture VBA replacement output plan should classify unknown relationships as metadata");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, vba_part.zip_path());
    check(output_reader.read_entry("xl/vbaProject.bin") == replacement_vba,
        "linked fixture VBA replacement should write replacement bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "linked fixture VBA replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "linked fixture VBA replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "linked fixture VBA replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "linked fixture VBA replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "linked fixture VBA replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "linked fixture VBA replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "linked fixture VBA replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "linked fixture VBA replacement should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "linked fixture VBA replacement should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "linked fixture VBA replacement should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "linked fixture VBA replacement should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "linked fixture VBA replacement should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "linked fixture VBA replacement should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "linked fixture VBA replacement should preserve styles bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "linked fixture VBA replacement should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "linked fixture VBA replacement should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "linked fixture VBA replacement should preserve unknown extension relationships bytes");

    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "linked fixture VBA replacement should keep workbook relationships readable");
    const auto* vba_relationship = workbook_relationships->find_by_id("rId2");
    check(vba_relationship != nullptr,
        "linked fixture VBA replacement should keep workbook VBA relationship id");
    check(vba_relationship->target == "vbaProject.bin",
        "linked fixture VBA replacement should keep workbook VBA target");
    check(vba_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "linked fixture VBA replacement should keep workbook VBA target mode");
    check(workbook_relationships->find_by_id("rId3") != nullptr,
        "linked fixture VBA replacement should keep workbook sharedStrings relationship");
    check(workbook_relationships->find_by_id("rId4") != nullptr,
        "linked fixture VBA replacement should keep workbook styles relationship");
    check(workbook_relationships->find_by_id("rId5") != nullptr,
        "linked fixture VBA replacement should keep workbook calcChain relationship");
    check(output_reader.relationships_for(vba_part) == nullptr,
        "linked fixture VBA replacement should not create VBA owner relationships");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "linked fixture VBA replacement should keep VBA content type override");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "linked fixture VBA replacement should keep chart content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "linked fixture VBA replacement should keep table content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "linked fixture VBA replacement should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "linked fixture VBA replacement should keep styles content type override");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "linked fixture VBA replacement should keep calcChain content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "linked fixture VBA replacement should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "linked fixture VBA replacement should not promote PNG media to an override");
}

void test_package_editor_repeated_vba_project_replacement_updates_final_state()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-linked-vba-repeat-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-linked-vba-repeat-output.xlsx");

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

    std::string stale_vba = "VBA";
    stale_vba.push_back('\0');
    stale_vba += "PROJECT";
    stale_vba.push_back('\0');
    stale_vba += "stale";
    std::string final_vba = "VBA";
    final_vba.push_back('\0');
    final_vba += "PROJECT";
    final_vba.push_back('\0');
    final_vba += "final";

    replace_part_with_memory_chunks(
        editor, vba_part, stale_vba, "stale repeated VBA project stream rewrite");
    replace_part_with_memory_chunks(
        editor, vba_part, final_vba, "final repeated VBA project stream rewrite");

    const auto* vba_plan = editor.edit_plan().find_part(vba_part);
    check(vba_plan != nullptr,
        "repeated VBA replacement should keep an active edit-plan part");
    check(vba_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated VBA replacement should keep final stream-rewrite mode");
    check(vba_plan->reason.find("final repeated") != std::string::npos,
        "repeated VBA replacement should keep final reason");
    check(vba_plan->reason.find("stale repeated") == std::string::npos,
        "repeated VBA replacement should drop stale reason");
    check_manifest_write_mode(editor, vba_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated VBA replacement should mirror final write mode into manifest");
    check(editor.manifest().content_types().override_for(vba_part) != nullptr,
        "repeated VBA replacement should keep VBA content type override");
    check(editor.edit_plan().find_removed_part(vba_part) == nullptr,
        "repeated VBA replacement should not leave removed-part audit");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/vbaProject.bin.rels")
            == nullptr,
        "repeated VBA replacement should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/_rels/vbaProject.bin.rels") == nullptr,
        "repeated VBA replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "repeated VBA replacement should not rewrite content types audit");
    check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels") == nullptr,
        "repeated VBA replacement should not rewrite workbook relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated VBA replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated VBA replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated VBA replacement should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated VBA replacement should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated VBA replacement should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated VBA replacement should keep table copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated VBA replacement should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated VBA replacement should keep styles copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated VBA replacement should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated VBA replacement should keep unknown extension copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "repeated VBA replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "repeated VBA replacement output plan should preserve calcChain policy");
    check(output_plan.relationship_target_audits.empty(),
        "repeated VBA replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "repeated VBA replacement output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "repeated VBA replacement output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "xl/vbaProject.bin",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "repeated VBA replacement output plan should rewrite VBA");
    const auto* output_vba_plan =
        find_output_entry_plan(output_plan.entries, "xl/vbaProject.bin");
    check(output_vba_plan->reason.find("final repeated") != std::string::npos,
        "repeated VBA replacement output plan should keep final reason");
    check(output_vba_plan->reason.find("stale repeated") == std::string::npos,
        "repeated VBA replacement output plan should drop stale reason");
    check(find_output_entry_plan(output_plan.entries, "xl/_rels/vbaProject.bin.rels")
            == nullptr,
        "repeated VBA replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated VBA replacement output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated VBA replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated VBA replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated VBA replacement output plan should preserve workbook");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/vbaProject.bin") != entries.end(),
        "repeated VBA replacement output should keep VBA project entry");
    check(entries.find("xl/_rels/vbaProject.bin.rels") == entries.end(),
        "repeated VBA replacement output should not invent owner relationships entry");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/vbaProject.bin") == final_vba,
        "repeated VBA replacement should write final VBA bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") != stale_vba,
        "repeated VBA replacement should not write stale VBA bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "repeated VBA replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "repeated VBA replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "repeated VBA replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "repeated VBA replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "repeated VBA replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "repeated VBA replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "repeated VBA replacement should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "repeated VBA replacement should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "repeated VBA replacement should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "repeated VBA replacement should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "repeated VBA replacement should preserve styles bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "repeated VBA replacement should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "repeated VBA replacement should preserve unknown extension bytes");

    const auto* workbook_relationships =
        output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "repeated VBA replacement should keep workbook relationships readable");
    const auto* vba_relationship = workbook_relationships->find_by_id("rId2");
    check(vba_relationship != nullptr,
        "repeated VBA replacement should keep workbook VBA relationship id");
    check(vba_relationship->target == "vbaProject.bin",
        "repeated VBA replacement should keep workbook VBA target");
    check(vba_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "repeated VBA replacement should keep workbook VBA target mode");
    check(output_reader.relationships_for(vba_part) == nullptr,
        "repeated VBA replacement should not create VBA owner relationships");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "repeated VBA replacement should keep VBA content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "repeated VBA replacement should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "repeated VBA replacement should keep styles content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "repeated VBA replacement should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "repeated VBA replacement should not promote PNG media to an override");
}

void test_package_editor_replaces_vml_drawing_and_preserves_worksheet_links()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-linked-vml-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-linked-vml-output.xlsx");

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

    const std::string replacement_vml = R"(<xml><v:shape id="replacement-shape"/></xml>)";
    replace_part_with_memory_chunks(editor, vml_drawing_part, replacement_vml,
        "linked fixture VML drawing local-DOM rewrite");

    const auto* vml_plan = editor.edit_plan().find_part(vml_drawing_part);
    check(vml_plan != nullptr,
        "linked fixture VML replacement should be present in the edit plan");
    check(vml_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "linked fixture VML replacement should be local-DOM-rewrite");
    check_manifest_write_mode(editor, vml_drawing_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "linked fixture VML replacement should mirror write mode into manifest");
    check(editor.edit_plan().find_package_entry("xl/drawings/_rels/vmlDrawing1.vml.rels")
            == nullptr,
        "linked fixture VML replacement should not invent VML owner relationships audit");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "linked fixture VML replacement should not rewrite content types audit");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "linked fixture VML replacement should not rewrite package relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture workbook should remain copy-original after VML replacement");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture worksheet should remain copy-original after VML replacement");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture drawing should remain copy-original after VML replacement");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture chart should remain copy-original after VML replacement");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture image should remain copy-original after VML replacement");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture table should remain copy-original after VML replacement");
    check(editor.edit_plan().find_part(percent_encoded_drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture percent-decoded drawing should remain copy-original after VML replacement");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture sharedStrings should remain copy-original after VML replacement");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture styles should remain copy-original after VML replacement");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture VBA should remain copy-original after VML replacement");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture calcChain should remain copy-original after VML replacement");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture unknown extension should remain copy-original after VML replacement");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "linked fixture VML replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "linked fixture VML replacement output plan should preserve calcChain policy");
    check(output_plan.relationship_target_audits.empty(),
        "linked fixture VML replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "linked fixture VML replacement output plan should not remove parts");
    check(output_plan.removed_package_entries.empty(),
        "linked fixture VML replacement output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawing1.vml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "linked fixture VML replacement output plan should rewrite VML drawing");
    check_output_entry_part_context(output_plan.entries, "xl/drawings/vmlDrawing1.vml",
        true, vml_drawing_part.value(),
        "linked fixture VML replacement output plan should classify VML drawing");
    const auto* output_vml_plan =
        find_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawing1.vml");
    check(output_vml_plan->reason.find("local-DOM rewrite") != std::string::npos,
        "linked fixture VML replacement output plan should keep replacement reason");
    check(find_output_entry_plan(output_plan.entries,
              "xl/drawings/_rels/vmlDrawing1.vml.rels") == nullptr,
        "linked fixture VML replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VML replacement output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "linked fixture VML replacement output plan should classify content types as metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VML replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VML replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VML replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VML replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VML replacement output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VML replacement output plan should preserve drawing");
    check_output_entry_plan(output_plan.entries, "xl/drawings/_rels/drawing1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VML replacement output plan should preserve drawing relationships");
    check_output_entry_plan(output_plan.entries, "xl/charts/chart1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VML replacement output plan should preserve chart");
    check_output_entry_plan(output_plan.entries, "xl/media/image1.png",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VML replacement output plan should preserve media");
    check_output_entry_plan(output_plan.entries, "xl/tables/table1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VML replacement output plan should preserve table");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing space.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VML replacement output plan should preserve percent-decoded drawing");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VML replacement output plan should preserve sharedStrings");
    check_output_entry_plan(output_plan.entries, "xl/_rels/sharedStrings.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VML replacement output plan should preserve sharedStrings relationships");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VML replacement output plan should preserve styles");
    check_output_entry_plan(output_plan.entries, "xl/vbaProject.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VML replacement output plan should preserve VBA");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VML replacement output plan should preserve calcChain");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VML replacement output plan should preserve unknown extension");
    check_output_entry_plan(output_plan.entries,
        "custom/_rels/opaque-extension.bin.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture VML replacement output plan should preserve unknown extension relationships");
    check_output_entry_part_context(output_plan.entries,
        "custom/_rels/opaque-extension.bin.rels", false, "",
        "linked fixture VML replacement output plan should classify unknown relationships as metadata");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, vml_drawing_part.zip_path());
    check(output_reader.read_entry("xl/drawings/vmlDrawing1.vml") == replacement_vml,
        "linked fixture VML replacement should write replacement XML");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "linked fixture VML replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "linked fixture VML replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "linked fixture VML replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "linked fixture VML replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "linked fixture VML replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "linked fixture VML replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "linked fixture VML replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "linked fixture VML replacement should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "linked fixture VML replacement should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "linked fixture VML replacement should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "linked fixture VML replacement should preserve table bytes");
    check(output_reader.read_entry("xl/drawings/drawing space.xml")
            == source.percent_encoded_drawing,
        "linked fixture VML replacement should preserve percent-decoded drawing bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "linked fixture VML replacement should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "linked fixture VML replacement should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "linked fixture VML replacement should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "linked fixture VML replacement should preserve VBA bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "linked fixture VML replacement should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "linked fixture VML replacement should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "linked fixture VML replacement should preserve unknown extension relationships bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "linked fixture VML replacement should keep worksheet relationships readable");
    const auto* vml_relationship = worksheet_relationships->find_by_id("rId7");
    check(vml_relationship != nullptr,
        "linked fixture VML replacement should keep worksheet VML relationship id");
    check(vml_relationship->target == "../drawings/vmlDrawing1.vml#shape1",
        "linked fixture VML replacement should keep worksheet VML URI-qualified target");
    check(vml_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "linked fixture VML replacement should keep worksheet VML target mode");
    check(worksheet_relationships->find_by_id("rId1") != nullptr,
        "linked fixture VML replacement should keep worksheet drawing relationship");
    check(worksheet_relationships->find_by_id("rId3") != nullptr,
        "linked fixture VML replacement should keep worksheet table relationship");
    check(worksheet_relationships->find_by_id("rId8") != nullptr,
        "linked fixture VML replacement should keep percent-encoded drawing relationship");
    check(output_reader.relationships_for(vml_drawing_part) == nullptr,
        "linked fixture VML replacement should not create VML owner relationships");
    check(output_reader.content_types().override_for(vml_drawing_part) != nullptr,
        "linked fixture VML replacement should keep VML content type override");
    check(output_reader.content_types().override_for(percent_encoded_drawing_part) != nullptr,
        "linked fixture VML replacement should keep percent-decoded drawing content type override");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "linked fixture VML replacement should keep chart content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "linked fixture VML replacement should keep table content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "linked fixture VML replacement should keep VBA content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "linked fixture VML replacement should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "linked fixture VML replacement should not promote PNG media to an override");
}

void test_package_editor_replaces_percent_decoded_drawing_and_preserves_encoded_link()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-linked-percent-drawing-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-linked-percent-drawing-output.xlsx");

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
        "linked fixture percent-decoded drawing local-DOM rewrite");

    const auto* percent_encoded_drawing_plan =
        editor.edit_plan().find_part(percent_encoded_drawing_part);
    check(percent_encoded_drawing_plan != nullptr,
        "linked fixture percent-decoded drawing replacement should be present in the edit plan");
    check(percent_encoded_drawing_plan->write_mode
            == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "linked fixture percent-decoded drawing replacement should be local-DOM-rewrite");
    check_manifest_write_mode(editor, percent_encoded_drawing_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "linked fixture percent-decoded drawing replacement should mirror write mode into manifest");
    check(editor.edit_plan().find_package_entry("xl/drawings/_rels/drawing space.xml.rels")
            == nullptr,
        "linked fixture percent-decoded drawing replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "linked fixture percent-decoded drawing replacement should not rewrite content types audit");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "linked fixture percent-decoded drawing replacement should not rewrite package relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture workbook should remain copy-original after percent-decoded drawing replacement");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture worksheet should remain copy-original after percent-decoded drawing replacement");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture drawing should remain copy-original after percent-decoded drawing replacement");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture chart should remain copy-original after percent-decoded drawing replacement");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture image should remain copy-original after percent-decoded drawing replacement");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture table should remain copy-original after percent-decoded drawing replacement");
    check(editor.edit_plan().find_part(vml_drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture VML drawing should remain copy-original after percent-decoded drawing replacement");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture sharedStrings should remain copy-original after percent-decoded drawing replacement");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture styles should remain copy-original after percent-decoded drawing replacement");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture VBA should remain copy-original after percent-decoded drawing replacement");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture calcChain should remain copy-original after percent-decoded drawing replacement");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture unknown extension should remain copy-original after percent-decoded drawing replacement");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "linked fixture percent-decoded drawing replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "linked fixture percent-decoded drawing replacement output plan should preserve calcChain policy");
    check(output_plan.relationship_target_audits.empty(),
        "linked fixture percent-decoded drawing replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "linked fixture percent-decoded drawing replacement output plan should not remove parts");
    check(output_plan.removed_package_entries.empty(),
        "linked fixture percent-decoded drawing replacement output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing space.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "linked fixture percent-decoded drawing replacement output plan should rewrite drawing");
    check_output_entry_part_context(output_plan.entries, "xl/drawings/drawing space.xml",
        true, percent_encoded_drawing_part.value(),
        "linked fixture percent-decoded drawing replacement output plan should classify drawing");
    const auto* output_drawing_plan =
        find_output_entry_plan(output_plan.entries, "xl/drawings/drawing space.xml");
    check(output_drawing_plan->reason.find("local-DOM rewrite") != std::string::npos,
        "linked fixture percent-decoded drawing replacement output plan should keep replacement reason");
    check(find_output_entry_plan(output_plan.entries,
              "xl/drawings/_rels/drawing space.xml.rels") == nullptr,
        "linked fixture percent-decoded drawing replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture percent-decoded drawing replacement output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "linked fixture percent-decoded drawing replacement output plan should classify content types as metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture percent-decoded drawing replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture percent-decoded drawing replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture percent-decoded drawing replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture percent-decoded drawing replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture percent-decoded drawing replacement output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture percent-decoded drawing replacement output plan should preserve drawing");
    check_output_entry_plan(output_plan.entries, "xl/drawings/_rels/drawing1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture percent-decoded drawing replacement output plan should preserve drawing relationships");
    check_output_entry_plan(output_plan.entries, "xl/charts/chart1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture percent-decoded drawing replacement output plan should preserve chart");
    check_output_entry_plan(output_plan.entries, "xl/media/image1.png",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture percent-decoded drawing replacement output plan should preserve media");
    check_output_entry_plan(output_plan.entries, "xl/tables/table1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture percent-decoded drawing replacement output plan should preserve table");
    check_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawing1.vml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture percent-decoded drawing replacement output plan should preserve VML drawing");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture percent-decoded drawing replacement output plan should preserve sharedStrings");
    check_output_entry_plan(output_plan.entries, "xl/_rels/sharedStrings.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture percent-decoded drawing replacement output plan should preserve sharedStrings relationships");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture percent-decoded drawing replacement output plan should preserve styles");
    check_output_entry_plan(output_plan.entries, "xl/vbaProject.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture percent-decoded drawing replacement output plan should preserve VBA");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture percent-decoded drawing replacement output plan should preserve calcChain");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture percent-decoded drawing replacement output plan should preserve unknown extension");
    check_output_entry_plan(output_plan.entries,
        "custom/_rels/opaque-extension.bin.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture percent-decoded drawing replacement output plan should preserve unknown extension relationships");
    check_output_entry_part_context(output_plan.entries,
        "custom/_rels/opaque-extension.bin.rels", false, "",
        "linked fixture percent-decoded drawing replacement output plan should classify unknown relationships as metadata");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(
        editor.reader(), output_reader, percent_encoded_drawing_part.zip_path());
    check(output_reader.read_entry("xl/drawings/drawing space.xml") == replacement_drawing,
        "linked fixture percent-decoded drawing replacement should write replacement XML");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "linked fixture percent-decoded drawing replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "linked fixture percent-decoded drawing replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "linked fixture percent-decoded drawing replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "linked fixture percent-decoded drawing replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "linked fixture percent-decoded drawing replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "linked fixture percent-decoded drawing replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "linked fixture percent-decoded drawing replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "linked fixture percent-decoded drawing replacement should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "linked fixture percent-decoded drawing replacement should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "linked fixture percent-decoded drawing replacement should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "linked fixture percent-decoded drawing replacement should preserve table bytes");
    check(output_reader.read_entry("xl/drawings/vmlDrawing1.vml") == source.vml_drawing,
        "linked fixture percent-decoded drawing replacement should preserve VML bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "linked fixture percent-decoded drawing replacement should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "linked fixture percent-decoded drawing replacement should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "linked fixture percent-decoded drawing replacement should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "linked fixture percent-decoded drawing replacement should preserve VBA bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "linked fixture percent-decoded drawing replacement should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "linked fixture percent-decoded drawing replacement should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "linked fixture percent-decoded drawing replacement should preserve unknown extension relationships bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "linked fixture percent-decoded drawing replacement should keep worksheet relationships readable");
    const auto* percent_encoded_relationship = worksheet_relationships->find_by_id("rId8");
    check(percent_encoded_relationship != nullptr,
        "linked fixture percent-decoded drawing replacement should keep worksheet drawing relationship id");
    check(percent_encoded_relationship->target == "../drawings/drawing%20space.xml",
        "linked fixture percent-decoded drawing replacement should keep percent-encoded target");
    check(percent_encoded_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "linked fixture percent-decoded drawing replacement should keep target mode internal");
    check(worksheet_relationships->find_by_id("rId1") != nullptr,
        "linked fixture percent-decoded drawing replacement should keep worksheet drawing relationship");
    check(worksheet_relationships->find_by_id("rId3") != nullptr,
        "linked fixture percent-decoded drawing replacement should keep worksheet table relationship");
    check(worksheet_relationships->find_by_id("rId7") != nullptr,
        "linked fixture percent-decoded drawing replacement should keep worksheet VML relationship");
    check(output_reader.relationships_for(percent_encoded_drawing_part) == nullptr,
        "linked fixture percent-decoded drawing replacement should not create owner relationships");
    check(output_reader.content_types().override_for(percent_encoded_drawing_part) != nullptr,
        "linked fixture percent-decoded drawing replacement should keep drawing content type override");
    check(output_reader.content_types().override_for(vml_drawing_part) != nullptr,
        "linked fixture percent-decoded drawing replacement should keep VML content type override");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "linked fixture percent-decoded drawing replacement should keep chart content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "linked fixture percent-decoded drawing replacement should keep table content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "linked fixture percent-decoded drawing replacement should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "linked fixture percent-decoded drawing replacement should keep styles content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "linked fixture percent-decoded drawing replacement should keep VBA content type override");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "linked fixture percent-decoded drawing replacement should keep calcChain content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "linked fixture percent-decoded drawing replacement should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "linked fixture percent-decoded drawing replacement should not promote PNG media to an override");
}


} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor preservation shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "preservation-resources")) {
            test_package_editor_replaces_media_and_preserves_drawing_links();
            test_package_editor_replaces_table_and_preserves_worksheet_links();
            test_package_editor_replaces_shared_strings_and_preserves_workbook_links();
            test_package_editor_repeated_shared_strings_replacement_updates_final_state();
            test_package_editor_replaces_styles_and_preserves_workbook_links();
            test_package_editor_repeated_styles_replacement_updates_final_state();
            test_package_editor_replaces_chart_and_preserves_drawing_links();
            test_package_editor_replaces_vba_project_and_preserves_workbook_links();
            test_package_editor_repeated_vba_project_replacement_updates_final_state();
            test_package_editor_replaces_vml_drawing_and_preserves_worksheet_links();
            test_package_editor_replaces_percent_decoded_drawing_and_preserves_encoded_link();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
