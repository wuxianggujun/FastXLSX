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
    return shard == "all" || shard == "sheetdata-catalog";
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
void test_package_editor_rejects_staged_small_xml_replacements_without_state_changes()
{
    SourcePackage source =
        write_source_package("fastxlsx-package-editor-staged-small-xml-source.xlsx");
    const std::string app_properties =
        "<Properties><Application>FastXLSX</Application></Properties>";
    source.content_types = R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/docProps/core.xml" ContentType="application/vnd.openxmlformats-package.core-properties+xml"/>)"
        R"(<Override PartName="/docProps/app.xml" ContentType="application/vnd.openxmlformats-officedocument.extended-properties+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(</Types>)";
    source.package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/>)"
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties" Target="docProps/app.xml"/>)"
        R"(</Relationships>)";
    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"docProps/core.xml", source.core_properties},
            {"docProps/app.xml", app_properties},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"custom/opaque.bin", source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-staged-small-xml-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName app_part("/docProps/app.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

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

    const std::string workbook_prefix =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets>)";
    const std::string workbook_suffix =
        R"(<sheet name="Renamed" sheetId="1" r:id="rId1"/></sheets></workbook>)";

    const auto expect_staged_small_xml_rejection =
        [&](const fastxlsx::detail::PartName& part_name,
            std::vector<fastxlsx::detail::PackageEntryChunk> chunks,
            const char* expected_failure_message) {
            bool failed = false;
            try {
                editor.replace_part_chunks(
                    part_name, std::move(chunks), "staged small-XML chunks");
            } catch (const std::exception& error) {
                failed = true;
                check_contains(error.what(), "staged replacement is not allowed",
                    "staged small-XML replacement failure should explain the rejected staged path");
                check_contains(error.what(), "workbook/core/app small XML",
                    "staged small-XML replacement failure should identify the metadata boundary");
                check_contains(error.what(), "replace_part()",
                    "staged small-XML replacement failure should point to materialized small-XML API");
            }
            check(failed, expected_failure_message);
        };

    expect_staged_small_xml_rejection(workbook_part,
        {
            fastxlsx::detail::PackageEntryChunk::memory(workbook_prefix),
            fastxlsx::detail::PackageEntryChunk::memory(workbook_suffix),
        },
        "PackageEditor should reject staged replacement for workbook small XML");
    expect_staged_small_xml_rejection(core_part,
        {fastxlsx::detail::PackageEntryChunk::memory(
            "<cp:coreProperties><dc:creator>Staged</dc:creator></cp:coreProperties>")},
        "PackageEditor should reject staged replacement for core properties small XML");
    expect_staged_small_xml_rejection(app_part,
        {fastxlsx::detail::PackageEntryChunk::memory(
            "<Properties><Application>Staged</Application></Properties>")},
        "PackageEditor should reject staged replacement for app properties small XML");

    check(editor.edit_plan().size() == initial_plan_size,
        "staged small-XML rejection should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "staged small-XML rejection should not append notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "staged small-XML rejection should not append relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "staged small-XML rejection should not append worksheet relationship audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == initial_worksheet_payload_dependency_audit_count,
        "staged small-XML rejection should not append worksheet payload audits");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == initial_workbook_payload_dependency_audit_count,
        "staged small-XML rejection should not append workbook payload audits");
    check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
        "staged small-XML rejection should preserve package-entry audit count");
    check(editor.edit_plan().removed_package_entries().size()
            == initial_removed_package_entry_count,
        "staged small-XML rejection should preserve removed package-entry audit count");
    check(editor.edit_plan().removed_parts().empty(),
        "staged small-XML rejection should not record removed parts");
    check(!editor.edit_plan().full_calculation_on_load(),
        "staged small-XML rejection should not request recalculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "staged small-XML rejection should not change calcChain policy");

    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "staged small-XML rejection should leave workbook copy-original");
    check_manifest_write_mode(editor, core_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "staged small-XML rejection should leave core properties copy-original");
    check_manifest_write_mode(editor, app_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "staged small-XML rejection should leave app properties copy-original");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "staged small-XML rejection should leave worksheet copy-original");
    check_manifest_write_mode(editor, unknown_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "staged small-XML rejection should leave unknown part copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "staged small-XML rejection output plan should not request recalculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "staged small-XML rejection output plan should preserve calcChain policy");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "staged small-XML rejection output plan should preserve workbook bytes");
    check_output_entry_plan(output_plan.entries, "docProps/core.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "staged small-XML rejection output plan should preserve core properties bytes");
    check_output_entry_plan(output_plan.entries, "docProps/app.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "staged small-XML rejection output plan should preserve app properties bytes");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "staged small-XML rejection output plan should preserve worksheet bytes");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "staged small-XML rejection output plan should preserve unknown bytes");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "staged small-XML rejection output should preserve source workbook bytes");
    check(output_reader.read_entry("docProps/core.xml") == source.core_properties,
        "staged small-XML rejection output should preserve source core properties bytes");
    check(output_reader.read_entry("docProps/app.xml") == app_properties,
        "staged small-XML rejection output should preserve source app properties bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "staged small-XML rejection output should preserve source worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "staged small-XML rejection output should preserve unknown bytes");
}

void test_package_editor_rejects_oversized_workbook_xml_materialization_without_state_changes()
{
    SourcePackage source =
        write_source_package("fastxlsx-package-editor-oversized-workbook-source.xlsx");
    source.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<definedNames><definedName name="Huge">)"
        + std::string(
            fastxlsx::detail::package_editor_workbook_xml_materialization_byte_limit + 1U, 'A')
        + R"(</definedName></definedNames></workbook>)";
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

    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    fastxlsx::detail::PackageEditor replacement_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const std::size_t initial_replacement_plan_size =
        replacement_editor.edit_plan().size();
    bool replacement_failed = false;
    try {
        replacement_editor.replace_part(workbook_part, source.workbook,
            fastxlsx::detail::PartWriteMode::LocalDomRewrite,
            "oversized workbook package-part replacement");
    } catch (const std::exception& error) {
        replacement_failed = true;
        check_contains(error.what(), "materialized workbook XML exceeds small XML limit",
            "oversized workbook replacement should report the small XML boundary");
        check_contains(error.what(), "workbook package-part replacement",
            "oversized workbook replacement should report the materialization purpose");
    }
    check(replacement_failed,
        "PackageEditor should reject oversized materialized workbook replacement");
    check(replacement_editor.edit_plan().size() == initial_replacement_plan_size,
        "oversized workbook replacement failure should not mutate the edit plan");
    check_manifest_write_mode(replacement_editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "oversized workbook replacement failure should leave workbook copy-original");

    fastxlsx::detail::PackageEditor by_name_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const std::size_t initial_by_name_plan_size = by_name_editor.edit_plan().size();
    bool worksheet_source_consumed = false;
    const fastxlsx::detail::WorksheetInputChunkCallback worksheet_source =
        [&](std::string& chunk) {
            worksheet_source_consumed = true;
            chunk = "<worksheet><sheetData/></worksheet>";
            return true;
        };

    bool by_name_failed = false;
    try {
        by_name_editor.replace_worksheet_part_from_chunk_source_by_name(
            "Sheet1", worksheet_source);
    } catch (const std::exception& error) {
        by_name_failed = true;
        check_contains(error.what(), "materialized workbook sheet catalog XML exceeds small XML limit",
            "oversized source workbook catalog should report the small XML boundary");
    }
    check(by_name_failed,
        "PackageEditor should reject oversized source workbook catalog materialization");
    check(!worksheet_source_consumed,
        "oversized source workbook catalog failure should not consume worksheet chunks");
    check(by_name_editor.edit_plan().size() == initial_by_name_plan_size,
        "oversized source workbook catalog failure should not mutate the edit plan");
    check_manifest_write_mode(by_name_editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "oversized source workbook catalog failure should leave workbook copy-original");
    check_manifest_write_mode(by_name_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "oversized source workbook catalog failure should leave worksheet copy-original");
}

void test_package_editor_rejects_shared_strings_materialized_replacement_without_state_changes()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package(
            "fastxlsx-package-editor-shared-strings-materialized-source.xlsx");
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    const std::string replacement_shared_strings =
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><si><t>staged only</t></si></sst>)";

    const std::size_t initial_plan_size = editor.edit_plan().size();
    bool failed = false;
    try {
        editor.replace_part(shared_strings_part, replacement_shared_strings,
            fastxlsx::detail::PartWriteMode::LocalDomRewrite,
            "sharedStrings materialized replacement");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "materialized source package part replacement is not allowed",
            "sharedStrings materialized replacement should report staged-only boundary");
        check_contains(error.what(), "replace_part_chunks",
            "sharedStrings materialized replacement should point to staged chunks");
    }
    check(failed,
        "PackageEditor should reject source sharedStrings materialized replacement");
    check(editor.edit_plan().size() == initial_plan_size,
        "sharedStrings materialized replacement failure should not mutate edit plan");
    check_manifest_write_mode(editor, shared_strings_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings materialized replacement failure should keep sharedStrings copy-original");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings materialized replacement failure should keep workbook copy-original");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings materialized replacement failure should keep worksheet copy-original");
    const fastxlsx::detail::PackageEditorOutputPlan failed_plan = editor.planned_output();
    check_output_entry_plan(failed_plan.entries, shared_strings_part.zip_path(),
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sharedStrings materialized replacement failure should preserve sharedStrings");
    check_output_entry_staged_replacement_chunks(failed_plan.entries,
        shared_strings_part.zip_path(), false,
        "sharedStrings materialized replacement failure should not stage sharedStrings chunks");
    check_output_entry_materialized_replacement(failed_plan.entries,
        shared_strings_part.zip_path(), false,
        "sharedStrings materialized replacement failure should not mark materialized replacement");

    editor.replace_part_chunks(shared_strings_part,
        {fastxlsx::detail::PackageEntryChunk::memory(replacement_shared_strings)},
        "sharedStrings staged chunk replacement");
    const fastxlsx::detail::PackageEditorOutputPlan staged_plan = editor.planned_output();
    check_output_entry_plan(staged_plan.entries, shared_strings_part.zip_path(),
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "sharedStrings staged replacement should appear in planned output");
    check_output_entry_staged_replacement_chunks(staged_plan.entries,
        shared_strings_part.zip_path(), true,
        "sharedStrings staged replacement should expose staged chunks");
    check_output_entry_materialized_replacement(staged_plan.entries,
        shared_strings_part.zip_path(), false,
        "sharedStrings staged replacement should not expose materialized replacement");
}

void test_package_editor_rejects_styles_materialized_replacement_without_state_changes()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package(
            "fastxlsx-package-editor-styles-materialized-source.xlsx");
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    const std::string replacement_styles =
        R"(<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<fonts count="1"><font><b/></font></fonts>)"
        R"(<fills count="2"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill></fills>)"
        R"(<borders count="1"><border/></borders>)"
        R"(<cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs>)"
        R"(<cellXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0" applyFont="1"/></cellXfs>)"
        R"(</styleSheet>)";

    const std::size_t initial_plan_size = editor.edit_plan().size();
    bool failed = false;
    try {
        editor.replace_part(styles_part, replacement_styles,
            fastxlsx::detail::PartWriteMode::LocalDomRewrite,
            "styles materialized replacement");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "materialized source package part replacement is not allowed",
            "styles materialized replacement should report staged-only boundary");
        check_contains(error.what(), "replace_part_chunks",
            "styles materialized replacement should point to staged chunks");
    }
    check(failed,
        "PackageEditor should reject source styles materialized replacement");
    check(editor.edit_plan().size() == initial_plan_size,
        "styles materialized replacement failure should not mutate edit plan");
    check_manifest_write_mode(editor, styles_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles materialized replacement failure should keep styles copy-original");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles materialized replacement failure should keep workbook copy-original");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles materialized replacement failure should keep worksheet copy-original");
    const fastxlsx::detail::PackageEditorOutputPlan failed_plan = editor.planned_output();
    check_output_entry_plan(failed_plan.entries, styles_part.zip_path(),
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "styles materialized replacement failure should preserve styles");
    check_output_entry_staged_replacement_chunks(failed_plan.entries,
        styles_part.zip_path(), false,
        "styles materialized replacement failure should not stage styles chunks");
    check_output_entry_materialized_replacement(failed_plan.entries,
        styles_part.zip_path(), false,
        "styles materialized replacement failure should not mark materialized replacement");

    editor.replace_part_chunks(styles_part,
        {fastxlsx::detail::PackageEntryChunk::memory(replacement_styles)},
        "styles staged chunk replacement");
    const fastxlsx::detail::PackageEditorOutputPlan staged_plan = editor.planned_output();
    check_output_entry_plan(staged_plan.entries, styles_part.zip_path(),
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "styles staged replacement should appear in planned output");
    check_output_entry_staged_replacement_chunks(staged_plan.entries,
        styles_part.zip_path(), true,
        "styles staged replacement should expose staged chunks");
    check_output_entry_materialized_replacement(staged_plan.entries,
        styles_part.zip_path(), false,
        "styles staged replacement should not expose materialized replacement");
}

void test_package_editor_rejects_generic_source_part_materialized_replacement_without_state_changes()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package(
            "fastxlsx-package-editor-generic-source-part-materialized-source.xlsx");
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    const std::string replacement_opaque_payload = "opaque extension materialized payload";

    const std::size_t initial_plan_size = editor.edit_plan().size();
    bool failed = false;
    try {
        editor.replace_part(opaque_extension_part, replacement_opaque_payload,
            fastxlsx::detail::PartWriteMode::LocalDomRewrite,
            "opaque extension materialized replacement");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "materialized source package part replacement is not allowed",
            "generic source part materialized replacement should report staged-only boundary");
        check_contains(error.what(), "replace_part_chunks",
            "generic source part materialized replacement should point to staged chunks");
    }
    check(failed,
        "PackageEditor should reject generic source package part materialized replacement");
    check(editor.edit_plan().size() == initial_plan_size,
        "generic source part materialized replacement failure should not mutate edit plan");
    check_manifest_write_mode(editor, opaque_extension_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "generic source part materialized replacement failure should keep opaque copy-original");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "generic source part materialized replacement failure should keep workbook copy-original");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "generic source part materialized replacement failure should keep worksheet copy-original");
    const fastxlsx::detail::PackageEditorOutputPlan failed_plan = editor.planned_output();
    check_output_entry_plan(failed_plan.entries, opaque_extension_part.zip_path(),
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "generic source part materialized replacement failure should preserve opaque part");
    check_output_entry_staged_replacement_chunks(failed_plan.entries,
        opaque_extension_part.zip_path(), false,
        "generic source part materialized replacement failure should not stage chunks");
    check_output_entry_materialized_replacement(failed_plan.entries,
        opaque_extension_part.zip_path(), false,
        "generic source part materialized replacement failure should not mark materialized replacement");

    editor.replace_part_chunks(opaque_extension_part,
        {fastxlsx::detail::PackageEntryChunk::memory(replacement_opaque_payload)},
        "opaque extension staged chunk replacement");
    const fastxlsx::detail::PackageEditorOutputPlan staged_plan = editor.planned_output();
    check_output_entry_plan(staged_plan.entries, opaque_extension_part.zip_path(),
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "generic source part staged replacement should appear in planned output");
    check_output_entry_staged_replacement_chunks(staged_plan.entries,
        opaque_extension_part.zip_path(), true,
        "generic source part staged replacement should expose staged chunks");
    check_output_entry_materialized_replacement(staged_plan.entries,
        opaque_extension_part.zip_path(), false,
        "generic source part staged replacement should not expose materialized replacement");
}

void test_package_editor_rejects_oversized_metadata_xml_materialization_without_state_changes()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-oversized-metadata-xml-source.xlsx");
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName app_part("/docProps/app.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    const std::string oversized_core_properties =
        "<cp:coreProperties><dc:creator>"
        + std::string(
            fastxlsx::detail::package_editor_metadata_xml_materialization_byte_limit + 1U,
            'M')
        + "</dc:creator></cp:coreProperties>";

    fastxlsx::detail::PackageEditor part_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const std::size_t initial_part_plan_size = part_editor.edit_plan().size();
    const std::size_t initial_part_package_entry_count =
        part_editor.edit_plan().package_entries().size();

    bool part_failed = false;
    try {
        part_editor.replace_part(core_part, oversized_core_properties,
            fastxlsx::detail::PartWriteMode::LocalDomRewrite,
            "oversized core properties materialized replacement");
    } catch (const std::exception& error) {
        part_failed = true;
        check_contains(error.what(), "materialized metadata XML exceeds small XML limit",
            "oversized core properties replacement should report metadata XML limit");
        check_contains(error.what(), "package-part replacement",
            "oversized core properties replacement should report the replacement boundary");
    }
    check(part_failed,
        "PackageEditor should reject oversized materialized metadata part replacement");
    check(part_editor.edit_plan().size() == initial_part_plan_size,
        "oversized metadata part replacement failure should not mutate edit plan");
    check(part_editor.edit_plan().package_entries().size()
            == initial_part_package_entry_count,
        "oversized metadata part replacement failure should not add package-entry audit");
    check_manifest_write_mode(part_editor, core_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "oversized metadata part replacement failure should keep core properties copy-original");
    check_manifest_write_mode(part_editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "oversized metadata part replacement failure should keep workbook copy-original");
    check_manifest_write_mode(part_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "oversized metadata part replacement failure should keep worksheet copy-original");
    const fastxlsx::detail::PackageEditorOutputPlan failed_part_plan =
        part_editor.planned_output();
    check_output_entry_plan(failed_part_plan.entries, core_part.zip_path(),
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "oversized metadata part replacement failure should preserve core properties");
    check_output_entry_materialized_replacement(failed_part_plan.entries,
        core_part.zip_path(), false,
        "oversized metadata part replacement failure should not mark materialized replacement");

    fastxlsx::detail::PackageEditor properties_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const std::size_t initial_properties_plan_size =
        properties_editor.edit_plan().size();
    const std::size_t initial_properties_note_count =
        properties_editor.edit_plan().notes().size();
    const std::size_t initial_properties_package_entry_count =
        properties_editor.edit_plan().package_entries().size();
    const std::size_t initial_properties_removed_part_count =
        properties_editor.edit_plan().removed_parts().size();
    const std::size_t initial_properties_removed_package_entry_count =
        properties_editor.edit_plan().removed_package_entries().size();

    fastxlsx::DocumentProperties properties;
    properties.creator = std::string(
        fastxlsx::detail::package_editor_metadata_xml_materialization_byte_limit + 1U,
        'C');

    bool properties_failed = false;
    try {
        properties_editor.set_document_properties(properties);
    } catch (const std::exception& error) {
        properties_failed = true;
        check_contains(error.what(), "materialized metadata XML exceeds small XML limit",
            "oversized generated document properties should report metadata XML limit");
        check_contains(error.what(), "core document properties replacement",
            "oversized generated document properties should report the generated part boundary");
    }
    check(properties_failed,
        "PackageEditor should reject oversized generated document properties XML");
    check(properties_editor.edit_plan().size() == initial_properties_plan_size,
        "oversized generated metadata failure should not mutate edit plan");
    check(properties_editor.edit_plan().notes().size() == initial_properties_note_count,
        "oversized generated metadata failure should not add notes");
    check(properties_editor.edit_plan().package_entries().size()
            == initial_properties_package_entry_count,
        "oversized generated metadata failure should not add package-entry audit");
    check(properties_editor.edit_plan().removed_parts().size()
            == initial_properties_removed_part_count,
        "oversized generated metadata failure should not record removed parts");
    check(properties_editor.edit_plan().removed_package_entries().size()
            == initial_properties_removed_package_entry_count,
        "oversized generated metadata failure should not record removed package entries");
    check(properties_editor.manifest().find_part(app_part) == nullptr,
        "oversized generated metadata failure should not add missing app properties part");
    check_manifest_write_mode(properties_editor, core_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "oversized generated metadata failure should keep core properties copy-original");
    check_manifest_write_mode(properties_editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "oversized generated metadata failure should keep workbook copy-original");
    check_manifest_write_mode(properties_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "oversized generated metadata failure should keep worksheet copy-original");
    const fastxlsx::detail::PackageEditorOutputPlan failed_properties_plan =
        properties_editor.planned_output();
    check_output_plan_preserves_source_copy_original(properties_editor,
        failed_properties_plan,
        "oversized generated metadata failure should preserve source copy-original output");
}

void test_package_editor_renames_sheet_catalog_entry_preserving_parts()
{
    LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-sheet-catalog-rename-source.xlsx");
    source.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<extLst><ext><sheets><sheet name="Decoy Sheet" sheetId="777" r:id="rId1"/></sheets></ext></extLst>)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<definedNames><definedName name="ReportRange">Sheet1!$A$1:$B$2</definedName></definedNames>)"
        R"(</workbook>)";
    rewrite_linked_object_source_package(source);

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-sheet-catalog-rename-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");
    using WorkbookAuditKind = fastxlsx::detail::WorkbookPayloadDependencyAuditKind;
    using WorkbookAuditScope = fastxlsx::detail::WorkbookPayloadDependencyAuditScope;

    editor.rename_sheet_catalog_entry("Sheet1", "Renamed & Data");

    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "sheet catalog rename should record workbook rewrite in the edit plan");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "sheet catalog rename should use local-DOM rewrite for workbook XML");
    check(workbook_plan->reason.find("sheet catalog") != std::string::npos,
        "sheet catalog rename plan reason should name the catalog rewrite");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheet catalog rename", "not synchronized"}),
        "sheet catalog rename should audit unsynchronized linked references");
    check(has_workbook_payload_audit(editor.edit_plan().workbook_payload_dependency_audits(),
              workbook_part, WorkbookAuditKind::SheetCatalog,
              WorkbookAuditScope::SheetCatalogRename, "sheets/sheet@name",
              {"sheet catalog rename", "sheet name attribute"}),
        "sheet catalog rename should record structured sheet catalog audit");
    check(has_workbook_payload_audit(editor.edit_plan().workbook_payload_dependency_audits(),
              workbook_part, WorkbookAuditKind::DefinedNames,
              WorkbookAuditScope::SheetCatalogRename, "definedNames",
              {"definedNames", "without semantic sync"}),
        "sheet catalog rename should record structured definedNames audit");
    check(!editor.edit_plan().full_calculation_on_load(),
        "sheet catalog rename should not request recalculation by default");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "sheet catalog rename should not change calcChain policy");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "sheet catalog rename should mark workbook local-DOM-rewrite");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sheet catalog rename should leave worksheet copy-original");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sheet catalog rename should leave calcChain copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "sheet catalog rename output plan should rewrite workbook XML");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheet catalog rename output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheet catalog rename output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheet catalog rename output plan should preserve worksheet bytes");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheet catalog rename output plan should preserve unknown extension bytes");
    check(has_workbook_payload_audit(output_plan.workbook_payload_dependency_audits,
              workbook_part, WorkbookAuditKind::SheetCatalog,
              WorkbookAuditScope::SheetCatalogRename, "sheets/sheet@name",
              {"sheet catalog rename", "sheet name attribute"}),
        "sheet catalog rename output plan should snapshot sheet catalog audit");
    check(has_workbook_payload_audit(output_plan.workbook_payload_dependency_audits,
              workbook_part, WorkbookAuditKind::DefinedNames,
              WorkbookAuditScope::SheetCatalogRename, "definedNames",
              {"definedNames", "without semantic sync"}),
        "sheet catalog rename output plan should snapshot definedNames audit");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, workbook_part.zip_path());
    check(output_reader.worksheet_part_by_sheet_name("Renamed & Data") == worksheet_part,
        "sheet catalog rename output should expose the new sheet name");
    bool old_name_failed = false;
    try {
        (void)output_reader.worksheet_part_by_sheet_name("Sheet1");
    } catch (const std::exception&) {
        old_name_failed = true;
    }
    check(old_name_failed,
        "sheet catalog rename output should no longer expose the old sheet name");
    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Renamed &amp; Data")",
        "sheet catalog rename should XML-escape the new sheet name");
    check_contains(workbook_xml,
        R"(<definedNames><definedName name="ReportRange">Sheet1!$A$1:$B$2</definedName></definedNames>)",
        "sheet catalog rename should preserve definedNames without semantic sync");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "sheet catalog rename should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "sheet catalog rename should preserve worksheet bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "sheet catalog rename should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "sheet catalog rename should preserve unknown extension payload");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "sheet catalog rename should preserve unknown owner relationships");
    check(output_reader.content_types().override_for(opaque_extension_part) == nullptr,
        "sheet catalog rename should keep unknown extension default content type");
}

void test_package_editor_sheet_catalog_rename_rewrites_defined_names_opt_in()
{
    LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-sheet-catalog-rename-defined-names-source.xlsx");
    source.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<definedNames>)"
        R"(<definedName name="ReportRange">Sheet1!$A$1:$B$2</definedName>)"
        R"(<definedName name="External">[Book.xlsx]Sheet1!$A$1</definedName>)"
        R"(<definedName name="ThreeD">Sheet1:Other!$A$1</definedName>)"
        R"(</definedNames></workbook>)";
    rewrite_linked_object_source_package(source);

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-sheet-catalog-rename-defined-names-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    using WorkbookAuditKind = fastxlsx::detail::WorkbookPayloadDependencyAuditKind;
    using WorkbookAuditScope = fastxlsx::detail::WorkbookPayloadDependencyAuditScope;

    fastxlsx::detail::SheetCatalogRenameOptions options;
    options.formula_policy =
        fastxlsx::detail::SheetCatalogRenameFormulaPolicy::RewriteDefinedNames;
    editor.rename_sheet_catalog_entry("Sheet1", "Renamed & Data", {}, options);

    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "opt-in definedName rename should record workbook rewrite");
    check(workbook_plan->reason.find("definedName formula references") != std::string::npos,
        "opt-in definedName rename plan reason should name definedName formula rewrite");
    check(has_note_containing(editor.edit_plan().notes(),
              {"definedName", "opt-in narrow policy", "worksheet formulas"}),
        "opt-in definedName rename should audit the narrow formula policy");
    check(has_workbook_payload_audit(editor.edit_plan().workbook_payload_dependency_audits(),
              workbook_part, WorkbookAuditKind::DefinedNames,
              WorkbookAuditScope::SheetCatalogRename, "definedNames",
              {"definedName", "opt-in narrow sync"}),
        "opt-in definedName rename should record structured definedName rewrite audit");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Renamed &amp; Data")",
        "opt-in definedName rename should still XML-escape the sheet catalog name");
    check_contains(workbook_xml,
        R"(<definedName name="ReportRange">'Renamed &amp; Data'!$A$1:$B$2</definedName>)",
        "opt-in definedName rename should rewrite direct local definedName formulas");
    check_contains(workbook_xml,
        R"(<definedName name="External">[Book.xlsx]Sheet1!$A$1</definedName>)",
        "opt-in definedName rename should preserve external workbook references");
    check_contains(workbook_xml,
        R"(<definedName name="ThreeD">Sheet1:Other!$A$1</definedName>)",
        "opt-in definedName rename should preserve 3D sheet-range references");
}

void test_package_editor_sheet_catalog_rename_defined_name_rewrite_failure_is_clean()
{
    LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-sheet-catalog-rename-defined-name-failure-source.xlsx");
    source.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<definedNames><definedName name="Nested"><x>Sheet1!A1</x></definedName></definedNames>)"
        R"(</workbook>)";
    rewrite_linked_object_source_package(source);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_workbook_payload_audit_count =
        editor.edit_plan().workbook_payload_dependency_audits().size();

    fastxlsx::detail::SheetCatalogRenameOptions options;
    options.formula_policy =
        fastxlsx::detail::SheetCatalogRenameFormulaPolicy::RewriteDefinedNames;
    bool failed = false;
    try {
        editor.rename_sheet_catalog_entry("Sheet1", "Renamed", {}, options);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "nested XML in definedName text",
            "definedName rewrite failure should report malformed definedName XML");
    }
    check(failed,
        "opt-in definedName rewrite should reject nested definedName XML before state changes");
    check(editor.edit_plan().size() == initial_plan_size,
        "definedName rewrite failure should preserve edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "definedName rewrite failure should not add notes");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == initial_workbook_payload_audit_count,
        "definedName rewrite failure should not add workbook payload audits");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "definedName rewrite failure should leave workbook copy-original");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "definedName rewrite failure should leave worksheet copy-original");
}

void test_package_editor_sheet_catalog_rename_uses_planned_workbook_xml()
{
    const LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-sheet-catalog-rename-planned-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-sheet-catalog-rename-planned-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    using WorkbookAuditKind = fastxlsx::detail::WorkbookPayloadDependencyAuditKind;
    using WorkbookAuditScope = fastxlsx::detail::WorkbookPayloadDependencyAuditScope;

    const std::string replacement_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Planned" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<calcPr calcId="124519" fullCalcOnLoad="1"/>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, replacement_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "ordinary planned workbook sheet catalog before rename");

    bool old_source_name_failed = false;
    try {
        editor.rename_sheet_catalog_entry("Sheet1", "Final");
    } catch (const std::exception& error) {
        old_source_name_failed = true;
        check_contains(error.what(), "workbook sheet name is not present",
            "planned sheet catalog rename should use planned sheet names");
    }
    check(old_source_name_failed,
        "sheet catalog rename should reject the old source name once planned workbook XML exists");

    editor.rename_sheet_catalog_entry("Planned", "Final");

    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr
            && workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "planned sheet catalog rename should keep workbook local-DOM-rewrite");
    check(workbook_plan->reason.find("sheet catalog") != std::string::npos,
        "planned sheet catalog rename should replace the prior ordinary workbook reason");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "planned sheet catalog rename should leave worksheet copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "planned sheet catalog rename output plan should expose final workbook rewrite");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "planned sheet catalog rename output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "planned sheet catalog rename output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "planned sheet catalog rename output plan should preserve worksheet bytes");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "planned sheet catalog rename output plan should preserve calcChain bytes");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "planned sheet catalog rename output plan should preserve unknown extension bytes");
    check(has_workbook_payload_audit(output_plan.workbook_payload_dependency_audits,
              workbook_part, WorkbookAuditKind::SheetCatalog,
              WorkbookAuditScope::SheetCatalogRename, "sheets/sheet@name",
              {"sheet catalog rename", "sheet name attribute"}),
        "planned sheet catalog rename output plan should snapshot sheet catalog audit");
    check(has_workbook_payload_audit(output_plan.workbook_payload_dependency_audits,
              workbook_part, WorkbookAuditKind::DefinedNames,
              WorkbookAuditScope::SheetCatalogRename, "definedNames",
              {"definedNames", "without semantic sync"}),
        "planned sheet catalog rename output plan should snapshot definedNames audit");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("Final") == worksheet_part,
        "planned sheet catalog rename output should expose the final sheet name");
    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Final")",
        "planned sheet catalog rename should write the final sheet name");
    check_not_contains(workbook_xml, R"(name="Planned")",
        "planned sheet catalog rename should remove the intermediate planned sheet name");
    check_not_contains(workbook_xml, R"(name="Sheet1")",
        "planned sheet catalog rename should not resurrect the source sheet name");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "planned sheet catalog rename should preserve existing workbook calc metadata");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "planned sheet catalog rename should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "planned sheet catalog rename should preserve unknown extension bytes");
}

void test_package_editor_rejects_sheet_catalog_rename_without_state_changes()
{
    LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-sheet-catalog-rename-failure-source.xlsx");
    source.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/>)"
        R"(<sheet name="Summary" sheetId="2" r:id="rId1"/></sheets>)"
        R"(<definedNames><definedName name="ReportRange">Sheet1!$A$1:$B$2</definedName></definedNames>)"
        R"(</workbook>)";
    rewrite_linked_object_source_package(source);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const auto expect_clean_failure =
        [&](auto&& operation, std::string_view expected_message, const char* context) {
            fastxlsx::detail::PackageEditor editor =
                fastxlsx::detail::PackageEditor::open(source.path);
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

            bool failed = false;
            try {
                operation(editor);
            } catch (const std::exception& error) {
                failed = true;
                check_contains(error.what(), expected_message, context);
            }
            check(failed, context);

            check(editor.edit_plan().size() == initial_plan_size,
                "sheet catalog rename failure should preserve edit-plan size");
            check(editor.edit_plan().notes().size() == initial_note_count,
                "sheet catalog rename failure should not append notes");
            check(editor.edit_plan().relationship_target_audits().size()
                    == initial_relationship_target_audit_count,
                "sheet catalog rename failure should not append relationship target audits");
            check(editor.edit_plan().worksheet_relationship_reference_audits().size()
                    == initial_worksheet_relationship_reference_audit_count,
                "sheet catalog rename failure should not append worksheet relationship audits");
            check(editor.edit_plan().worksheet_payload_dependency_audits().size()
                    == initial_worksheet_payload_dependency_audit_count,
                "sheet catalog rename failure should not append worksheet payload audits");
            check(editor.edit_plan().workbook_payload_dependency_audits().size()
                    == initial_workbook_payload_dependency_audit_count,
                "sheet catalog rename failure should not append workbook payload audits");
            check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
                "sheet catalog rename failure should preserve package-entry audit count");
            check(editor.edit_plan().removed_package_entries().size()
                    == initial_removed_package_entry_count,
                "sheet catalog rename failure should preserve removed package-entry audit count");
            check(editor.edit_plan().removed_parts().empty(),
                "sheet catalog rename failure should not record removed parts");
            check(!editor.edit_plan().full_calculation_on_load(),
                "sheet catalog rename failure should not request recalculation");
            check(editor.edit_plan().calc_chain_action()
                    == fastxlsx::detail::CalcChainAction::Preserve,
                "sheet catalog rename failure should not change calcChain policy");
            check_manifest_write_mode(editor, workbook_part,
                fastxlsx::detail::PartWriteMode::CopyOriginal,
                "sheet catalog rename failure should leave workbook copy-original");
            check_manifest_write_mode(editor, worksheet_part,
                fastxlsx::detail::PartWriteMode::CopyOriginal,
                "sheet catalog rename failure should leave worksheet copy-original");
            check_manifest_write_mode(editor, calc_chain_part,
                fastxlsx::detail::PartWriteMode::CopyOriginal,
                "sheet catalog rename failure should leave calcChain copy-original");
        };

    expect_clean_failure(
        [](fastxlsx::detail::PackageEditor& editor) {
            editor.rename_sheet_catalog_entry("Missing", "Renamed");
        },
        "workbook sheet name is not present",
        "PackageEditor should reject sheet catalog rename with missing old name");
    expect_clean_failure(
        [](fastxlsx::detail::PackageEditor& editor) {
            editor.rename_sheet_catalog_entry("Sheet1", "Sheet1");
        },
        "target name already exists",
        "PackageEditor should reject sheet catalog rename to an existing name");
    expect_clean_failure(
        [](fastxlsx::detail::PackageEditor& editor) {
            editor.rename_sheet_catalog_entry("Sheet1", "summary");
        },
        "target name already exists",
        "PackageEditor should reject case-insensitive sheet catalog rename conflicts");
    expect_clean_failure(
        [](fastxlsx::detail::PackageEditor& editor) {
            editor.rename_sheet_catalog_entry("Sheet1", "Bad/Name");
        },
        "invalid characters",
        "PackageEditor should reject invalid sheet catalog rename targets");
    expect_clean_failure(
        [](fastxlsx::detail::PackageEditor& editor) {
            fastxlsx::detail::ReferencePolicy policy;
            policy.unsupported_linked_part_action =
                fastxlsx::detail::ReferencePolicyAction::Fail;
            editor.rename_sheet_catalog_entry("Sheet1", "Renamed", policy);
        },
        "definedNames",
        "PackageEditor should reject sheet catalog rename with definedNames under fail policy");

    const auto expect_invalid_planned_rename_failure =
        [&](std::string_view source_name, std::string_view output_name,
            std::string planned_workbook, auto configure_source,
            std::string_view expected_error, const char* scenario_message) {
            LinkedObjectSourcePackage planned_source =
                write_sheet_data_patch_source_package(source_name);
            configure_source(planned_source);
            rewrite_linked_object_source_package(planned_source);

            fastxlsx::detail::PackageEditor editor =
                fastxlsx::detail::PackageEditor::open(planned_source.path);
            editor.replace_part(workbook_part, planned_workbook,
                fastxlsx::detail::PartWriteMode::LocalDomRewrite,
                "invalid planned workbook catalog before rename");

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
            const std::size_t queued_removed_package_entry_count =
                editor.edit_plan().removed_package_entries().size();

            bool failed = false;
            try {
                editor.rename_sheet_catalog_entry("Broken", "Renamed");
            } catch (const std::exception& error) {
                failed = true;
                check_contains(error.what(), expected_error,
                    "invalid planned catalog rename failure should explain the catalog issue");
            }
            check(failed, scenario_message);

            check(editor.edit_plan().size() == queued_plan_size,
                "invalid planned catalog rename failure should preserve queued edit-plan size");
            check(editor.edit_plan().notes().size() == queued_note_count,
                "invalid planned catalog rename failure should not append notes");
            check(editor.edit_plan().relationship_target_audits().size()
                    == queued_relationship_target_audit_count,
                "invalid planned catalog rename failure should not append relationship audits");
            check(editor.edit_plan().worksheet_relationship_reference_audits().size()
                    == queued_worksheet_relationship_reference_audit_count,
                "invalid planned catalog rename failure should not append worksheet audits");
            check(editor.edit_plan().worksheet_payload_dependency_audits().size()
                    == queued_worksheet_payload_dependency_audit_count,
                "invalid planned catalog rename failure should not append worksheet payload audits");
            check(editor.edit_plan().workbook_payload_dependency_audits().size()
                    == queued_workbook_payload_dependency_audit_count,
                "invalid planned catalog rename failure should not append workbook payload audits");
            check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
                "invalid planned catalog rename failure should preserve package-entry audit count");
            check(editor.edit_plan().removed_package_entries().size()
                    == queued_removed_package_entry_count,
                "invalid planned catalog rename failure should preserve removed package-entry audit count");
            check(editor.edit_plan().removed_parts().empty(),
                "invalid planned catalog rename failure should not record removed parts");
            check(!editor.edit_plan().full_calculation_on_load(),
                "invalid planned catalog rename failure should not request recalculation");
            check(editor.edit_plan().calc_chain_action()
                    == fastxlsx::detail::CalcChainAction::Preserve,
                "invalid planned catalog rename failure should not change calcChain policy");
            const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
            check(workbook_plan != nullptr
                    && workbook_plan->write_mode
                        == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
                "invalid planned catalog rename failure should keep queued workbook replacement");
            check_manifest_write_mode(editor, workbook_part,
                fastxlsx::detail::PartWriteMode::LocalDomRewrite,
                "invalid planned catalog rename failure should keep workbook local-DOM-rewrite");
            check_manifest_write_mode(editor, worksheet_part,
                fastxlsx::detail::PartWriteMode::CopyOriginal,
                "invalid planned catalog rename failure should leave worksheet copy-original");
            check_manifest_write_mode(editor, calc_chain_part,
                fastxlsx::detail::PartWriteMode::CopyOriginal,
                "invalid planned catalog rename failure should leave calcChain copy-original");

            editor.save_as(output_path(output_name));
            const fastxlsx::detail::PackageReader output_reader =
                fastxlsx::detail::PackageReader::open(output_path(output_name));
            check_preserved_source_entries(
                editor.reader(), output_reader, workbook_part.zip_path());
            check(output_reader.read_entry("xl/workbook.xml") == planned_workbook,
                "invalid planned catalog rename output should keep queued workbook replacement");
            check(output_reader.read_entry("xl/worksheets/sheet1.xml")
                    == planned_source.worksheet,
                "invalid planned catalog rename output should preserve source worksheet bytes");
            check(output_reader.read_entry("xl/calcChain.xml") == planned_source.calc_chain,
                "invalid planned catalog rename output should preserve calcChain bytes");
            check(output_reader.read_entry("custom/opaque-extension.bin")
                    == planned_source.opaque_extension,
                "invalid planned catalog rename output should preserve unknown extension bytes");
        };

    const std::string missing_relationship_id_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Broken" sheetId="1" r:id="missingRel"/></sheets>)"
        R"(</workbook>)";
    expect_invalid_planned_rename_failure(
        "fastxlsx-package-editor-sheet-catalog-rename-missing-rel-source.xlsx",
        "fastxlsx-package-editor-sheet-catalog-rename-missing-rel-output.xlsx",
        missing_relationship_id_workbook,
        [](LinkedObjectSourcePackage&) {},
        "relationship id is not present",
        "PackageEditor should reject sheet catalog rename with missing planned relationship id");

    const std::string unregistered_target_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Broken" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    expect_invalid_planned_rename_failure(
        "fastxlsx-package-editor-sheet-catalog-rename-unregistered-target-source.xlsx",
        "fastxlsx-package-editor-sheet-catalog-rename-unregistered-target-output.xlsx",
        unregistered_target_workbook,
        [](LinkedObjectSourcePackage& planned_source) {
            planned_source.workbook_relationships =
                R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
                R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/missing.xml"/>)"
                R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/vbaProject" Target="vbaProject.bin"/>)"
                R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>)"
                R"(<Relationship Id="rId4" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>)"
                R"(<Relationship Id="rId5" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="calcChain.xml"/>)"
                R"(</Relationships>)";
        },
        "unknown part",
        "PackageEditor should reject sheet catalog rename with unregistered planned worksheet target");

    fastxlsx::detail::PackageEditor removed_workbook_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    removed_workbook_editor.remove_part(workbook_part, "remove workbook before rename failure");
    const std::size_t queued_plan_size = removed_workbook_editor.edit_plan().size();
    const std::size_t queued_package_entry_count =
        removed_workbook_editor.edit_plan().package_entries().size();
    const std::size_t queued_removed_package_entry_count =
        removed_workbook_editor.edit_plan().removed_package_entries().size();
    bool removed_workbook_failed = false;
    try {
        removed_workbook_editor.rename_sheet_catalog_entry("Sheet1", "Renamed");
    } catch (const std::exception& error) {
        removed_workbook_failed = true;
        check_contains(error.what(), "requires the officeDocument workbook part",
            "workbook removal should make sheet catalog rename fail before state changes");
    }
    check(removed_workbook_failed,
        "PackageEditor should reject sheet catalog rename after planned workbook removal");
    check(removed_workbook_editor.edit_plan().size() == queued_plan_size,
        "sheet catalog rename failure after workbook removal should preserve queued plan size");
    check(removed_workbook_editor.edit_plan().package_entries().size()
            == queued_package_entry_count,
        "sheet catalog rename failure after workbook removal should preserve package-entry audit");
    check(removed_workbook_editor.edit_plan().removed_package_entries().size()
            == queued_removed_package_entry_count,
        "sheet catalog rename failure after workbook removal should preserve removed package-entry audit");
    check(removed_workbook_editor.manifest().find_part(workbook_part) == nullptr,
        "sheet catalog rename failure after workbook removal should keep workbook removed");
    check_manifest_write_mode(removed_workbook_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sheet catalog rename failure after workbook removal should leave worksheet copy-original");
}

void test_package_editor_rejects_invalid_planned_workbook_catalog_by_name_without_state_changes()
{
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const auto expect_invalid_planned_catalog_failure =
        [&](std::string_view source_name, std::string_view output_name,
            std::string planned_workbook, auto configure_source,
            std::string_view expected_error, const char* scenario_message) {
            LinkedObjectSourcePackage source = write_sheet_data_patch_source_package(source_name);
            configure_source(source);
            rewrite_linked_object_source_package(source);

            fastxlsx::detail::PackageEditor editor =
                fastxlsx::detail::PackageEditor::open(source.path);
            editor.replace_part(workbook_part, planned_workbook,
                fastxlsx::detail::PartWriteMode::LocalDomRewrite,
                "invalid planned workbook catalog before by-name patch");

            const std::size_t queued_plan_size = editor.edit_plan().size();
            const std::size_t queued_note_count = editor.edit_plan().notes().size();
            const std::size_t queued_relationship_target_audit_count =
                editor.edit_plan().relationship_target_audits().size();
            const std::size_t queued_worksheet_relationship_reference_audit_count =
                editor.edit_plan().worksheet_relationship_reference_audits().size();
            const std::size_t queued_worksheet_payload_dependency_audit_count =
                editor.edit_plan().worksheet_payload_dependency_audits().size();
            const std::size_t queued_package_entry_count =
                editor.edit_plan().package_entries().size();
            const std::size_t queued_removed_package_entry_count =
                editor.edit_plan().removed_package_entries().size();

            const auto check_no_state_change = [&]() {
                check(editor.edit_plan().size() == queued_plan_size,
                    "invalid planned catalog failure should preserve queued edit-plan size");
                check(editor.edit_plan().notes().size() == queued_note_count,
                    "invalid planned catalog failure should not append notes");
                check(editor.edit_plan().relationship_target_audits().size()
                        == queued_relationship_target_audit_count,
                    "invalid planned catalog failure should not append relationship target audits");
                check(editor.edit_plan().worksheet_relationship_reference_audits().size()
                        == queued_worksheet_relationship_reference_audit_count,
                    "invalid planned catalog failure should not append worksheet relationship audits");
                check(editor.edit_plan().worksheet_payload_dependency_audits().size()
                        == queued_worksheet_payload_dependency_audit_count,
                    "invalid planned catalog failure should not append worksheet payload audits");
                check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
                    "invalid planned catalog failure should preserve package-entry audit count");
                check(editor.edit_plan().removed_package_entries().size()
                        == queued_removed_package_entry_count,
                    "invalid planned catalog failure should preserve removed package-entry audit count");
                check(editor.edit_plan().removed_parts().empty(),
                    "invalid planned catalog failure should not record removed parts");
                check(!editor.edit_plan().full_calculation_on_load(),
                    "invalid planned catalog failure should not request recalculation");
                check(editor.edit_plan().calc_chain_action()
                        == fastxlsx::detail::CalcChainAction::Preserve,
                    "invalid planned catalog failure should not change calcChain policy");
                check_manifest_write_mode(editor, workbook_part,
                    fastxlsx::detail::PartWriteMode::LocalDomRewrite,
                    "invalid planned catalog failure should keep workbook local-DOM-rewrite");
                check_manifest_write_mode(editor, worksheet_part,
                    fastxlsx::detail::PartWriteMode::CopyOriginal,
                    "invalid planned catalog failure should leave worksheet copy-original");
                check_manifest_write_mode(editor, calc_chain_part,
                    fastxlsx::detail::PartWriteMode::CopyOriginal,
                    "invalid planned catalog failure should leave calcChain copy-original");
            };

            bool failed = false;
            try {
                replace_worksheet_part_by_name_from_single_chunk_source(editor, "Broken", "<worksheet/>");
            } catch (const std::exception& error) {
                failed = true;
                check_contains(error.what(), expected_error,
                    "invalid planned catalog worksheet failure should explain the catalog issue");
            }
            check(failed, scenario_message);
            check_no_state_change();

            failed = false;
            try {
                replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Broken", "<sheetData/>");
            } catch (const std::exception& error) {
                failed = true;
                check_contains(error.what(), expected_error,
                    "invalid planned catalog sheetData failure should explain the catalog issue");
            }
            check(failed,
                "PackageEditor should reject by-name sheetData replacement with invalid planned catalog");
            check_no_state_change();

            editor.save_as(output_path(output_name));

            const fastxlsx::detail::PackageReader output_reader =
                fastxlsx::detail::PackageReader::open(output_path(output_name));
            check_preserved_source_entries(editor.reader(), output_reader, workbook_part.zip_path());
            check(output_reader.read_entry("xl/workbook.xml") == planned_workbook,
                "invalid planned catalog failure output should keep queued workbook replacement");
            check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
                "invalid planned catalog failure output should preserve source worksheet bytes");
            check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
                "invalid planned catalog failure output should preserve calcChain bytes");
            check(output_reader.read_entry("custom/opaque-extension.bin") == source.opaque_extension,
                "invalid planned catalog failure output should preserve unknown extension bytes");
        };

    const std::string missing_relationship_id_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Broken" sheetId="1" r:id="missingRel"/></sheets>)"
        R"(</workbook>)";
    expect_invalid_planned_catalog_failure(
        "fastxlsx-package-editor-invalid-planned-catalog-missing-rel-source.xlsx",
        "fastxlsx-package-editor-invalid-planned-catalog-missing-rel-output.xlsx",
        missing_relationship_id_workbook,
        [](LinkedObjectSourcePackage&) {},
        "relationship id is not present",
        "PackageEditor should reject planned workbook sheet ids missing from workbook relationships");

    const std::string unregistered_target_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Broken" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    expect_invalid_planned_catalog_failure(
        "fastxlsx-package-editor-invalid-planned-catalog-unregistered-target-source.xlsx",
        "fastxlsx-package-editor-invalid-planned-catalog-unregistered-target-output.xlsx",
        unregistered_target_workbook,
        [](LinkedObjectSourcePackage& source) {
            source.workbook_relationships =
                R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
                R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/missing.xml"/>)"
                R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/vbaProject" Target="vbaProject.bin"/>)"
                R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>)"
                R"(<Relationship Id="rId4" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>)"
                R"(<Relationship Id="rId5" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="calcChain.xml"/>)"
                R"(</Relationships>)";
        },
        "unknown part",
        "PackageEditor should reject planned workbook worksheet targets absent from the package");
}

void test_package_editor_by_name_helpers_allow_workbook_metadata_rewrite()
{
    const LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-by-name-after-workbook-metadata-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-by-name-after-workbook-metadata-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    editor.request_full_calculation();
    check(editor.edit_plan().find_part(workbook_part) != nullptr,
        "workbook metadata helper should queue workbook local-DOM rewrite");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "workbook metadata helper should queue stale calcChain removal");

    const std::string queued_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<dimension ref="A1:A1"/>)"
        R"(<sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData>)"
        R"(<autoFilter ref="A1:A1"/>)"
        R"(</worksheet>)";
    replace_worksheet_part_by_name_from_single_chunk_source(editor, "Sheet1", queued_worksheet);

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="7"><c r="A7"><v>77</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Sheet1", replacement_sheet_data);

    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr
            && workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "by-name helpers after workbook metadata helper should keep workbook local-DOM rewrite");
    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr
            && worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "by-name helpers after workbook metadata helper should local-DOM-rewrite the worksheet");
    check(editor.edit_plan().full_calculation_on_load(),
        "by-name helpers after workbook metadata helper should keep fullCalcOnLoad");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Remove,
        "by-name helpers after workbook metadata helper should keep calcChain removal policy");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "by-name helpers after workbook metadata helper should keep calcChain removed-part audit");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "by-name helpers after workbook metadata helper should expose workbook rewrite");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "by-name helpers after workbook metadata helper should expose worksheet local-DOM rewrite");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "by-name helpers after workbook metadata helper should omit stale calcChain");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "by-name helpers after workbook metadata helper should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "by-name helpers after workbook metadata helper output should omit calcChain");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string worksheet_xml = output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, replacement_sheet_data,
        "by-name helpers after workbook metadata helper output should include replacement sheetData");
    check_contains(worksheet_xml, R"(<dimension ref="A1:A1"/>)",
        "sheetData by-name helper should preserve the queued worksheet wrapper");
    check_contains(worksheet_xml, R"(<autoFilter ref="A1:A1"/>)",
        "sheetData by-name helper should preserve queued worksheet metadata");
    check_contains(output_reader.read_entry("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "by-name helpers after workbook metadata helper output should request recalculation");
    check_not_contains(output_reader.read_entry("[Content_Types].xml"), "calcChain+xml",
        "by-name helpers after workbook metadata helper output should remove calcChain content type");
    check_not_contains(output_reader.read_entry("xl/_rels/workbook.xml.rels"),
        "relationships/calcChain",
        "by-name helpers after workbook metadata helper output should remove calcChain relationship");
    check(output_reader.read_entry("custom/opaque-extension.bin") == source.opaque_extension,
        "by-name helpers after workbook metadata helper output should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "by-name helpers after workbook metadata helper output should preserve unknown extension relationships");
    check(output_reader.content_types().override_for(opaque_extension_part) == nullptr,
        "by-name helpers after workbook metadata helper output should keep unknown extension default content type");
}

void test_package_editor_by_name_helpers_use_planned_catalog_after_workbook_metadata_rewrite()
{
    LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-by-name-planned-after-workbook-metadata-source.xlsx");
    source.workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="./worksheets/../worksheets/sheet1.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/vbaProject" Target="vbaProject.bin"/>)"
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>)"
        R"(<Relationship Id="rId4" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>)"
        R"(<Relationship Id="rId5" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="calcChain.xml"/>)"
        R"(</Relationships>)";
    rewrite_linked_object_source_package(source);
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-by-name-planned-after-workbook-metadata-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string replacement_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="RenamedAfterCalc" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<definedNames><definedName name="ReportRange">RenamedAfterCalc!$A$1:$B$2</definedName></definedNames>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, replacement_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "ordinary workbook replacement before calc metadata helper and by-name patch");
    editor.request_full_calculation();

    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    const std::size_t queued_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t queued_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t queued_worksheet_payload_dependency_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t queued_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t queued_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const std::size_t queued_removed_part_count =
        editor.edit_plan().removed_parts().size();

    bool failed = false;
    try {
        replace_worksheet_part_by_name_from_single_chunk_source(editor,
            "Sheet1", "<worksheet><sheetData/></worksheet>");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "workbook sheet name is not present",
            "planned catalog after workbook metadata helper should reject the old source sheet name");
    }
    check(failed,
        "PackageEditor should keep using planned sheet names after workbook metadata helper takes ownership");
    check(editor.edit_plan().size() == queued_plan_size,
        "old-name failure after workbook metadata helper should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "old-name failure after workbook metadata helper should not append notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == queued_relationship_target_audit_count,
        "old-name failure after workbook metadata helper should not append relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == queued_worksheet_relationship_reference_audit_count,
        "old-name failure after workbook metadata helper should not append worksheet relationship audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == queued_worksheet_payload_dependency_audit_count,
        "old-name failure after workbook metadata helper should not append worksheet payload audits");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "old-name failure after workbook metadata helper should preserve package-entry audit count");
    check(editor.edit_plan().removed_package_entries().size()
            == queued_removed_package_entry_count,
        "old-name failure after workbook metadata helper should preserve removed package-entry audit count");
    check(editor.edit_plan().removed_parts().size() == queued_removed_part_count,
        "old-name failure after workbook metadata helper should preserve removed-part audit count");
    check(editor.edit_plan().full_calculation_on_load(),
        "old-name failure after workbook metadata helper should preserve fullCalcOnLoad");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Remove,
        "old-name failure after workbook metadata helper should preserve calcChain policy");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "old-name failure after workbook metadata helper should keep workbook local-DOM-rewrite");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "old-name failure after workbook metadata helper should leave worksheet copy-original");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "old-name failure after workbook metadata helper should keep calcChain omitted from manifest");

    const std::string queued_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<dimension ref="C3:C3"/>)"
        R"(<sheetData><row r="3"><c r="C3"><v>33</v></c></row></sheetData>)"
        R"(<autoFilter ref="C3:C3"/>)"
        R"(</worksheet>)";
    replace_worksheet_part_by_name_from_single_chunk_source(editor, "RenamedAfterCalc", queued_worksheet);

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="9"><c r="A9"><v>99</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor,
        "RenamedAfterCalc", replacement_sheet_data);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr
            && worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "planned catalog after workbook metadata helper should resolve renamed worksheet part");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr
            && workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "planned catalog after workbook metadata helper should keep workbook local-DOM rewrite");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "planned catalog after workbook metadata helper should keep calcChain removed-part audit");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "planned catalog after workbook metadata helper should keep calcChain omitted from manifest");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "planned catalog after workbook metadata helper output plan should expose workbook rewrite");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "planned catalog after workbook metadata helper output plan should expose worksheet local-DOM rewrite");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "planned catalog after workbook metadata helper output plan should omit calcChain");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "planned catalog after workbook metadata helper output should omit calcChain");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("RenamedAfterCalc")
            == worksheet_part,
        "planned catalog after workbook metadata helper output should expose planned sheet name");
    bool old_name_failed = false;
    try {
        (void)output_reader.worksheet_part_by_sheet_name("Sheet1");
    } catch (const std::exception&) {
        old_name_failed = true;
    }
    check(old_name_failed,
        "planned catalog after workbook metadata helper output should not expose old source sheet name");
    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, replacement_sheet_data,
        "planned catalog after workbook metadata helper output should include final sheetData");
    check_contains(worksheet_xml, R"(<dimension ref="C3:C3"/>)",
        "planned catalog after workbook metadata helper sheetData patch should preserve queued worksheet wrapper");
    check_contains(worksheet_xml, R"(<autoFilter ref="C3:C3"/>)",
        "planned catalog after workbook metadata helper sheetData patch should preserve queued metadata");
    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="RenamedAfterCalc")",
        "planned catalog after workbook metadata helper output should preserve planned sheet name");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "planned catalog after workbook metadata helper output should keep full calculation request");
    check_contains(workbook_xml, "RenamedAfterCalc!$A$1:$B$2",
        "planned catalog after workbook metadata helper output should preserve planned definedNames text");
    check_not_contains(output_reader.read_entry("[Content_Types].xml"), "calcChain+xml",
        "planned catalog after workbook metadata helper output should remove calcChain content type");
    const std::string output_workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_contains(output_workbook_relationships,
        R"(Target="./worksheets/../worksheets/sheet1.xml")",
        "planned catalog after workbook metadata helper should preserve dot-segment worksheet target text");
    check_not_contains(output_workbook_relationships, "relationships/calcChain",
        "planned catalog after workbook metadata helper output should remove calcChain relationship");
    check(output_reader.read_entry("custom/opaque-extension.bin") == source.opaque_extension,
        "planned catalog after workbook metadata helper output should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "planned catalog after workbook metadata helper output should preserve unknown extension relationships");
    check(output_reader.content_types().override_for(opaque_extension_part) == nullptr,
        "planned catalog after workbook metadata helper output should keep unknown extension default content type");
}

void test_package_editor_by_name_helpers_reject_after_workbook_removal_without_state_changes()
{
    const LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-by-name-after-workbook-removal-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-by-name-after-workbook-removal-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    editor.remove_part(workbook_part,
        "explicit workbook removal before by-name helper lookup");

    const std::size_t removal_plan_size = editor.edit_plan().size();
    const std::size_t removal_note_count = editor.edit_plan().notes().size();
    const std::size_t removal_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t removal_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t removal_worksheet_payload_dependency_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t removal_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t removal_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const std::size_t removal_removed_part_count =
        editor.edit_plan().removed_parts().size();

    const auto check_removal_state = [&]() {
        check(editor.edit_plan().size() == removal_plan_size,
            "by-name after workbook removal failure should preserve edit-plan size");
        check(editor.edit_plan().notes().size() == removal_note_count,
            "by-name after workbook removal failure should preserve note count");
        check(editor.edit_plan().relationship_target_audits().size()
                == removal_relationship_target_audit_count,
            "by-name after workbook removal failure should preserve relationship target audits");
        check(editor.edit_plan().worksheet_relationship_reference_audits().size()
                == removal_worksheet_relationship_reference_audit_count,
            "by-name after workbook removal failure should preserve worksheet relationship audits");
        check(editor.edit_plan().worksheet_payload_dependency_audits().size()
                == removal_worksheet_payload_dependency_audit_count,
            "by-name after workbook removal failure should preserve worksheet payload audits");
        check(editor.edit_plan().package_entries().size() == removal_package_entry_count,
            "by-name after workbook removal failure should preserve package-entry audits");
        check(editor.edit_plan().removed_package_entries().size()
                == removal_removed_package_entry_count,
            "by-name after workbook removal failure should preserve removed package-entry audits");
        check(editor.edit_plan().removed_parts().size() == removal_removed_part_count,
            "by-name after workbook removal failure should preserve removed-part audits");
        check(!editor.edit_plan().full_calculation_on_load(),
            "by-name after workbook removal failure should not request recalculation");
        check(editor.edit_plan().calc_chain_action()
                == fastxlsx::detail::CalcChainAction::Preserve,
            "by-name after workbook removal failure should preserve calcChain policy");
        check(editor.edit_plan().find_removed_part(workbook_part) != nullptr,
            "by-name after workbook removal failure should keep workbook removed");
        check(editor.edit_plan().find_removed_package_entry("xl/_rels/workbook.xml.rels")
                != nullptr,
            "by-name after workbook removal failure should keep workbook owner relationships omitted");
        check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels")
                == nullptr,
            "by-name after workbook removal failure should not restore workbook owner relationships");
        check(editor.manifest().find_part(workbook_part) == nullptr,
            "by-name after workbook removal failure should keep workbook absent from manifest");
        check_manifest_write_mode(editor, worksheet_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "by-name after workbook removal failure should leave worksheet copy-original");
        check_manifest_write_mode(editor, calc_chain_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "by-name after workbook removal failure should leave calcChain copy-original");
    };

    bool worksheet_failed = false;
    try {
        replace_worksheet_part_by_name_from_single_chunk_source(editor,
            "Sheet1", "<worksheet><sheetData/></worksheet>");
    } catch (const std::exception& error) {
        worksheet_failed = true;
        check_contains(error.what(), "workbook sheet catalog",
            "by-name worksheet failure after workbook removal should name the catalog");
        check_contains(error.what(), "removed",
            "by-name worksheet failure after workbook removal should name planned removal");
    }
    check(worksheet_failed,
        "PackageEditor should reject by-name worksheet replacement after workbook removal");
    check_removal_state();

    bool sheet_data_failed = false;
    try {
        replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Sheet1", "<sheetData/>");
    } catch (const std::exception& error) {
        sheet_data_failed = true;
        check_contains(error.what(), "workbook sheet catalog",
            "by-name sheetData failure after workbook removal should name the catalog");
        check_contains(error.what(), "removed",
            "by-name sheetData failure after workbook removal should name planned removal");
    }
    check(sheet_data_failed,
        "PackageEditor should reject by-name sheetData replacement after workbook removal");
    check_removal_state();

    const fastxlsx::detail::PackageEditorOutputPlan output_plan =
        editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "by-name after workbook removal output plan should keep workbook omitted");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "by-name after workbook removal output plan should keep workbook relationships omitted");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "by-name after workbook removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "by-name after workbook removal output plan should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/workbook.xml") == entries.end(),
        "by-name after workbook removal output should omit workbook part");
    check(entries.find("xl/_rels/workbook.xml.rels") == entries.end(),
        "by-name after workbook removal output should omit workbook owner relationships");
    check(entries.find("xl/worksheets/sheet1.xml") != entries.end(),
        "by-name after workbook removal output should keep worksheet part");
    check(entries.find("custom/opaque-extension.bin") != entries.end(),
        "by-name after workbook removal output should keep unknown extension");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(workbook_part) == nullptr,
        "by-name after workbook removal output should remove workbook content type");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "by-name after workbook removal output should preserve package relationships bytes");
    check(output_reader.relationships_for(workbook_part) == nullptr,
        "by-name after workbook removal output should not keep workbook owner relationships");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "by-name after workbook removal output should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "by-name after workbook removal output should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "by-name after workbook removal output should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "by-name after workbook removal output should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "by-name after workbook removal output should preserve unknown extension relationships");
    check(output_reader.content_types().override_for(worksheet_part) != nullptr,
        "by-name after workbook removal output should keep worksheet content type");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "by-name after workbook removal output should keep calcChain content type");
    check(output_reader.content_types().override_for(opaque_extension_part) == nullptr,
        "by-name after workbook removal output should keep unknown extension default content type");
}

void test_package_editor_rejects_worksheet_by_sheet_name_without_state_changes()
{
    const LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-worksheet-by-name-invalid-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-worksheet-by-name-invalid-output.xlsx");

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

    bool failed = false;
    try {
        replace_worksheet_part_by_name_from_single_chunk_source(editor, "Missing", "<worksheet/>");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "sheet name",
            "missing sheet-name worksheet replacement failure should name sheet lookup");
    }
    check(failed,
        "PackageEditor should reject worksheet replacement for a missing sheet name");
    check(editor.edit_plan().size() == initial_plan_size,
        "missing sheet-name worksheet replacement should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "missing sheet-name worksheet replacement should not add audit notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "missing sheet-name worksheet replacement should not add relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "missing sheet-name worksheet replacement should not add worksheet relationship reference audits");
    check(editor.edit_plan().removed_parts().empty(),
        "missing sheet-name worksheet replacement should not record removed parts");
    check(editor.edit_plan().package_entries().empty(),
        "missing sheet-name worksheet replacement should not record package-entry audit");
    check(!editor.edit_plan().full_calculation_on_load(),
        "missing sheet-name worksheet replacement should not request recalculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "missing sheet-name worksheet replacement should not change calcChain policy");
    check(editor.manifest().find_part(calc_chain_part) != nullptr,
        "missing sheet-name worksheet replacement should keep calcChain in the manifest");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "missing sheet-name worksheet replacement should keep worksheet copy-original");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader);
}

void test_package_editor_rejects_invalid_worksheet_replacement_xml_without_state_changes()
{
    const LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-worksheet-invalid-xml-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-worksheet-invalid-xml-output.xlsx");

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

    const auto check_no_state_change = [&]() {
        check(editor.edit_plan().size() == initial_plan_size,
            "invalid worksheet XML should not change edit plan size");
        check(editor.edit_plan().notes().size() == initial_note_count,
            "invalid worksheet XML should not add audit notes");
        check(editor.edit_plan().relationship_target_audits().size()
                == initial_relationship_target_audit_count,
            "invalid worksheet XML should not add relationship target audits");
        check(editor.edit_plan().worksheet_relationship_reference_audits().size()
                == initial_worksheet_relationship_reference_audit_count,
            "invalid worksheet XML should not add worksheet relationship reference audits");
        check(editor.edit_plan().removed_parts().empty(),
            "invalid worksheet XML should not record removed parts");
        check(editor.edit_plan().package_entries().empty(),
            "invalid worksheet XML should not record package-entry audit");
        check(editor.edit_plan().removed_package_entries().empty(),
            "invalid worksheet XML should not record removed package-entry audit");
        check(!editor.edit_plan().full_calculation_on_load(),
            "invalid worksheet XML should not request recalculation");
        check(editor.edit_plan().calc_chain_action()
                == fastxlsx::detail::CalcChainAction::Preserve,
            "invalid worksheet XML should not change calcChain policy");
        check(editor.manifest().find_part(calc_chain_part) != nullptr,
            "invalid worksheet XML should keep calcChain in the manifest");
        check_manifest_write_mode(editor, worksheet_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "invalid worksheet XML should keep worksheet copy-original");
        check_manifest_write_mode(editor, workbook_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "invalid worksheet XML should keep workbook copy-original");
        check_manifest_write_mode(editor, calc_chain_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "invalid worksheet XML should keep calcChain copy-original");
    };

    for (std::string_view invalid_xml : {
            std::string_view(""),
            std::string_view("   \r\n\t"),
            std::string_view("<sheetData/>"),
            std::string_view("<!DOCTYPE worksheet><worksheet/>"),
            std::string_view("<ignored/><worksheet/>"),
            std::string_view("<worksheet/> <!-- trailing comment -->"),
            std::string_view("<worksheet/><?trailing instruction?>"),
            std::string_view("<worksheet><sheetData/></worksheet><worksheet/>"),
            std::string_view("<worksheet><sheetData/>"),
          }) {
        bool failed = false;
        try {
            replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, std::string(invalid_xml));
        } catch (const std::exception& error) {
            failed = true;
            check_contains(error.what(), "worksheet",
                "invalid worksheet XML failure should name worksheet replacement");
        }
        check(failed,
            "PackageEditor should reject invalid worksheet replacement XML");
        check_no_state_change();
    }

    bool by_name_failed = false;
    try {
        replace_worksheet_part_by_name_from_single_chunk_source(editor, "Sheet1", "<notWorksheet/>");
    } catch (const std::exception& error) {
        by_name_failed = true;
        check_contains(error.what(), "worksheet",
            "by-name invalid worksheet XML failure should name worksheet replacement");
    }
    check(by_name_failed,
        "PackageEditor should reject invalid by-name worksheet replacement XML");
    check_no_state_change();

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader);
}

void test_package_editor_replaces_worksheet_with_payload_audit_notes()
{
    const LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-worksheet-payload-audit-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-worksheet-payload-audit-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::string replacement_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetPr filterMode="1"/>)"
        R"(<sheetCalcPr fullCalcOnLoad="1"/>)"
        R"(<dimension ref="A1:B1"/>)"
        R"(<sheetViews><sheetView workbookViewId="0"/></sheetViews>)"
        R"(<customSheetViews><customSheetView guid="{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}"/></customSheetViews>)"
        R"(<sheetFormatPr defaultRowHeight="14.25"/>)"
        R"(<cols><col min="1" max="2" width="18" customWidth="1"/></cols>)"
        R"(<sheetData><row r="1"><c r="A1" t="s"><v>0</v></c><c r="B1" s="0"><f>SUM(A1:A1)</f><v>9</v></c></row></sheetData>)"
        R"(<sortState ref="A1:B1"><sortCondition ref="A1:A1"/></sortState>)"
        R"(<scenarios><scenario name="Replacement" user="FastXLSX"/></scenarios>)"
        R"(<dataConsolidate function="count"><dataRefs count="1"><dataRef ref="A1:B1" sheet="Sheet1"/></dataRefs></dataConsolidate>)"
        R"(<customProperties><customPr name="FastXLSXReplacement"/></customProperties>)"
        R"(<cellWatches><cellWatch r="B1"/></cellWatches>)"
        R"(<smartTags><cellSmartTags r="B1"><cellSmartTag type="urn:fastxlsx:replacement"/></cellSmartTags></smartTags>)"
        R"(<webPublishItems count="1"><webPublishItem id="2" divId="FastXLSXReplacement" sourceType="range" sourceRef="Sheet1!A1:B1"/></webPublishItems>)"
        R"(<hyperlinks><hyperlink ref="A1" r:id="rId2"/></hyperlinks>)"
        R"(<printOptions gridLines="1"/>)"
        R"(<pageMargins left="0.5" right="0.5" top="0.75" bottom="0.75" header="0.3" footer="0.3"/>)"
        R"(<pageSetup paperSize="9" orientation="portrait"/>)"
        R"(<headerFooter><oddFooter>&amp;RPage &amp;P</oddFooter></headerFooter>)"
        R"(<rowBreaks count="1" manualBreakCount="1"><brk id="1" max="16383" man="1"/></rowBreaks>)"
        R"(<colBreaks count="1" manualBreakCount="1"><brk id="1" max="1048575" man="1"/></colBreaks>)"
        R"(<phoneticPr fontId="1" type="noConversion"/>)"
        R"(<drawing r:id="rId1"/>)"
        R"(<legacyDrawing r:id="rId7"/>)"
        R"(<picture r:id="rId1"/>)"
        R"(<legacyDrawingHF r:id="rId7"/>)"
        R"(<oleObjects><oleObject progId="Forms.CommandButton.1" r:id="rId1"/></oleObjects>)"
        R"(<controls><control shapeId="1" r:id="rId1"/></controls>)"
        R"(<tableParts count="1"><tablePart r:id="rId3"/></tableParts>)"
        R"(</worksheet>)";

    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_worksheet);

    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "shared string indexes", "xl/sharedStrings.xml"}),
        "worksheet replacement should audit shared string index references");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "style id references", "xl/styles.xml"}),
        "worksheet replacement should audit style id references");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "contains formulas", "calcChain policy"}),
        "worksheet replacement should audit formula references");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "sheet property metadata", "caller review"}),
        "worksheet replacement should audit sheet property metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "sheet calculation metadata", "caller review"}),
        "worksheet replacement should audit sheet calculation metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "dimension metadata", "caller review"}),
        "worksheet replacement should audit dimension metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "view metadata", "caller review"}),
        "worksheet replacement should audit sheet view metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "custom view metadata", "caller review"}),
        "worksheet replacement should audit custom view metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "default format metadata", "caller review"}),
        "worksheet replacement should audit default format metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "column metadata", "caller review"}),
        "worksheet replacement should audit column metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "sort-state metadata", "caller review"}),
        "worksheet replacement should audit sort-state metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "scenario metadata", "caller review"}),
        "worksheet replacement should audit scenario metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "data consolidation metadata", "caller review"}),
        "worksheet replacement should audit data consolidation metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "custom property metadata", "caller review"}),
        "worksheet replacement should audit custom property metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "cell watch metadata", "caller review"}),
        "worksheet replacement should audit cell watch metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "smart tag metadata", "caller review"}),
        "worksheet replacement should audit smart tag metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "web publishing metadata", "caller review"}),
        "worksheet replacement should audit web publishing metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "print options metadata", "caller review"}),
        "worksheet replacement should audit print options metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "page margins metadata", "caller review"}),
        "worksheet replacement should audit page margins metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "page setup metadata", "caller review"}),
        "worksheet replacement should audit page setup metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "header/footer metadata", "caller review"}),
        "worksheet replacement should audit header/footer metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "row break metadata", "caller review"}),
        "worksheet replacement should audit row break metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "column break metadata", "caller review"}),
        "worksheet replacement should audit column break metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "phonetic metadata", "caller review"}),
        "worksheet replacement should audit phonetic metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "hyperlink metadata", "worksheet relationships"}),
        "worksheet replacement should audit hyperlink relationship metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "drawing relationship metadata",
                  "worksheet relationships"}),
        "worksheet replacement should audit drawing relationship metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "legacy drawing relationship metadata",
                  "worksheet relationships"}),
        "worksheet replacement should audit legacy drawing relationship metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "background picture relationship metadata",
                  "worksheet relationships"}),
        "worksheet replacement should audit background picture relationship metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "header/footer drawing relationship metadata",
                  "worksheet relationships"}),
        "worksheet replacement should audit header/footer drawing relationship metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "OLE object relationship metadata",
                  "worksheet relationships"}),
        "worksheet replacement should audit OLE object relationship metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "control relationship metadata",
                  "worksheet relationships"}),
        "worksheet replacement should audit control relationship metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "table relationship metadata",
                  "worksheet relationships"}),
        "worksheet replacement should audit table relationship metadata");
    using PayloadAuditKind =
        fastxlsx::detail::WorksheetPayloadDependencyAuditKind;
    using PayloadAuditScope =
        fastxlsx::detail::WorksheetPayloadDependencyAuditScope;
    const auto& payload_audits =
        editor.edit_plan().worksheet_payload_dependency_audits();
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::SharedStrings,
              PayloadAuditScope::WorksheetReplacement, "c",
              {"shared string indexes", "xl/sharedStrings.xml"}),
        "worksheet replacement should record structured sharedStrings payload audit");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::Styles,
              PayloadAuditScope::WorksheetReplacement, "c",
              {"style id references", "xl/styles.xml"}),
        "worksheet replacement should record structured styles payload audit");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::Formula,
              PayloadAuditScope::WorksheetReplacement, "f",
              {"formulas", "calcChain policy"}),
        "worksheet replacement should record structured formula payload audit");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::RangeMetadata,
              PayloadAuditScope::WorksheetReplacement, "dimension",
              {"dimension metadata", "caller review"}),
        "worksheet replacement should record structured dimension payload audit");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::RangeMetadata,
              PayloadAuditScope::WorksheetReplacement, "sheetViews",
              {"view metadata", "caller review"}),
        "worksheet replacement should record structured sheetViews payload audit");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::RangeMetadata,
              PayloadAuditScope::WorksheetReplacement, "scenarios",
              {"scenario metadata", "caller review"}),
        "worksheet replacement should record structured scenarios payload audit");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::RangeMetadata,
              PayloadAuditScope::WorksheetReplacement, "dataConsolidate",
              {"data consolidation metadata", "caller review"}),
        "worksheet replacement should record structured dataConsolidate payload audit");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::RangeMetadata,
              PayloadAuditScope::WorksheetReplacement, "customProperties",
              {"custom property metadata", "caller review"}),
        "worksheet replacement should record structured customProperties payload audit");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::RangeMetadata,
              PayloadAuditScope::WorksheetReplacement, "cellWatches",
              {"cell watch metadata", "caller review"}),
        "worksheet replacement should record structured cellWatches payload audit");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::RangeMetadata,
              PayloadAuditScope::WorksheetReplacement, "smartTags",
              {"smart tag metadata", "caller review"}),
        "worksheet replacement should record structured smartTags payload audit");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::RangeMetadata,
              PayloadAuditScope::WorksheetReplacement, "webPublishItems",
              {"web publishing metadata", "caller review"}),
        "worksheet replacement should record structured webPublishItems payload audit");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::RelationshipMetadata,
              PayloadAuditScope::WorksheetReplacement, "drawing",
              {"drawing relationship metadata", "worksheet relationships"}),
        "worksheet replacement should record structured drawing payload audit");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::RelationshipMetadata,
              PayloadAuditScope::WorksheetReplacement, "tableParts",
              {"table relationship metadata", "worksheet relationships"}),
        "worksheet replacement should record structured tableParts payload audit");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "worksheet payload audit replacement should stream-rewrite worksheet");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "worksheet payload audit replacement should rewrite workbook calc metadata");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "worksheet payload audit replacement should remove calcChain by default");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(has_note_containing(output_plan.notes,
              {"worksheet replacement", "shared string indexes", "xl/sharedStrings.xml"}),
        "worksheet payload audit output plan should snapshot sharedStrings note");
    check(has_note_containing(output_plan.notes,
              {"worksheet replacement", "style id references", "xl/styles.xml"}),
        "worksheet payload audit output plan should snapshot styles note");
    check(has_note_containing(output_plan.notes,
              {"worksheet replacement", "contains formulas", "calcChain policy"}),
        "worksheet payload audit output plan should snapshot formula note");
    check(has_note_containing(output_plan.notes,
              {"worksheet replacement", "dimension metadata", "caller review"}),
        "worksheet payload audit output plan should snapshot dimension note");
    check(has_note_containing(output_plan.notes,
              {"worksheet replacement", "view metadata", "caller review"}),
        "worksheet payload audit output plan should snapshot sheet view note");
    check(has_note_containing(output_plan.notes,
              {"worksheet replacement", "column metadata", "caller review"}),
        "worksheet payload audit output plan should snapshot column note");
    check(has_note_containing(output_plan.notes,
              {"worksheet replacement", "scenario metadata", "caller review"}),
        "worksheet payload audit output plan should snapshot scenario note");
    check(has_note_containing(output_plan.notes,
              {"worksheet replacement", "page setup metadata", "caller review"}),
        "worksheet payload audit output plan should snapshot page setup note");
    check(has_note_containing(output_plan.notes,
              {"worksheet replacement", "drawing relationship metadata",
                  "worksheet relationships"}),
        "worksheet payload audit output plan should snapshot linked metadata note");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == payload_audits.size(),
        "worksheet payload audit output plan should mirror structured payload audits");
    check(has_payload_audit(output_plan.worksheet_payload_dependency_audits,
              worksheet_part, PayloadAuditKind::SharedStrings,
              PayloadAuditScope::WorksheetReplacement, "c",
              {"shared string indexes", "xl/sharedStrings.xml"}),
        "worksheet payload audit output plan should keep structured sharedStrings audit");
    check(has_payload_audit(output_plan.worksheet_payload_dependency_audits,
              worksheet_part, PayloadAuditKind::RelationshipMetadata,
              PayloadAuditScope::WorksheetReplacement, "drawing",
              {"drawing relationship metadata", "worksheet relationships"}),
        "worksheet payload audit output plan should keep structured drawing audit");
    check(has_payload_audit(output_plan.worksheet_payload_dependency_audits,
              worksheet_part, PayloadAuditKind::RangeMetadata,
              PayloadAuditScope::WorksheetReplacement, "scenarios",
              {"scenario metadata", "caller review"}),
        "worksheet payload audit output plan should keep structured scenario audit");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_worksheet,
        "worksheet payload audit output should write replacement worksheet XML");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "worksheet payload audit output should preserve worksheet relationships");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "worksheet payload audit output should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "worksheet payload audit output should preserve styles bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "worksheet payload audit output should preserve drawing bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "worksheet payload audit output should preserve table bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "worksheet payload audit output should preserve unknown extension bytes");
}

void test_package_editor_worksheet_replacement_audits_relationship_id_mismatch()
{
    const LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-worksheet-rid-audit-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-worksheet-rid-audit-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    const std::string replacement_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>41</v></c></row></sheetData>)"
        R"(<rowBreaks count="1" manualBreakCount="1"><brk id="1" max="16383" man="1"/></rowBreaks>)"
        R"(<hyperlinks><hyperlink ref="A1" r:id="rId2"/></hyperlinks>)"
        R"(<pageSetup r:id="rIdPrinterMissing"/>)"
        R"(<pageSetup r:id="rId2"/>)"
        R"(<drawing r:id="rId2"/>)"
        R"(<drawing r:id="rIdMissing"/>)"
        R"(<tableParts count="1"><tablePart r:id="rId3"/></tableParts>)"
        R"(</worksheet>)";

    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_worksheet);

    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "relationship id rIdMissing", "<drawing>",
                  "do not contain that id", "repair worksheet .rels"}),
        "worksheet replacement should audit replacement relationship ids missing from source .rels");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "relationship id rIdPrinterMissing", "<pageSetup>",
                  "do not contain that id", "repair worksheet .rels"}),
        "worksheet replacement should audit missing printer settings relationship ids");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement leaves preserved worksheet relationship id rId1",
                  "unreferenced", "stale linked-object relationships"}),
        "worksheet replacement should audit preserved source relationships omitted by replacement XML");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "relationship id rId2", "<drawing>",
                  "relationships/hyperlink", "expected", "relationships/drawing",
                  "review worksheet .rels"}),
        "worksheet replacement should audit relationship ids whose type does not match the element");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "relationship id rId2", "<pageSetup>",
                  "relationships/hyperlink", "expected", "relationships/printerSettings",
                  "review worksheet .rels"}),
        "worksheet replacement should audit printer settings relationship type mismatches");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement references relationship id 1"}),
        "worksheet replacement relationship-id audit should ignore ordinary unqualified id attributes");

    using WorksheetReferenceAuditKind =
        fastxlsx::detail::WorksheetRelationshipReferenceAuditKind;
    static constexpr std::string_view drawing_relationship_type =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing";
    static constexpr std::string_view hyperlink_relationship_type =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink";
    static constexpr std::string_view printer_settings_relationship_type =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/printerSettings";
    const auto has_reference_audit = [&](const auto& audits,
                                         WorksheetReferenceAuditKind kind,
                                         std::string_view element,
                                         std::string_view relationship_id,
                                         std::string_view expected_type,
                                         std::string_view actual_type,
                                         std::string_view note_text) {
        return std::any_of(audits.begin(), audits.end(),
            [&](const fastxlsx::detail::WorksheetRelationshipReferenceAudit& audit) {
                return audit.worksheet_part == worksheet_part && audit.kind == kind
                    && audit.element == element && audit.relationship_id == relationship_id
                    && audit.expected_relationship_type == expected_type
                    && audit.actual_relationship_type == actual_type
                    && audit.note.find(note_text) != std::string::npos;
            });
    };

    const auto& relationship_reference_audits =
        editor.edit_plan().worksheet_relationship_reference_audits();
    check(has_reference_audit(relationship_reference_audits,
              WorksheetReferenceAuditKind::MissingRelationshipId, "drawing", "rIdMissing",
              drawing_relationship_type, {}, "do not contain that id"),
        "worksheet replacement should record structured missing relationship-id audit");
    check(has_reference_audit(relationship_reference_audits,
              WorksheetReferenceAuditKind::MissingRelationshipId, "pageSetup",
              "rIdPrinterMissing", printer_settings_relationship_type, {},
              "do not contain that id"),
        "worksheet replacement should record structured missing printer settings audit");
    check(has_reference_audit(relationship_reference_audits,
              WorksheetReferenceAuditKind::UnreferencedRelationshipId, {}, "rId1", {},
              drawing_relationship_type, "unreferenced"),
        "worksheet replacement should record structured unreferenced relationship-id audit");
    check(has_reference_audit(relationship_reference_audits,
              WorksheetReferenceAuditKind::TypeMismatch, "drawing", "rId2",
              drawing_relationship_type, hyperlink_relationship_type, "expected"),
        "worksheet replacement should record structured relationship type mismatch audit");
    check(has_reference_audit(relationship_reference_audits,
              WorksheetReferenceAuditKind::TypeMismatch, "pageSetup", "rId2",
              printer_settings_relationship_type, hyperlink_relationship_type, "expected"),
        "worksheet replacement should record structured printer settings type mismatch audit");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(has_note_containing(output_plan.notes,
              {"relationship id rIdMissing", "<drawing>", "repair worksheet .rels"}),
        "worksheet relationship-id audit should be visible in planned output notes");
    check(has_note_containing(output_plan.notes,
              {"relationship id rIdPrinterMissing", "<pageSetup>",
                  "repair worksheet .rels"}),
        "printer settings relationship-id audit should be visible in planned output notes");
    check(has_note_containing(output_plan.notes,
              {"preserved worksheet relationship id rId1", "unreferenced"}),
        "worksheet unreferenced relationship audit should be visible in planned output notes");
    check(has_note_containing(output_plan.notes,
              {"relationship id rId2", "<drawing>", "relationships/hyperlink",
                  "relationships/drawing"}),
        "worksheet relationship type mismatch audit should be visible in planned output notes");
    check(has_note_containing(output_plan.notes,
              {"relationship id rId2", "<pageSetup>", "relationships/hyperlink",
                  "relationships/printerSettings"}),
        "printer settings type mismatch audit should be visible in planned output notes");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == relationship_reference_audits.size(),
        "planned output should mirror structured worksheet relationship reference audits");
    check(has_reference_audit(output_plan.worksheet_relationship_reference_audits,
              WorksheetReferenceAuditKind::MissingRelationshipId, "drawing", "rIdMissing",
              drawing_relationship_type, {}, "do not contain that id"),
        "planned output should keep structured missing relationship-id audit");
    check(has_reference_audit(output_plan.worksheet_relationship_reference_audits,
              WorksheetReferenceAuditKind::MissingRelationshipId, "pageSetup",
              "rIdPrinterMissing", printer_settings_relationship_type, {},
              "do not contain that id"),
        "planned output should keep structured missing printer settings audit");
    check(has_reference_audit(output_plan.worksheet_relationship_reference_audits,
              WorksheetReferenceAuditKind::UnreferencedRelationshipId, {}, "rId1", {},
              drawing_relationship_type, "unreferenced"),
        "planned output should keep structured unreferenced relationship-id audit");
    check(has_reference_audit(output_plan.worksheet_relationship_reference_audits,
              WorksheetReferenceAuditKind::TypeMismatch, "drawing", "rId2",
              drawing_relationship_type, hyperlink_relationship_type, "expected"),
        "planned output should keep structured relationship type mismatch audit");
    check(has_reference_audit(output_plan.worksheet_relationship_reference_audits,
              WorksheetReferenceAuditKind::TypeMismatch, "pageSetup", "rId2",
              printer_settings_relationship_type, hyperlink_relationship_type, "expected"),
        "planned output should keep structured printer settings type mismatch audit");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_worksheet,
        "relationship-id audit output should write the replacement worksheet XML");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "relationship-id audit output should preserve worksheet relationships byte-for-byte");
    const fastxlsx::detail::RelationshipSet* worksheet_relationships =
        output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "relationship-id audit output should keep worksheet relationships readable");
    check(worksheet_relationships->find_by_id("rId1") != nullptr,
        "relationship-id audit output should not prune unreferenced preserved relationships");
    const fastxlsx::detail::Relationship* r_id2 =
        worksheet_relationships->find_by_id("rId2");
    check(r_id2 != nullptr && r_id2->type.find("relationships/hyperlink") != std::string::npos,
        "relationship type mismatch audit output should not rewrite preserved relationship types");
    check(worksheet_relationships->find_by_id("rIdMissing") == nullptr,
        "relationship-id audit output should not synthesize missing relationships");
    check(worksheet_relationships->find_by_id("rIdPrinterMissing") == nullptr,
        "relationship-id audit output should not synthesize printer settings relationships");
}

void test_package_editor_worksheet_relationship_id_audit_respects_relationship_namespace()
{
    const LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-worksheet-rid-namespace-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-worksheet-rid-namespace-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    const std::string replacement_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" )"
        R"(xmlns:rel="http://schemas.openxmlformats.org/officeDocument/2006/relationships" )"
        R"(xmlns:x="urn:fastxlsx:not-relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>42</v></c></row></sheetData>)"
        R"(<drawing x:id="rId2"/>)"
        R"(<hyperlinks><hyperlink ref="A1" x:id="rId2"/></hyperlinks>)"
        R"(<drawing rel:id="rId1"/>)"
        R"(<drawing r:id="rIdMissing"/>)"
        R"(</worksheet>)";

    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_worksheet);

    using WorksheetReferenceAuditKind =
        fastxlsx::detail::WorksheetRelationshipReferenceAuditKind;
    static constexpr std::string_view drawing_relationship_type =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing";
    static constexpr std::string_view hyperlink_relationship_type =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink";
    const auto has_reference_audit = [&](const auto& audits,
                                         WorksheetReferenceAuditKind kind,
                                         std::string_view element,
                                         std::string_view relationship_id,
                                         std::string_view expected_type,
                                         std::string_view actual_type,
                                         std::string_view note_text) {
        return std::any_of(audits.begin(), audits.end(),
            [&](const fastxlsx::detail::WorksheetRelationshipReferenceAudit& audit) {
                return audit.worksheet_part == worksheet_part && audit.kind == kind
                    && audit.element == element && audit.relationship_id == relationship_id
                    && audit.expected_relationship_type == expected_type
                    && audit.actual_relationship_type == actual_type
                    && audit.note.find(note_text) != std::string::npos;
            });
    };

    const auto& relationship_reference_audits =
        editor.edit_plan().worksheet_relationship_reference_audits();
    check(has_reference_audit(relationship_reference_audits,
              WorksheetReferenceAuditKind::MissingRelationshipId, "drawing", "rIdMissing",
              drawing_relationship_type, {}, "do not contain that id"),
        "namespace-aware relationship-id audit should still record missing r:id references");
    check(has_reference_audit(relationship_reference_audits,
              WorksheetReferenceAuditKind::UnreferencedRelationshipId, {}, "rId2", {},
              hyperlink_relationship_type, "unreferenced"),
        "wrong-namespace x:id should not mark preserved hyperlink relationship ids as referenced");
    check(!has_reference_audit(relationship_reference_audits,
              WorksheetReferenceAuditKind::TypeMismatch, "drawing", "rId2",
              drawing_relationship_type, hyperlink_relationship_type, "expected"),
        "wrong-namespace x:id should not produce relationship type mismatch audits");
    check(!has_reference_audit(relationship_reference_audits,
              WorksheetReferenceAuditKind::UnreferencedRelationshipId, {}, "rId1", {},
              drawing_relationship_type, "unreferenced"),
        "alternate relationship namespace prefix should mark drawing relationship ids as referenced");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"relationship id rId2", "<drawing>", "relationships/hyperlink",
                  "relationships/drawing"}),
        "wrong-namespace x:id should not be described as a drawing relationship reference");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.worksheet_relationship_reference_audits.size()
            == relationship_reference_audits.size(),
        "planned output should mirror namespace-aware worksheet relationship audits");
    check(has_reference_audit(output_plan.worksheet_relationship_reference_audits,
              WorksheetReferenceAuditKind::MissingRelationshipId, "drawing", "rIdMissing",
              drawing_relationship_type, {}, "do not contain that id"),
        "planned output should keep missing r:id audit after namespace filtering");
    check(has_reference_audit(output_plan.worksheet_relationship_reference_audits,
              WorksheetReferenceAuditKind::UnreferencedRelationshipId, {}, "rId2", {},
              hyperlink_relationship_type, "unreferenced"),
        "planned output should keep wrong-namespace x:id unreferenced audit");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_worksheet,
        "namespace-aware relationship-id audit output should write replacement worksheet XML");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "namespace-aware relationship-id audit output should preserve worksheet relationships");
    const fastxlsx::detail::RelationshipSet* worksheet_relationships =
        output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "namespace-aware relationship-id audit output should keep worksheet relationships readable");
    check(worksheet_relationships->find_by_id("rIdMissing") == nullptr,
        "namespace-aware relationship-id audit output should not synthesize missing relationships");
}

void test_package_editor_worksheet_replacement_audits_missing_relationships_entry()
{
    const CalcSourcePackage source = write_calc_source_package(
        "fastxlsx-package-editor-worksheet-missing-rels-audit-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-worksheet-missing-rels-audit-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    const std::string replacement_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>42</v></c></row></sheetData>)"
        R"(<drawing r:id="rId1"/>)"
        R"(</worksheet>)";

    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_worksheet);

    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "relationship id rId1", "<drawing>",
                  "source worksheet relationships are missing", "repair worksheet .rels"}),
        "worksheet replacement should audit missing worksheet relationships entry");

    using WorksheetReferenceAuditKind =
        fastxlsx::detail::WorksheetRelationshipReferenceAuditKind;
    static constexpr std::string_view drawing_relationship_type =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing";
    const auto has_missing_relationships_audit = [&](const auto& audits) {
        return std::any_of(audits.begin(), audits.end(),
            [&](const fastxlsx::detail::WorksheetRelationshipReferenceAudit& audit) {
                return audit.worksheet_part == worksheet_part
                    && audit.kind == WorksheetReferenceAuditKind::MissingRelationships
                    && audit.element == "drawing" && audit.relationship_id == "rId1"
                    && audit.expected_relationship_type == drawing_relationship_type
                    && audit.actual_relationship_type.empty()
                    && audit.note.find("source worksheet relationships are missing")
                        != std::string::npos;
            });
    };
    check(has_missing_relationships_audit(
              editor.edit_plan().worksheet_relationship_reference_audits()),
        "worksheet replacement should record structured missing relationships-entry audit");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(has_note_containing(output_plan.notes,
              {"relationship id rId1", "<drawing>",
                  "source worksheet relationships are missing"}),
        "missing worksheet relationships audit should be visible in planned output notes");
    check(has_missing_relationships_audit(
              output_plan.worksheet_relationship_reference_audits),
        "planned output should keep structured missing relationships-entry audit");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_worksheet,
        "missing relationships-entry audit output should write replacement worksheet XML");
    check(output_reader.find_entry("xl/worksheets/_rels/sheet1.xml.rels") == nullptr,
        "missing relationships-entry audit output should not synthesize worksheet relationships");
    check(output_reader.relationships_for(worksheet_part) == nullptr,
        "missing relationships-entry audit output should keep relationship graph without worksheet relationships");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "missing relationships-entry audit output should preserve unknown bytes");
    check(output_reader.find_entry("xl/calcChain.xml") == nullptr,
        "missing relationships-entry audit output should keep stale calcChain omitted");
}

void test_package_editor_reference_policy_fail_blocks_missing_relationship_references_without_state_changes()
{
    const CalcSourcePackage source = write_calc_source_package(
        "fastxlsx-package-editor-policy-fail-missing-rels-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-policy-fail-missing-rels-output.xlsx");

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
    const std::size_t initial_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t initial_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t initial_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();

    fastxlsx::detail::ReferencePolicy fail_policy;
    fail_policy.unsupported_linked_part_action =
        fastxlsx::detail::ReferencePolicyAction::Fail;

    const std::string replacement_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>42</v></c></row></sheetData>)"
        R"(<drawing r:id="rId1"/>)"
        R"(</worksheet>)";

    bool failed = false;
    try {
        replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_worksheet, fail_policy);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "relationship references blocked by reference policy",
            "missing worksheet relationship references should name reference policy");
    }
    check(failed,
        "PackageEditor should fail missing worksheet relationships when reference policy is Fail");

    check(editor.edit_plan().size() == initial_plan_size,
        "missing relationship policy failure should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "missing relationship policy failure should not add audit notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "missing relationship policy failure should not add relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "missing relationship policy failure should not add worksheet relationship audits");
    check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
        "missing relationship policy failure should not add package-entry audit");
    check(editor.edit_plan().removed_parts().size() == initial_removed_part_count,
        "missing relationship policy failure should not record removed parts");
    check(editor.edit_plan().removed_package_entries().size()
            == initial_removed_package_entry_count,
        "missing relationship policy failure should not record removed package entries");
    check(!editor.edit_plan().full_calculation_on_load(),
        "missing relationship policy failure should not request full calculation");
    check(editor.edit_plan().calc_chain_action()
            == fastxlsx::detail::CalcChainAction::Preserve,
        "missing relationship policy failure should not change calcChain action");
    check(editor.manifest().find_part(calc_chain_part) != nullptr,
        "missing relationship policy failure should keep calcChain in the manifest");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "missing relationship policy failure should leave worksheet copy-original");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "missing relationship policy failure should leave workbook copy-original");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "missing relationship policy failure should leave calcChain copy-original");

    editor.save_as(output);
    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(source_reader, output_reader);
    check(output_reader.find_entry("xl/worksheets/_rels/sheet1.xml.rels") == nullptr,
        "missing relationship policy failure should not synthesize worksheet relationships");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "missing relationship policy failure output should preserve unknown bytes");
}

void test_package_editor_reference_policy_fail_blocks_payload_dependencies_without_state_changes()
{
    const CalcSourcePackage source = write_calc_source_package(
        "fastxlsx-package-editor-policy-fail-payload-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-policy-fail-payload-output.xlsx");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
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
    const std::size_t initial_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t initial_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t initial_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();

    fastxlsx::detail::ReferencePolicy fail_policy;
    fail_policy.unsupported_linked_part_action =
        fastxlsx::detail::ReferencePolicyAction::Fail;

    const std::string replacement_worksheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1" t="s" s="1"><f>SUM(B1:C1)</f><v>0</v></c></row></sheetData><scenarios><scenario name="Blocked"/></scenarios></worksheet>)";

    bool failed = false;
    try {
        replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_worksheet, fail_policy);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "payload dependencies blocked by reference policy",
            "payload dependency policy failure should name reference policy");
    }
    check(failed,
        "ReferencePolicyAction::Fail should reject worksheet payload-only dependencies");

    check(editor.edit_plan().size() == initial_plan_size,
        "payload policy failure should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "payload policy failure should not add audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "shared string indexes"}),
        "payload policy failure should not leak sharedStrings audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "style id references"}),
        "payload policy failure should not leak styles audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "contains formulas"}),
        "payload policy failure should not leak formula audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "scenario metadata"}),
        "payload policy failure should not leak worksheet metadata audit notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "payload policy failure should not add relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "payload policy failure should not add worksheet relationship audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == initial_worksheet_payload_dependency_audit_count,
        "payload policy failure should not add worksheet payload dependency audits");
    check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
        "payload policy failure should not add package-entry audit");
    check(editor.edit_plan().removed_parts().size() == initial_removed_part_count,
        "payload policy failure should not record removed parts");
    check(editor.edit_plan().removed_package_entries().size()
            == initial_removed_package_entry_count,
        "payload policy failure should not record removed package entries");
    check(!editor.edit_plan().full_calculation_on_load(),
        "payload policy failure should not request full calculation");
    check(editor.edit_plan().calc_chain_action()
            == fastxlsx::detail::CalcChainAction::Preserve,
        "payload policy failure should not change calcChain action");
    check(editor.manifest().find_part(calc_chain_part) != nullptr,
        "payload policy failure should keep calcChain in the manifest");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "payload policy failure should leave worksheet copy-original");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "payload policy failure should leave workbook copy-original");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "payload policy failure should leave calcChain copy-original");

    editor.save_as(output);
    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(source_reader, output_reader);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "payload policy failure output should preserve worksheet bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "payload policy failure output should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "payload policy failure output should preserve unknown bytes");
}

void test_package_editor_reference_policy_fail_blocks_sheet_data_payload_dependencies_without_state_changes()
{
    const CalcSourcePackage source = write_calc_source_package(
        "fastxlsx-package-editor-policy-fail-sheetdata-payload-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-policy-fail-sheetdata-payload-output.xlsx");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
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
    const std::size_t initial_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t initial_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t initial_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();

    fastxlsx::detail::ReferencePolicy fail_policy;
    fail_policy.unsupported_linked_part_action =
        fastxlsx::detail::ReferencePolicyAction::Fail;

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="1"><c r="A1" t="s" s="1"><f>SUM(B1:C1)</f><v>0</v></c></row></sheetData>)";

    bool failed = false;
    try {
        replace_worksheet_sheet_data_from_single_chunk_source(editor,
            worksheet_part, replacement_sheet_data, fail_policy);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "payload dependencies blocked by reference policy",
            "sheetData payload policy failure should name reference policy");
    }
    check(failed,
        "ReferencePolicyAction::Fail should reject sheetData payload-only dependencies");

    check(editor.edit_plan().size() == initial_plan_size,
        "sheetData payload policy failure should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "sheetData payload policy failure should not add audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "shared string indexes"}),
        "sheetData payload policy failure should not leak sharedStrings audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "style id references"}),
        "sheetData payload policy failure should not leak styles audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "contains formulas"}),
        "sheetData payload policy failure should not leak formula audit notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "sheetData payload policy failure should not add relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "sheetData payload policy failure should not add worksheet relationship audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == initial_worksheet_payload_dependency_audit_count,
        "sheetData payload policy failure should not add worksheet payload dependency audits");
    check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
        "sheetData payload policy failure should not add package-entry audit");
    check(editor.edit_plan().removed_parts().size() == initial_removed_part_count,
        "sheetData payload policy failure should not record removed parts");
    check(editor.edit_plan().removed_package_entries().size()
            == initial_removed_package_entry_count,
        "sheetData payload policy failure should not record removed package entries");
    check(!editor.edit_plan().full_calculation_on_load(),
        "sheetData payload policy failure should not request full calculation");
    check(editor.edit_plan().calc_chain_action()
            == fastxlsx::detail::CalcChainAction::Preserve,
        "sheetData payload policy failure should not change calcChain action");
    check(editor.manifest().find_part(calc_chain_part) != nullptr,
        "sheetData payload policy failure should keep calcChain in the manifest");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sheetData payload policy failure should leave worksheet copy-original");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sheetData payload policy failure should leave workbook copy-original");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sheetData payload policy failure should leave calcChain copy-original");

    editor.save_as(output);
    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(source_reader, output_reader);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "sheetData payload policy failure output should preserve worksheet bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "sheetData payload policy failure output should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "sheetData payload policy failure output should preserve unknown bytes");
}

void test_package_editor_rejects_sheet_name_lookup_with_invalid_office_document_without_state_changes()
{
    LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-worksheet-by-name-bad-office-document-source.xlsx");
    source.package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="https://example.invalid/workbook.xml" TargetMode="External"/>)"
        R"(</Relationships>)";
    rewrite_linked_object_source_package(source);

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-worksheet-by-name-bad-office-document-output.xlsx");
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

    const auto check_no_state_change = [&]() {
        check(editor.edit_plan().size() == initial_plan_size,
            "invalid officeDocument lookup should not change edit plan size");
        check(editor.edit_plan().notes().size() == initial_note_count,
            "invalid officeDocument lookup should not add audit notes");
        check(editor.edit_plan().relationship_target_audits().size()
                == initial_relationship_target_audit_count,
            "invalid officeDocument lookup should not add relationship target audits");
        check(editor.edit_plan().worksheet_relationship_reference_audits().size()
                == initial_worksheet_relationship_reference_audit_count,
            "invalid officeDocument lookup should not add worksheet relationship reference audits");
        check(editor.edit_plan().removed_parts().empty(),
            "invalid officeDocument lookup should not record removed parts");
        check(editor.edit_plan().package_entries().empty(),
            "invalid officeDocument lookup should not record package-entry audit");
        check(editor.edit_plan().removed_package_entries().empty(),
            "invalid officeDocument lookup should not record removed package-entry audit");
        check(!editor.edit_plan().full_calculation_on_load(),
            "invalid officeDocument lookup should not request recalculation");
        check(editor.edit_plan().calc_chain_action()
                == fastxlsx::detail::CalcChainAction::Preserve,
            "invalid officeDocument lookup should not change calcChain policy");
        check(editor.manifest().find_part(calc_chain_part) != nullptr,
            "invalid officeDocument lookup should keep calcChain in the manifest");
        check_manifest_write_mode(editor, worksheet_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "invalid officeDocument lookup should keep worksheet copy-original");
        check_manifest_write_mode(editor, workbook_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "invalid officeDocument lookup should keep workbook copy-original");
        check_manifest_write_mode(editor, calc_chain_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "invalid officeDocument lookup should keep calcChain copy-original");
    };

    bool failed = false;
    try {
        replace_worksheet_part_by_name_from_single_chunk_source(editor, "Sheet1", "<worksheet/>");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "officeDocument",
            "invalid officeDocument worksheet replacement failure should name package entrypoint");
    }
    check(failed,
        "PackageEditor should reject by-name worksheet replacement with invalid officeDocument");
    check_no_state_change();

    failed = false;
    try {
        replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Sheet1", "<sheetData/>");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "officeDocument",
            "invalid officeDocument sheetData replacement failure should name package entrypoint");
    }
    check(failed,
        "PackageEditor should reject by-name sheetData replacement with invalid officeDocument");
    check_no_state_change();

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader);
}

void test_package_editor_rejects_invalid_source_workbook_sheet_catalog_without_state_changes()
{
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const auto expect_source_catalog_failure =
        [&](std::string_view source_name, std::string_view output_name,
            auto configure_source, std::string_view expected_error,
            const char* scenario_message) {
            LinkedObjectSourcePackage source = write_sheet_data_patch_source_package(source_name);
            configure_source(source);
            rewrite_linked_object_source_package(source);

            fastxlsx::detail::PackageEditor editor =
                fastxlsx::detail::PackageEditor::open(source.path);
            const std::size_t initial_plan_size = editor.edit_plan().size();
            const std::size_t initial_note_count = editor.edit_plan().notes().size();
            const std::size_t initial_relationship_target_audit_count =
                editor.edit_plan().relationship_target_audits().size();
            const std::size_t initial_worksheet_relationship_reference_audit_count =
                editor.edit_plan().worksheet_relationship_reference_audits().size();
            const std::size_t initial_worksheet_payload_dependency_audit_count =
                editor.edit_plan().worksheet_payload_dependency_audits().size();
            const std::size_t initial_package_entry_count =
                editor.edit_plan().package_entries().size();
            const std::size_t initial_removed_package_entry_count =
                editor.edit_plan().removed_package_entries().size();

            const auto check_no_state_change = [&]() {
                check(editor.edit_plan().size() == initial_plan_size,
                    "invalid source catalog lookup should not change edit plan size");
                check(editor.edit_plan().notes().size() == initial_note_count,
                    "invalid source catalog lookup should not add audit notes");
                check(editor.edit_plan().relationship_target_audits().size()
                        == initial_relationship_target_audit_count,
                    "invalid source catalog lookup should not add relationship target audits");
                check(editor.edit_plan().worksheet_relationship_reference_audits().size()
                        == initial_worksheet_relationship_reference_audit_count,
                    "invalid source catalog lookup should not add worksheet relationship audits");
                check(editor.edit_plan().worksheet_payload_dependency_audits().size()
                        == initial_worksheet_payload_dependency_audit_count,
                    "invalid source catalog lookup should not add worksheet payload audits");
                check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
                    "invalid source catalog lookup should not record package-entry audit");
                check(editor.edit_plan().removed_package_entries().size()
                        == initial_removed_package_entry_count,
                    "invalid source catalog lookup should not record removed package-entry audit");
                check(editor.edit_plan().removed_parts().empty(),
                    "invalid source catalog lookup should not record removed parts");
                check(!editor.edit_plan().full_calculation_on_load(),
                    "invalid source catalog lookup should not request recalculation");
                check(editor.edit_plan().calc_chain_action()
                        == fastxlsx::detail::CalcChainAction::Preserve,
                    "invalid source catalog lookup should not change calcChain policy");
                check(editor.manifest().find_part(calc_chain_part) != nullptr,
                    "invalid source catalog lookup should keep calcChain in the manifest");
                check_manifest_write_mode(editor, worksheet_part,
                    fastxlsx::detail::PartWriteMode::CopyOriginal,
                    "invalid source catalog lookup should keep worksheet copy-original");
                check_manifest_write_mode(editor, workbook_part,
                    fastxlsx::detail::PartWriteMode::CopyOriginal,
                    "invalid source catalog lookup should keep workbook copy-original");
                check_manifest_write_mode(editor, calc_chain_part,
                    fastxlsx::detail::PartWriteMode::CopyOriginal,
                    "invalid source catalog lookup should keep calcChain copy-original");
            };

            bool failed = false;
            try {
                replace_worksheet_part_by_name_from_single_chunk_source(editor, "Sheet1", "<worksheet/>");
            } catch (const std::exception& error) {
                failed = true;
                check_contains(error.what(), expected_error,
                    "invalid source catalog worksheet replacement failure should explain the catalog issue");
            }
            check(failed, scenario_message);
            check_no_state_change();

            failed = false;
            try {
                replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Sheet1", "<sheetData/>");
            } catch (const std::exception& error) {
                failed = true;
                check_contains(error.what(), expected_error,
                    "invalid source catalog sheetData failure should explain the catalog issue");
            }
            check(failed,
                "PackageEditor should reject by-name sheetData replacement with invalid source catalog");
            check_no_state_change();

            editor.save_as(output_path(output_name));
            const fastxlsx::detail::PackageReader output_reader =
                fastxlsx::detail::PackageReader::open(output_path(output_name));
            check_preserved_source_entries(editor.reader(), output_reader);
            check(output_reader.read_entry("custom/opaque-extension.bin")
                    == source.opaque_extension,
                "invalid source catalog output should preserve unknown extension bytes");
        };

    expect_source_catalog_failure(
        "fastxlsx-package-editor-worksheet-by-name-missing-rel-source.xlsx",
        "fastxlsx-package-editor-worksheet-by-name-missing-rel-output.xlsx",
        [](LinkedObjectSourcePackage& source) {
            source.workbook =
                R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
                R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="missingRel"/></sheets>)"
                R"(<definedNames><definedName name="ReportRange">Sheet1!$A$1:$B$2</definedName></definedNames>)"
                R"(</workbook>)";
        },
        "relationship id is not present",
        "PackageEditor should reject by-name worksheet replacement with missing source sheet relationship id");

    expect_source_catalog_failure(
        "fastxlsx-package-editor-worksheet-by-name-unregistered-target-source.xlsx",
        "fastxlsx-package-editor-worksheet-by-name-unregistered-target-output.xlsx",
        [](LinkedObjectSourcePackage& source) {
            source.workbook_relationships =
                R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
                R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/missing.xml"/>)"
                R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/vbaProject" Target="vbaProject.bin"/>)"
                R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>)"
                R"(<Relationship Id="rId4" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>)"
                R"(<Relationship Id="rId5" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="calcChain.xml"/>)"
                R"(</Relationships>)";
        },
        "unknown part",
        "PackageEditor should reject by-name worksheet replacement with unregistered worksheet target");
}


} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor sheetdata shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "sheetdata-catalog")) {
            test_package_editor_rejects_staged_small_xml_replacements_without_state_changes();
            test_package_editor_rejects_oversized_workbook_xml_materialization_without_state_changes();
            test_package_editor_rejects_shared_strings_materialized_replacement_without_state_changes();
            test_package_editor_rejects_styles_materialized_replacement_without_state_changes();
            test_package_editor_rejects_generic_source_part_materialized_replacement_without_state_changes();
            test_package_editor_rejects_oversized_metadata_xml_materialization_without_state_changes();
            test_package_editor_renames_sheet_catalog_entry_preserving_parts();
            test_package_editor_sheet_catalog_rename_rewrites_defined_names_opt_in();
            test_package_editor_sheet_catalog_rename_defined_name_rewrite_failure_is_clean();
            test_package_editor_sheet_catalog_rename_uses_planned_workbook_xml();
            test_package_editor_rejects_sheet_catalog_rename_without_state_changes();
            test_package_editor_rejects_invalid_planned_workbook_catalog_by_name_without_state_changes();
            test_package_editor_by_name_helpers_allow_workbook_metadata_rewrite();
            test_package_editor_by_name_helpers_use_planned_catalog_after_workbook_metadata_rewrite();
            test_package_editor_by_name_helpers_reject_after_workbook_removal_without_state_changes();
            test_package_editor_rejects_worksheet_by_sheet_name_without_state_changes();
            test_package_editor_rejects_invalid_worksheet_replacement_xml_without_state_changes();
            test_package_editor_replaces_worksheet_with_payload_audit_notes();
            test_package_editor_worksheet_replacement_audits_relationship_id_mismatch();
            test_package_editor_worksheet_relationship_id_audit_respects_relationship_namespace();
            test_package_editor_worksheet_replacement_audits_missing_relationships_entry();
            test_package_editor_reference_policy_fail_blocks_missing_relationship_references_without_state_changes();
            test_package_editor_reference_policy_fail_blocks_payload_dependencies_without_state_changes();
            test_package_editor_reference_policy_fail_blocks_sheet_data_payload_dependencies_without_state_changes();
            test_package_editor_rejects_sheet_name_lookup_with_invalid_office_document_without_state_changes();
            test_package_editor_rejects_invalid_source_workbook_sheet_catalog_without_state_changes();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
