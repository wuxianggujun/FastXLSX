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
    return shard == "all" || shard == "c5";
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


} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor shard: " << shard << '\n';

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
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
