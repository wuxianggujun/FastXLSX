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
    return shard == "all" || shard == "sheetdata-linked";
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
void test_package_editor_replaces_worksheet_and_preserves_linked_object_parts()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-linked-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-linked-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
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

    const std::string replacement_sheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>99</v></c></row></sheetData>)"
        R"(<sheetProtection sheet="1"/>)"
        R"(<protectedRanges><protectedRange name="Locked" sqref="A1:B2"/></protectedRanges>)"
        R"(<autoFilter ref="A1:B2"/>)"
        R"(<mergeCells count="1"><mergeCell ref="A1:B1"/></mergeCells>)"
        R"(<dataValidations count="1"><dataValidation type="whole" sqref="A1:B2"><formula1>1</formula1></dataValidation></dataValidations>)"
        R"(<conditionalFormatting sqref="A1:B2"><cfRule type="expression" priority="1"><formula>$A$1&gt;0</formula></cfRule></conditionalFormatting>)"
        R"(<hyperlinks><hyperlink ref="A1" r:id="rId2"/></hyperlinks>)"
        R"(<ignoredErrors><ignoredError sqref="A1:B2" numberStoredAsText="1"/></ignoredErrors>)"
        R"(<drawing r:id="rId1"/>)"
        R"(<legacyDrawing r:id="rId7"/>)"
        R"(<tableParts count="1"><tablePart r:id="rId3"/></tableParts>)"
        R"(<extLst><ext uri="{fastxlsx-test}"/></extLst>)"
        R"(</worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_sheet);

    const auto* drawing_plan = editor.edit_plan().find_part(drawing_part);
    check(drawing_plan != nullptr,
        "linked drawing part should remain visible in the edit plan");
    check(drawing_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked drawing part should remain copy-original");
    check(drawing_plan->reason.find("worksheet relationship rId1")
            != std::string::npos,
        "linked drawing copy reason should come from worksheet relationship traversal");
    check(drawing_plan->reason.find("relationships/drawing") != std::string::npos,
        "linked drawing copy reason should include relationship type");
    check(drawing_plan->relationship_owner_part == worksheet_part.value(),
        "linked drawing copy audit should keep structured relationship owner");
    check(drawing_plan->relationship_id == "rId1",
        "linked drawing copy audit should keep structured relationship id");
    check(drawing_plan->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
        "linked drawing copy audit should keep structured relationship type");
    check(drawing_plan->relationship_target == "../drawings/drawing1.xml",
        "linked drawing copy audit should keep structured relationship target");
    const auto* chart_plan = editor.edit_plan().find_part(chart_part);
    check(chart_plan != nullptr,
        "linked chart part should remain visible in the edit plan");
    check(chart_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked chart part should remain copy-original");
    check(chart_plan->reason.find("/xl/drawings/drawing1.xml") != std::string::npos
            && chart_plan->reason.find("rId2") != std::string::npos,
        "linked chart copy reason should come from drawing relationship traversal");
    check(chart_plan->relationship_owner_part == drawing_part.value(),
        "linked chart copy audit should keep drawing-owned relationship owner");
    check(chart_plan->relationship_id == "rId2",
        "linked chart copy audit should keep drawing-owned relationship id");
    check(chart_plan->relationship_target == "../charts/chart1.xml",
        "linked chart copy audit should keep drawing-owned relationship target");
    const auto* image_plan = editor.edit_plan().find_part(image_part);
    check(image_plan != nullptr,
        "linked image part should remain visible in the edit plan");
    check(image_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked image part should remain copy-original");
    check(image_plan->reason.find("/xl/drawings/drawing1.xml") != std::string::npos
            && image_plan->reason.find("rId1") != std::string::npos,
        "linked image copy reason should come from drawing relationship traversal");
    const auto* table_plan = editor.edit_plan().find_part(table_part);
    check(table_plan != nullptr,
        "linked table part should remain visible in the edit plan");
    check(table_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked table part should remain copy-original");
    check(table_plan->reason.find("worksheet relationship rId3") != std::string::npos,
        "linked table copy reason should come from worksheet relationship traversal");
    const auto* vml_drawing_plan = editor.edit_plan().find_part(vml_drawing_part);
    check(vml_drawing_plan != nullptr,
        "URI-qualified base target should remain visible in the edit plan");
    check(vml_drawing_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "URI-qualified base target should remain copy-original");
    check(vml_drawing_plan->reason.find("rId7") != std::string::npos
            && vml_drawing_plan->reason.find("/xl/drawings/vmlDrawing1.vml")
                != std::string::npos,
        "URI-qualified base target copy reason should come from relationship traversal");
    const auto* percent_encoded_drawing_plan =
        editor.edit_plan().find_part(percent_encoded_drawing_part);
    check(percent_encoded_drawing_plan != nullptr,
        "percent-decoded target should remain visible in the edit plan");
    check(percent_encoded_drawing_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "percent-decoded target should remain copy-original");
    check(percent_encoded_drawing_plan->reason.find("rId8") != std::string::npos
            && percent_encoded_drawing_plan->reason.find("/xl/drawings/drawing space.xml")
                != std::string::npos,
        "percent-decoded target copy reason should come from relationship traversal");
    const auto* shared_strings_plan = editor.edit_plan().find_part(shared_strings_part);
    check(shared_strings_plan != nullptr,
        "sharedStrings part should remain visible in the edit plan");
    check(shared_strings_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings part should remain copy-original");
    check(shared_strings_plan->reason.find("shared strings") != std::string::npos,
        "sharedStrings copy reason should come from dependency analysis");
    check(shared_strings_plan->relationship_owner_part.empty(),
        "sharedStrings static dependency should not carry relationship owner metadata");
    const auto* styles_plan = editor.edit_plan().find_part(styles_part);
    check(styles_plan != nullptr,
        "styles part should remain visible in the edit plan");
    check(styles_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles part should remain copy-original");
    check(styles_plan->reason.find("style ids") != std::string::npos,
        "styles copy reason should come from dependency analysis");
    check(styles_plan->relationship_owner_part.empty(),
        "styles static dependency should not carry relationship owner metadata");
    const auto* vba_plan = editor.edit_plan().find_part(vba_part);
    check(vba_plan != nullptr,
        "vba part should remain visible in the edit plan");
    check(vba_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "vba part should remain copy-original");
    const auto* opaque_extension_plan = editor.edit_plan().find_part(opaque_extension_part);
    check(opaque_extension_plan != nullptr,
        "reachable unknown extension part should remain visible in the edit plan");
    check(opaque_extension_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "reachable unknown extension part should remain copy-original");
    check(opaque_extension_plan->reason.find("rId9") != std::string::npos
            && opaque_extension_plan->reason.find(
                   "https://fastxlsx.invalid/relationships/opaque-extension")
                != std::string::npos
            && opaque_extension_plan->reason.find("/custom/opaque-extension.bin")
                != std::string::npos,
        "reachable unknown extension copy reason should come from relationship traversal");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "protection metadata", "caller review"}),
        "worksheet replacement should audit protection metadata in replacement payload");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "protected-range metadata", "caller review"}),
        "worksheet replacement should audit protected range metadata in replacement payload");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "autoFilter metadata", "caller review"}),
        "worksheet replacement should audit autoFilter metadata in replacement payload");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "merged-cell metadata", "caller review"}),
        "worksheet replacement should audit merged-cell metadata in replacement payload");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "data validation metadata", "caller review"}),
        "worksheet replacement should audit data validation metadata in replacement payload");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "conditional formatting metadata", "caller review"}),
        "worksheet replacement should audit conditional formatting metadata in replacement payload");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "ignored-error metadata", "caller review"}),
        "worksheet replacement should audit ignored-error metadata in replacement payload");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "extension metadata", "caller review"}),
        "worksheet replacement should audit extension metadata in replacement payload");
    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load,
        "linked-object output plan should expose full calculation request");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Remove,
        "linked-object output plan should expose calcChain removal policy");
    check(output_plan.relationship_target_audits.size()
            == editor.edit_plan().relationship_target_audits().size(),
        "linked-object output plan should snapshot structured relationship audits");
    check(has_note_containing(output_plan.notes,
              {"worksheet relationships are preserved", "policy review"}),
        "linked-object output plan should snapshot dependency audit notes");
    check(has_note_containing(output_plan.notes,
              {"worksheet replacement", "autoFilter metadata", "caller review"}),
        "linked-object output plan should snapshot replacement autoFilter audit notes");
    check(has_note_containing(output_plan.notes,
              {"worksheet replacement", "data validation metadata", "caller review"}),
        "linked-object output plan should snapshot replacement data validation audit notes");
    check(has_note_containing(output_plan.notes,
              {"worksheet replacement", "extension metadata", "caller review"}),
        "linked-object output plan should snapshot replacement extension audit notes");
    check_output_entry_relationship_context(output_plan.entries, "xl/drawings/drawing1.xml",
        worksheet_part.value(), "rId1",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
        "../drawings/drawing1.xml",
        "linked drawing output plan should keep worksheet relationship audit");
    check_output_entry_relationship_context(output_plan.entries, "xl/charts/chart1.xml",
        drawing_part.value(), "rId2",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart",
        "../charts/chart1.xml",
        "linked chart output plan should keep drawing relationship audit");
    check_output_entry_relationship_context(output_plan.entries, "xl/media/image1.png",
        drawing_part.value(), "rId1",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image",
        "../media/image1.png",
        "linked image output plan should keep drawing relationship audit");
    check_output_entry_relationship_context(output_plan.entries, "xl/tables/table1.xml",
        worksheet_part.value(), "rId3",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/table",
        "../tables/table1.xml",
        "linked table output plan should keep worksheet relationship audit");
    check_output_entry_relationship_context(output_plan.entries, "custom/opaque-extension.bin",
        worksheet_part.value(), "rId9",
        "https://fastxlsx.invalid/relationships/opaque-extension",
        "../../custom/opaque-extension.bin",
        "linked unknown extension output plan should keep worksheet relationship audit");
    check_output_entry_relationship_context(output_plan.entries, "xl/sharedStrings.xml", "", "",
        "", "",
        "sharedStrings output plan should not invent relationship-derived audit");
    check_output_entry_relationship_context(output_plan.entries, "xl/styles.xml", "", "", "", "",
        "styles output plan should not invent relationship-derived audit");
    const auto* worksheet_relationships_plan =
        editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels");
    check(worksheet_relationships_plan != nullptr,
        "linked-object worksheet rewrite should audit preserved worksheet relationships");
    check(worksheet_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "preserved worksheet relationships should be copy-original in package-entry audit");
    check(worksheet_relationships_plan->reason.find("/xl/worksheets/sheet1.xml")
            != std::string::npos,
        "preserved worksheet relationships audit should name the owner part");
    const auto* drawing_relationships_plan =
        editor.edit_plan().find_package_entry("xl/drawings/_rels/drawing1.xml.rels");
    check(drawing_relationships_plan != nullptr,
        "linked-object worksheet rewrite should audit preserved drawing relationships");
    check(drawing_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "preserved drawing relationships should be copy-original in package-entry audit");
    check(drawing_relationships_plan->reason.find("/xl/drawings/drawing1.xml")
            != std::string::npos,
        "preserved drawing relationships audit should name the owner part");
    const auto* shared_strings_relationships_plan =
        editor.edit_plan().find_package_entry("xl/_rels/sharedStrings.xml.rels");
    check(shared_strings_relationships_plan != nullptr,
        "linked-object worksheet rewrite should audit preserved sharedStrings relationships");
    check(shared_strings_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "preserved sharedStrings relationships should be copy-original in package-entry audit");
    check(shared_strings_relationships_plan->reason.find("/xl/sharedStrings.xml")
            != std::string::npos,
        "preserved sharedStrings relationships audit should name the owner part");
    const auto* opaque_extension_relationships_plan =
        editor.edit_plan().find_package_entry("custom/_rels/opaque-extension.bin.rels");
    check(opaque_extension_relationships_plan != nullptr,
        "linked-object worksheet rewrite should audit preserved unknown extension relationships");
    check(opaque_extension_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "preserved unknown extension relationships should be copy-original in package-entry audit");
    check(opaque_extension_relationships_plan->reason.find("/custom/opaque-extension.bin")
            != std::string::npos,
        "preserved unknown extension relationships audit should name the owner part");
    bool found_external_relationship_note = false;
    bool found_external_relationship_detail_note = false;
    bool found_uri_qualified_relationship_note = false;
    bool found_uri_qualified_relationship_detail_note = false;
    bool found_invalid_internal_relationship_note = false;
    bool found_invalid_internal_relationship_detail_note = false;
    bool found_unresolved_internal_relationship_note = false;
    bool found_unresolved_internal_relationship_detail_note = false;
    for (const std::string& note : editor.edit_plan().notes()) {
        if (note.find("external relationship targets") != std::string::npos) {
            found_external_relationship_note = true;
        }
        if (note.find("external relationship targets are preserved in owner .rels")
                != std::string::npos
            && note.find("/xl/worksheets/sheet1.xml") != std::string::npos
            && note.find("rId2") != std::string::npos
            && note.find("relationships/hyperlink") != std::string::npos
            && note.find("https://example.invalid/link") != std::string::npos) {
            found_external_relationship_detail_note = true;
        }
        if (note.find("URI-qualified internal relationship targets") != std::string::npos) {
            found_uri_qualified_relationship_note = true;
        }
        if (note.find("URI-qualified internal relationship targets require package structure review")
                != std::string::npos
            && note.find("rId7") != std::string::npos
            && note.find("relationships/vmlDrawing") != std::string::npos
            && note.find("../drawings/vmlDrawing1.vml#shape1") != std::string::npos
            && note.find("/xl/drawings/vmlDrawing1.vml") != std::string::npos) {
            found_uri_qualified_relationship_detail_note = true;
        }
        if (note.find("invalid internal relationship targets") != std::string::npos) {
            found_invalid_internal_relationship_note = true;
        }
        if (note.find("invalid internal relationship targets require package structure review")
                != std::string::npos
            && note.find("rId6") != std::string::npos
            && note.find("../../../outside.bin") != std::string::npos) {
            found_invalid_internal_relationship_detail_note = true;
        }
        if (note.find("unresolved internal relationship targets") != std::string::npos) {
            found_unresolved_internal_relationship_note = true;
        }
        if (note.find("unresolved internal relationship targets require package structure review")
                != std::string::npos
            && note.find("rId4") != std::string::npos
            && note.find("../comments/comment1.xml") != std::string::npos
            && note.find("/xl/comments/comment1.xml") != std::string::npos) {
            found_unresolved_internal_relationship_detail_note = true;
        }
    }
    check(found_external_relationship_note,
        "linked-object worksheet rewrite should audit external relationship targets");
    check(found_external_relationship_detail_note,
        "linked-object worksheet rewrite should include external relationship details");
    check(found_uri_qualified_relationship_note,
        "linked-object worksheet rewrite should audit URI-qualified internal targets");
    check(found_uri_qualified_relationship_detail_note,
        "linked-object worksheet rewrite should include URI-qualified relationship details");
    check(found_invalid_internal_relationship_note,
        "linked-object worksheet rewrite should audit invalid internal targets");
    check(found_invalid_internal_relationship_detail_note,
        "linked-object worksheet rewrite should include invalid relationship details");
    check(found_unresolved_internal_relationship_note,
        "linked-object worksheet rewrite should audit unresolved internal targets");
    check(found_unresolved_internal_relationship_detail_note,
        "linked-object worksheet rewrite should include unresolved relationship details");
    check(has_note_containing(editor.edit_plan().notes(),
              {"external relationship targets are preserved in owner .rels",
                  "/xl/drawings/drawing1.xml", "rId3",
                  "https://drawing.example.invalid/link"}),
        "linked-object worksheet rewrite should include drawing-owned external relationship details");
    check(has_note_containing(editor.edit_plan().notes(),
              {"external relationship targets are preserved in owner .rels",
                  "/custom/opaque-extension.bin", "rIdOpaqueExternal",
                  "https://example.invalid/opaque-extension-audit"}),
        "linked-object worksheet rewrite should include unknown extension-owned external relationship details");
    check(has_note_containing(editor.edit_plan().notes(),
              {"URI-qualified internal relationship targets require package structure review",
                  "/xl/drawings/drawing1.xml", "rId4", "../charts/chart1.xml#plotArea",
                  "/xl/charts/chart1.xml"}),
        "linked-object worksheet rewrite should include drawing-owned URI-qualified relationship details");
    check(has_note_containing(editor.edit_plan().notes(),
              {"unresolved internal relationship targets require package structure review",
                  "/xl/drawings/drawing1.xml", "rId5", "../embeddings/oleObject1.bin",
                  "/xl/embeddings/oleObject1.bin"}),
        "linked-object worksheet rewrite should include drawing-owned unresolved relationship details");
    check(has_note_containing(editor.edit_plan().notes(),
              {"invalid internal relationship targets require package structure review",
                  "/xl/drawings/drawing1.xml", "rId6", "../../../outside-from-drawing.bin"}),
        "linked-object worksheet rewrite should include drawing-owned invalid relationship details");
    bool found_structured_worksheet_external_audit = false;
    bool found_structured_drawing_external_audit = false;
    bool found_structured_drawing_uri_audit = false;
    bool found_structured_drawing_unresolved_audit = false;
    bool found_structured_drawing_invalid_audit = false;
    bool found_structured_unknown_external_audit = false;
    for (const fastxlsx::detail::RelationshipTargetAudit& audit :
        editor.edit_plan().relationship_target_audits()) {
        if (audit.owner_part == worksheet_part && audit.relationship_id == "rId2"
            && audit.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink"
            && audit.target == "https://example.invalid/link"
            && audit.normalized_target.empty()
            && audit.note.find("external relationship targets") != std::string::npos) {
            found_structured_worksheet_external_audit = true;
        }
        if (audit.owner_part == drawing_part && audit.relationship_id == "rId3"
            && audit.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink"
            && audit.target == "https://drawing.example.invalid/link"
            && audit.normalized_target.empty()
            && audit.note.find("external relationship targets") != std::string::npos) {
            found_structured_drawing_external_audit = true;
        }
        if (audit.owner_part == drawing_part && audit.relationship_id == "rId4"
            && audit.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart"
            && audit.target == "../charts/chart1.xml#plotArea"
            && audit.normalized_target == "/xl/charts/chart1.xml"
            && audit.note.find("has base part /xl/charts/chart1.xml")
                != std::string::npos) {
            found_structured_drawing_uri_audit = true;
        }
        if (audit.owner_part == drawing_part && audit.relationship_id == "rId5"
            && audit.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/oleObject"
            && audit.target == "../embeddings/oleObject1.bin"
            && audit.normalized_target == "/xl/embeddings/oleObject1.bin"
            && audit.note.find("resolves to unregistered part /xl/embeddings/oleObject1.bin")
                != std::string::npos) {
            found_structured_drawing_unresolved_audit = true;
        }
        if (audit.owner_part == drawing_part && audit.relationship_id == "rId6"
            && audit.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/oleObject"
            && audit.target == "../../../outside-from-drawing.bin"
            && audit.normalized_target.empty()
            && audit.note.find("cannot be normalized as a package part")
                != std::string::npos) {
            found_structured_drawing_invalid_audit = true;
        }
        if (audit.owner_part == opaque_extension_part
            && audit.relationship_id == "rIdOpaqueExternal"
            && audit.relationship_type
                == "https://fastxlsx.invalid/relationships/opaque-extension-audit"
            && audit.target == "https://example.invalid/opaque-extension-audit"
            && audit.normalized_target.empty()
            && audit.note.find("external relationship targets") != std::string::npos) {
            found_structured_unknown_external_audit = true;
        }
    }
    check(found_structured_worksheet_external_audit,
        "linked-object worksheet rewrite should preserve structured worksheet-owned external audit");
    check(found_structured_drawing_external_audit,
        "linked-object worksheet rewrite should preserve structured drawing-owned external audit");
    check(found_structured_drawing_uri_audit,
        "linked-object worksheet rewrite should preserve structured drawing-owned URI-qualified audit");
    check(found_structured_drawing_unresolved_audit,
        "linked-object worksheet rewrite should preserve structured drawing-owned unresolved audit");
    check(found_structured_drawing_invalid_audit,
        "linked-object worksheet rewrite should preserve structured drawing-owned invalid audit");
    check(found_structured_unknown_external_audit,
        "linked-object worksheet rewrite should preserve structured unknown-extension-owned external audit");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "linked-object worksheet rewrite should still remove stale calcChain");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "linked-object worksheet rewrite should omit stale calcChain.xml");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_sheet,
        "linked-object worksheet rewrite should replace only the worksheet XML");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "worksheet relationships should be byte-preserved");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "drawing XML should be byte-preserved");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "drawing relationships should be byte-preserved");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "chart XML should be byte-preserved");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "media bytes should be byte-preserved");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "table XML should be byte-preserved");
    check(output_reader.read_entry("xl/drawings/vmlDrawing1.vml") == source.vml_drawing,
        "URI-qualified base target bytes should be byte-preserved");
    check(output_reader.read_entry("xl/drawings/drawing space.xml")
            == source.percent_encoded_drawing,
        "percent-decoded relationship target bytes should be byte-preserved");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "sharedStrings XML should be byte-preserved");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "sharedStrings relationships should be byte-preserved");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "styles XML should be byte-preserved");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "VBA project bytes should be byte-preserved");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "unknown extension bytes should be byte-preserved");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "unknown extension relationships should be byte-preserved");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "package relationships should be byte-preserved");

    const auto* sheet_relationships = output_reader.relationships_for(worksheet_part);
    check(sheet_relationships != nullptr,
        "preserved worksheet relationships should remain readable");
    check(sheet_relationships->find_by_id("rId1") != nullptr,
        "preserved drawing relationship should remain readable");
    check(sheet_relationships->find_by_id("rId2") != nullptr,
        "preserved external hyperlink relationship should remain readable");
    check(sheet_relationships->find_by_id("rId3") != nullptr,
        "preserved table relationship should remain readable");
    check(sheet_relationships->find_by_id("rId4") != nullptr,
        "preserved unresolved internal relationship should remain readable");
    check(sheet_relationships->find_by_id("rId5") != nullptr,
        "preserved URI-qualified internal relationship should remain readable");
    check(sheet_relationships->find_by_id("rId6") != nullptr,
        "preserved invalid internal relationship should remain readable");
    check(sheet_relationships->find_by_id("rId7") != nullptr,
        "preserved URI-qualified base target relationship should remain readable");
    check(sheet_relationships->find_by_id("rId8") != nullptr,
        "preserved percent-encoded target relationship should remain readable");
    const auto* drawing_relationships = output_reader.relationships_for(drawing_part);
    check(drawing_relationships != nullptr,
        "preserved drawing relationships should remain readable");
    check(drawing_relationships->find_by_id("rId1") != nullptr,
        "preserved image relationship should remain readable");
    check(drawing_relationships->find_by_id("rId2") != nullptr,
        "preserved chart relationship should remain readable");
    check(drawing_relationships->find_by_id("rId3") != nullptr,
        "preserved drawing-owned external relationship should remain readable");
    check(drawing_relationships->find_by_id("rId4") != nullptr,
        "preserved drawing-owned URI-qualified relationship should remain readable");
    check(drawing_relationships->find_by_id("rId5") != nullptr,
        "preserved drawing-owned unresolved relationship should remain readable");
    check(drawing_relationships->find_by_id("rId6") != nullptr,
        "preserved drawing-owned invalid relationship should remain readable");

    const std::string workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_contains(workbook_relationships, "relationships/sharedStrings",
        "workbook relationships should preserve sharedStrings relationship");
    check_contains(workbook_relationships, "relationships/styles",
        "workbook relationships should preserve styles relationship");
    check_contains(workbook_relationships, "relationships/vbaProject",
        "workbook relationships should preserve VBA relationship");
    check_not_contains(workbook_relationships, "relationships/calcChain",
        "workbook relationships should remove only calcChain relationship");
    const auto* workbook_relationship_set = output_reader.relationships_for(workbook_part);
    check(workbook_relationship_set != nullptr,
        "preserved workbook relationships should remain readable");
    check(workbook_relationship_set->find_by_id("rId3") != nullptr,
        "preserved sharedStrings relationship should remain readable");
    check(workbook_relationship_set->find_by_id("rId4") != nullptr,
        "preserved styles relationship should remain readable");
    const auto* shared_strings_relationships =
        output_reader.relationships_for(shared_strings_part);
    check(shared_strings_relationships != nullptr,
        "preserved sharedStrings relationships should remain readable");
    check(shared_strings_relationships->find_by_id("rIdSharedExternal") != nullptr,
        "preserved sharedStrings owner relationships should remain attached to sharedStrings");
    const auto* opaque_extension_relationships =
        output_reader.relationships_for(opaque_extension_part);
    check(opaque_extension_relationships != nullptr,
        "preserved unknown extension relationships should remain readable");
    check(opaque_extension_relationships->find_by_id("rIdOpaqueExternal") != nullptr,
        "preserved unknown extension owner relationships should remain attached to the extension part");
    check(output_reader.content_types().override_for(calc_chain_part) == nullptr,
        "content types should remove calcChain override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "content types should preserve sharedStrings override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "content types should preserve styles override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "content types should preserve VBA override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "content types should preserve table override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "content types should preserve PNG default");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "content types should not promote PNG media defaults to overrides");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, R"(PartName="/xl/media/image1.png")",
        "rewritten content types should not add unnecessary image overrides");

    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml,
        R"(<definedNames><definedName name="ReportRange">Sheet1!$A$1:$B$2</definedName></definedNames>)",
        "workbook XML should preserve defined names while updating calc metadata");
    check_contains(workbook_xml, R"(<calcPr calcId="124519" fullCalcOnLoad="1"/>)",
        "workbook XML should add calcPr when it was absent");
}
void test_package_editor_sheet_data_patch_preserves_worksheet_owned_object_parts()
{
    const std::filesystem::path source =
        output_path("fastxlsx-package-editor-sheetdata-objects-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-sheetdata-objects-output.xlsx");

    const std::string content_types =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/embeddings/oleObject1.bin" ContentType="application/vnd.openxmlformats-officedocument.oleObject"/>)"
        R"(<Override PartName="/xl/ctrlProps/control1.xml" ContentType="application/vnd.ms-excel.controlproperties+xml"/>)"
        R"(</Types>)";
    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    const std::string worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<dimension ref="A1:B2"/>)"
        R"(<sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData>)"
        R"(<oleObjects><oleObject progId="Forms.CommandButton.1" r:id="rIdOle"/></oleObjects>)"
        R"(<controls><control shapeId="1" r:id="rIdControl"/></controls>)"
        R"(</worksheet>)";
    const std::string worksheet_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdOle" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/oleObject" Target="../embeddings/oleObject1.bin"/>)"
        R"(<Relationship Id="rIdControl" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/control" Target="../ctrlProps/control1.xml"/>)"
        R"(</Relationships>)";
    const std::string ole_object = std::string("OLE\0OBJECT\0opaque", 17);
    const std::string control_properties =
        R"(<controlPr xmlns="http://schemas.microsoft.com/office/spreadsheetml/2010/11/main" shapeId="1"/>)";

    fastxlsx::detail::write_package(source,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", workbook},
            {"xl/_rels/workbook.xml.rels", workbook_relationships},
            {"xl/worksheets/sheet1.xml", worksheet},
            {"xl/worksheets/_rels/sheet1.xml.rels", worksheet_relationships},
            {"xl/embeddings/oleObject1.bin", ole_object},
            {"xl/ctrlProps/control1.xml", control_properties},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName ole_part("/xl/embeddings/oleObject1.bin");
    const fastxlsx::detail::PartName control_part("/xl/ctrlProps/control1.xml");

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="2"><c r="A2"><v>2</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_from_single_chunk_source(editor, worksheet_part, replacement_sheet_data);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "OLE sheetData replacement should keep worksheet in edit plan");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "OLE sheetData replacement should local-DOM-rewrite worksheet");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "OLE sheetData replacement should plan workbook calc metadata rewrite");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "OLE sheetData replacement should local-DOM-rewrite workbook calc metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "OLE object reference metadata", "caller review"}),
        "OLE sheetData replacement should audit preserved OLE object metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "control reference metadata", "caller review"}),
        "control sheetData replacement should audit preserved control metadata");

    const auto* ole_plan = editor.edit_plan().find_part(ole_part);
    check(ole_plan != nullptr,
        "worksheet-owned OLE part should remain visible in the edit plan");
    check(ole_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet-owned OLE part should remain copy-original");
    check(ole_plan->reason.find("worksheet relationship rIdOle") != std::string::npos
            && ole_plan->reason.find("relationships/oleObject") != std::string::npos
            && ole_plan->reason.find("/xl/embeddings/oleObject1.bin") != std::string::npos,
        "worksheet-owned OLE copy reason should come from worksheet relationship traversal");
    check(ole_plan->relationship_owner_part == worksheet_part.value(),
        "worksheet-owned OLE audit should keep structured relationship owner");
    check(ole_plan->relationship_id == "rIdOle",
        "worksheet-owned OLE audit should keep structured relationship id");
    check(ole_plan->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/oleObject",
        "worksheet-owned OLE audit should keep structured relationship type");
    check(ole_plan->relationship_target == "../embeddings/oleObject1.bin",
        "worksheet-owned OLE audit should keep structured relationship target");
    const auto* control_plan = editor.edit_plan().find_part(control_part);
    check(control_plan != nullptr,
        "worksheet-owned control part should remain visible in the edit plan");
    check(control_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet-owned control part should remain copy-original");
    check(control_plan->reason.find("worksheet relationship rIdControl") != std::string::npos
            && control_plan->reason.find("relationships/control") != std::string::npos
            && control_plan->reason.find("/xl/ctrlProps/control1.xml") != std::string::npos,
        "worksheet-owned control copy reason should come from worksheet relationship traversal");
    check(control_plan->relationship_owner_part == worksheet_part.value(),
        "worksheet-owned control audit should keep structured relationship owner");
    check(control_plan->relationship_id == "rIdControl",
        "worksheet-owned control audit should keep structured relationship id");
    check(control_plan->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/control",
        "worksheet-owned control audit should keep structured relationship type");
    check(control_plan->relationship_target == "../ctrlProps/control1.xml",
        "worksheet-owned control audit should keep structured relationship target");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load,
        "OLE/control sheetData output plan should request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Remove,
        "OLE/control sheetData output plan should expose calcChain removal policy");
    check(output_plan.relationship_target_audits.empty(),
        "OLE/control sheetData output plan should not invent dependency audits");
    check(output_plan.worksheet_relationship_reference_audits.empty(),
        "OLE/control sheetData output plan should not invent relationship-id audits");
    check(output_plan.removed_parts.empty(),
        "OLE/control sheetData output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "OLE/control sheetData output plan should not expose removed package entries");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "OLE object reference metadata", "caller review"}),
        "OLE/control sheetData output plan should snapshot preserved OLE notes");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "control reference metadata", "caller review"}),
        "OLE/control sheetData output plan should snapshot preserved control notes");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "OLE/control sheetData output plan should rewrite worksheet");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/sheet1.xml",
        true, worksheet_part.value(),
        "OLE/control sheetData output plan should classify worksheet as a part");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "OLE/control sheetData output plan should rewrite workbook metadata");
    check_output_entry_part_context(output_plan.entries, "xl/workbook.xml",
        true, workbook_part.value(),
        "OLE/control sheetData output plan should classify workbook as a part");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "OLE/control sheetData output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "OLE/control sheetData output plan should classify content types as metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "OLE/control sheetData output plan should preserve package relationships");
    check_output_entry_part_context(output_plan.entries, "_rels/.rels", false, "",
        "OLE/control sheetData output plan should classify package relationships as metadata");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "OLE/control sheetData output plan should preserve workbook relationships");
    check_output_entry_part_context(output_plan.entries, "xl/_rels/workbook.xml.rels",
        false, "",
        "OLE/control sheetData output plan should classify workbook relationships as metadata");
    check_output_entry_plan(output_plan.entries, "xl/embeddings/oleObject1.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "OLE sheetData output plan should preserve worksheet-owned OLE bytes");
    check_output_entry_part_context(output_plan.entries, "xl/embeddings/oleObject1.bin",
        true, ole_part.value(),
        "OLE sheetData output plan should classify OLE as a part");
    check_output_entry_relationship_context(output_plan.entries,
        "xl/embeddings/oleObject1.bin", worksheet_part.value(), "rIdOle",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/oleObject",
        "../embeddings/oleObject1.bin",
        "OLE sheetData output plan should keep worksheet relationship audit");
    check_output_entry_plan(output_plan.entries, "xl/ctrlProps/control1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "control sheetData output plan should preserve worksheet-owned control properties");
    check_output_entry_part_context(output_plan.entries, "xl/ctrlProps/control1.xml",
        true, control_part.value(),
        "control sheetData output plan should classify control properties as a part");
    check_output_entry_relationship_context(output_plan.entries,
        "xl/ctrlProps/control1.xml", worksheet_part.value(), "rIdControl",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/control",
        "../ctrlProps/control1.xml",
        "control sheetData output plan should keep worksheet relationship audit");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "OLE sheetData output plan should preserve worksheet relationships");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        false, "",
        "OLE/control sheetData output plan should classify worksheet relationships as metadata");
    const auto* output_worksheet_relationships_plan =
        find_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels");
    check(output_worksheet_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "OLE/control sheetData output plan should classify worksheet relationships metadata");
    check(output_worksheet_relationships_plan->owner_part == worksheet_part.value(),
        "OLE/control sheetData output plan should keep worksheet relationships owner context");
    check(find_output_entry_plan(output_plan.entries, "xl/calcChain.xml") == nullptr,
        "OLE/control sheetData output plan should not invent calcChain output");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string expected_worksheet =
        std::string(
            R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
            R"(<dimension ref="A1:B2"/>)")
        + replacement_sheet_data
        + R"(<oleObjects><oleObject progId="Forms.CommandButton.1" r:id="rIdOle"/></oleObjects>)"
          R"(<controls><control shapeId="1" r:id="rIdControl"/></controls>)"
          R"(</worksheet>)";
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == expected_worksheet,
        "OLE sheetData replacement should preserve OLE metadata around sheetData");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == worksheet_relationships,
        "OLE sheetData replacement should byte-preserve worksheet relationships");
    check(output_reader.read_entry("xl/embeddings/oleObject1.bin") == ole_object,
        "OLE sheetData replacement should byte-preserve worksheet-owned OLE payload");
    check(output_reader.read_entry("xl/ctrlProps/control1.xml") == control_properties,
        "control sheetData replacement should byte-preserve worksheet-owned control properties");

    const auto* output_relationships = output_reader.relationships_for(worksheet_part);
    check(output_relationships != nullptr,
        "OLE sheetData replacement should keep worksheet relationships readable");
    const auto* output_ole_relationship = output_relationships->find_by_id("rIdOle");
    check(output_ole_relationship != nullptr,
        "OLE sheetData replacement should keep OLE relationship readable");
    check(output_ole_relationship->target == "../embeddings/oleObject1.bin",
        "OLE sheetData replacement should preserve OLE relationship target");
    check(output_ole_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "OLE sheetData replacement should keep OLE relationship internal");
    const auto* output_control_relationship = output_relationships->find_by_id("rIdControl");
    check(output_control_relationship != nullptr,
        "control sheetData replacement should keep control relationship readable");
    check(output_control_relationship->target == "../ctrlProps/control1.xml",
        "control sheetData replacement should preserve control relationship target");
    check(output_control_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "control sheetData replacement should keep control relationship internal");
    check(output_reader.content_types().override_for(ole_part) != nullptr,
        "OLE sheetData replacement should preserve OLE content type override");
    check(output_reader.content_types().override_for(control_part) != nullptr,
        "control sheetData replacement should preserve control content type override");
}

void test_package_editor_removes_worksheet_owned_object_parts_with_inbound_audit()
{
    const std::filesystem::path source =
        output_path("fastxlsx-package-editor-remove-worksheet-objects-source.xlsx");
    const std::filesystem::path ole_output =
        output_path("fastxlsx-package-editor-remove-worksheet-ole-output.xlsx");
    const std::filesystem::path control_output =
        output_path("fastxlsx-package-editor-remove-worksheet-control-output.xlsx");

    const std::string content_types =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/embeddings/oleObject1.bin" ContentType="application/vnd.openxmlformats-officedocument.oleObject"/>)"
        R"(<Override PartName="/xl/ctrlProps/control1.xml" ContentType="application/vnd.ms-excel.controlproperties+xml"/>)"
        R"(</Types>)";
    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    const std::string worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<dimension ref="A1:B2"/>)"
        R"(<sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData>)"
        R"(<oleObjects><oleObject progId="Forms.CommandButton.1" r:id="rIdOle"/></oleObjects>)"
        R"(<controls><control shapeId="1" r:id="rIdControl"/></controls>)"
        R"(</worksheet>)";
    const std::string worksheet_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdOle" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/oleObject" Target="../embeddings/oleObject1.bin"/>)"
        R"(<Relationship Id="rIdControl" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/control" Target="../ctrlProps/control1.xml"/>)"
        R"(</Relationships>)";
    const std::string ole_object = std::string("OLE\0OBJECT\0opaque", 17);
    const std::string control_properties =
        R"(<controlPr xmlns="http://schemas.microsoft.com/office/spreadsheetml/2010/11/main" shapeId="1"/>)";

    fastxlsx::detail::write_package(source,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", workbook},
            {"xl/_rels/workbook.xml.rels", workbook_relationships},
            {"xl/worksheets/sheet1.xml", worksheet},
            {"xl/worksheets/_rels/sheet1.xml.rels", worksheet_relationships},
            {"xl/embeddings/oleObject1.bin", ole_object},
            {"xl/ctrlProps/control1.xml", control_properties},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName ole_part("/xl/embeddings/oleObject1.bin");
    const fastxlsx::detail::PartName control_part("/xl/ctrlProps/control1.xml");

    {
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source);

        editor.remove_part(ole_part, "explicit worksheet-owned OLE object removal");

        check(editor.edit_plan().find_part(ole_part) == nullptr,
            "worksheet-owned OLE removal should clear the active edit-plan part");
        const auto* removed_ole = editor.edit_plan().find_removed_part(ole_part);
        check(removed_ole != nullptr,
            "worksheet-owned OLE removal should record removed-part audit");
        check(removed_ole->reason.find("OLE object") != std::string::npos,
            "worksheet-owned OLE removal should retain the removal reason");
        check(removed_ole->reason.find("inbound relationship preserved")
                != std::string::npos,
            "worksheet-owned OLE removal should audit preserved inbound relationships");
        check(removed_ole->reason.find("/xl/worksheets/sheet1.xml")
                != std::string::npos,
            "worksheet-owned OLE removal inbound audit should include owner part");
        check(removed_ole->reason.find("rIdOle") != std::string::npos,
            "worksheet-owned OLE removal inbound audit should include relationship id");
        check(removed_ole->reason.find("../embeddings/oleObject1.bin")
                != std::string::npos,
            "worksheet-owned OLE removal inbound audit should include original target");
        check(removed_ole->inbound_relationships.size() == 1,
            "worksheet-owned OLE removal should keep structured inbound audit");
        const auto& ole_inbound = removed_ole->inbound_relationships.front();
        check(ole_inbound.owner_part == worksheet_part.value(),
            "worksheet-owned OLE removal should keep inbound owner part");
        check(ole_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
            "worksheet-owned OLE removal should keep inbound owner relationship entry");
        check(ole_inbound.relationship_id == "rIdOle",
            "worksheet-owned OLE removal should keep inbound relationship id");
        check(ole_inbound.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/oleObject",
            "worksheet-owned OLE removal should keep inbound relationship type");
        check(ole_inbound.relationship_target == "../embeddings/oleObject1.bin",
            "worksheet-owned OLE removal should keep inbound raw target");
        check(ole_inbound.target_part == ole_part,
            "worksheet-owned OLE removal should keep normalized target part");
        check(editor.manifest().find_part(ole_part) == nullptr,
            "worksheet-owned OLE removal should remove the part from the manifest");
        check(editor.manifest().content_types().override_for(ole_part) == nullptr,
            "worksheet-owned OLE removal should remove the OLE content type override");
        check(editor.manifest().content_types().override_for(control_part) != nullptr,
            "worksheet-owned OLE removal should keep control content type override");
        const auto* content_types_entry =
            editor.edit_plan().find_package_entry("[Content_Types].xml");
        check(content_types_entry != nullptr,
            "worksheet-owned OLE removal should record content types rewrite");
        check(content_types_entry->write_mode
                == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
            "worksheet-owned OLE removal content types rewrite should be local-DOM-rewrite");
        check(content_types_entry->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
            "worksheet-owned OLE removal content types audit should keep structured role");
        check(editor.edit_plan().find_removed_package_entry(
                  "xl/embeddings/_rels/oleObject1.bin.rels") == nullptr,
            "worksheet-owned OLE removal should not invent missing owner relationships omission");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check(output_plan.relationship_target_audits.empty(),
            "worksheet-owned OLE removal output plan should not invent target audits");
        check_output_entry_plan(output_plan.entries, "xl/embeddings/oleObject1.bin",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
            "worksheet-owned OLE removal output plan should omit OLE part");
        check_output_entry_has_inbound_relationship(output_plan.entries,
            "xl/embeddings/oleObject1.bin", worksheet_part.value(),
            "xl/worksheets/_rels/sheet1.xml.rels", "rIdOle",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/oleObject",
            "../embeddings/oleObject1.bin", ole_part,
            "worksheet-owned OLE removal output plan should keep worksheet inbound audit");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
            "worksheet-owned OLE removal output plan should rewrite content types");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "worksheet-owned OLE removal output plan should preserve worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/ctrlProps/control1.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "worksheet-owned OLE removal output plan should preserve control properties");

        editor.save_as(ole_output);

        const auto entries = fastxlsx::test::read_zip_entries(ole_output);
        check(entries.find("xl/embeddings/oleObject1.bin") == entries.end(),
            "worksheet-owned OLE removal output should omit OLE part");
        check(entries.find("xl/embeddings/_rels/oleObject1.bin.rels") == entries.end(),
            "worksheet-owned OLE removal output should not invent owner relationships");

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(ole_output);
        check(output_reader.content_types().override_for(ole_part) == nullptr,
            "worksheet-owned OLE removal output should remove OLE content type override");
        const std::string output_content_types =
            output_reader.read_entry("[Content_Types].xml");
        check_not_contains(output_content_types, "/xl/embeddings/oleObject1.bin",
            "worksheet-owned OLE removal content types XML should omit OLE override");
        check(output_reader.content_types().override_for(control_part) != nullptr,
            "worksheet-owned OLE removal output should keep control content type override");
        check(output_reader.read_entry("_rels/.rels") == package_relationships,
            "worksheet-owned OLE removal should preserve package relationships bytes");
        check(output_reader.read_entry("xl/workbook.xml") == workbook,
            "worksheet-owned OLE removal should preserve workbook bytes");
        check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
                == workbook_relationships,
            "worksheet-owned OLE removal should preserve workbook relationships bytes");
        check(output_reader.read_entry("xl/worksheets/sheet1.xml") == worksheet,
            "worksheet-owned OLE removal should preserve worksheet bytes");
        check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == worksheet_relationships,
            "worksheet-owned OLE removal should not prune worksheet relationships");
        check(output_reader.read_entry("xl/ctrlProps/control1.xml")
                == control_properties,
            "worksheet-owned OLE removal should preserve control properties bytes");
        check(output_reader.relationships_for(ole_part) == nullptr,
            "worksheet-owned OLE removal should not create owner relationships for absent OLE");

        const auto* output_relationships = output_reader.relationships_for(worksheet_part);
        check(output_relationships != nullptr,
            "worksheet-owned OLE removal should keep worksheet relationships readable");
        const auto* ole_link = output_relationships->find_by_id("rIdOle");
        check(ole_link != nullptr,
            "worksheet-owned OLE removal should keep inbound OLE relationship id");
        check(ole_link->target == "../embeddings/oleObject1.bin",
            "worksheet-owned OLE removal should not rewrite inbound OLE target");
        check(ole_link->target_mode
                == fastxlsx::detail::Relationship::TargetMode::Internal,
            "worksheet-owned OLE removal should keep inbound OLE target mode");
        check(output_relationships->find_by_id("rIdControl") != nullptr,
            "worksheet-owned OLE removal should keep control relationship");
    }

    {
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source);

        editor.remove_part(control_part, "explicit worksheet-owned control property removal");

        check(editor.edit_plan().find_part(control_part) == nullptr,
            "worksheet-owned control removal should clear the active edit-plan part");
        const auto* removed_control =
            editor.edit_plan().find_removed_part(control_part);
        check(removed_control != nullptr,
            "worksheet-owned control removal should record removed-part audit");
        check(removed_control->reason.find("control property") != std::string::npos,
            "worksheet-owned control removal should retain the removal reason");
        check(removed_control->reason.find("inbound relationship preserved")
                != std::string::npos,
            "worksheet-owned control removal should audit preserved inbound relationships");
        check(removed_control->reason.find("/xl/worksheets/sheet1.xml")
                != std::string::npos,
            "worksheet-owned control removal inbound audit should include owner part");
        check(removed_control->reason.find("rIdControl") != std::string::npos,
            "worksheet-owned control removal inbound audit should include relationship id");
        check(removed_control->reason.find("../ctrlProps/control1.xml")
                != std::string::npos,
            "worksheet-owned control removal inbound audit should include original target");
        check(removed_control->inbound_relationships.size() == 1,
            "worksheet-owned control removal should keep structured inbound audit");
        const auto& control_inbound = removed_control->inbound_relationships.front();
        check(control_inbound.owner_part == worksheet_part.value(),
            "worksheet-owned control removal should keep inbound owner part");
        check(control_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
            "worksheet-owned control removal should keep inbound owner relationship entry");
        check(control_inbound.relationship_id == "rIdControl",
            "worksheet-owned control removal should keep inbound relationship id");
        check(control_inbound.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/control",
            "worksheet-owned control removal should keep inbound relationship type");
        check(control_inbound.relationship_target == "../ctrlProps/control1.xml",
            "worksheet-owned control removal should keep inbound raw target");
        check(control_inbound.target_part == control_part,
            "worksheet-owned control removal should keep normalized target part");
        check(editor.manifest().find_part(control_part) == nullptr,
            "worksheet-owned control removal should remove the part from the manifest");
        check(editor.manifest().content_types().override_for(control_part) == nullptr,
            "worksheet-owned control removal should remove the control content type override");
        check(editor.manifest().content_types().override_for(ole_part) != nullptr,
            "worksheet-owned control removal should keep OLE content type override");
        const auto* content_types_entry =
            editor.edit_plan().find_package_entry("[Content_Types].xml");
        check(content_types_entry != nullptr,
            "worksheet-owned control removal should record content types rewrite");
        check(content_types_entry->write_mode
                == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
            "worksheet-owned control removal content types rewrite should be local-DOM-rewrite");
        check(content_types_entry->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
            "worksheet-owned control removal content types audit should keep structured role");
        check(editor.edit_plan().find_removed_package_entry(
                  "xl/ctrlProps/_rels/control1.xml.rels") == nullptr,
            "worksheet-owned control removal should not invent missing owner relationships omission");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check(output_plan.relationship_target_audits.empty(),
            "worksheet-owned control removal output plan should not invent target audits");
        check_output_entry_plan(output_plan.entries, "xl/ctrlProps/control1.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
            "worksheet-owned control removal output plan should omit control properties");
        check_output_entry_has_inbound_relationship(output_plan.entries,
            "xl/ctrlProps/control1.xml", worksheet_part.value(),
            "xl/worksheets/_rels/sheet1.xml.rels", "rIdControl",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/control",
            "../ctrlProps/control1.xml", control_part,
            "worksheet-owned control removal output plan should keep worksheet inbound audit");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
            "worksheet-owned control removal output plan should rewrite content types");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "worksheet-owned control removal output plan should preserve worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/embeddings/oleObject1.bin",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "worksheet-owned control removal output plan should preserve OLE part");

        editor.save_as(control_output);

        const auto entries = fastxlsx::test::read_zip_entries(control_output);
        check(entries.find("xl/ctrlProps/control1.xml") == entries.end(),
            "worksheet-owned control removal output should omit control properties");
        check(entries.find("xl/ctrlProps/_rels/control1.xml.rels") == entries.end(),
            "worksheet-owned control removal output should not invent owner relationships");

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(control_output);
        check(output_reader.content_types().override_for(control_part) == nullptr,
            "worksheet-owned control removal output should remove control content type override");
        const std::string output_content_types =
            output_reader.read_entry("[Content_Types].xml");
        check_not_contains(output_content_types, "/xl/ctrlProps/control1.xml",
            "worksheet-owned control removal content types XML should omit control override");
        check(output_reader.content_types().override_for(ole_part) != nullptr,
            "worksheet-owned control removal output should keep OLE content type override");
        check(output_reader.read_entry("_rels/.rels") == package_relationships,
            "worksheet-owned control removal should preserve package relationships bytes");
        check(output_reader.read_entry("xl/workbook.xml") == workbook,
            "worksheet-owned control removal should preserve workbook bytes");
        check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
                == workbook_relationships,
            "worksheet-owned control removal should preserve workbook relationships bytes");
        check(output_reader.read_entry("xl/worksheets/sheet1.xml") == worksheet,
            "worksheet-owned control removal should preserve worksheet bytes");
        check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == worksheet_relationships,
            "worksheet-owned control removal should not prune worksheet relationships");
        check(output_reader.read_entry("xl/embeddings/oleObject1.bin") == ole_object,
            "worksheet-owned control removal should preserve OLE bytes");
        check(output_reader.relationships_for(control_part) == nullptr,
            "worksheet-owned control removal should not create owner relationships for absent control");

        const auto* output_relationships = output_reader.relationships_for(worksheet_part);
        check(output_relationships != nullptr,
            "worksheet-owned control removal should keep worksheet relationships readable");
        const auto* control_link = output_relationships->find_by_id("rIdControl");
        check(control_link != nullptr,
            "worksheet-owned control removal should keep inbound control relationship id");
        check(control_link->target == "../ctrlProps/control1.xml",
            "worksheet-owned control removal should not rewrite inbound control target");
        check(control_link->target_mode
                == fastxlsx::detail::Relationship::TargetMode::Internal,
            "worksheet-owned control removal should keep inbound control target mode");
        check(output_relationships->find_by_id("rIdOle") != nullptr,
            "worksheet-owned control removal should keep OLE relationship");
    }
}

void test_package_editor_worksheet_owned_object_part_same_path_ordering()
{
    const std::filesystem::path source =
        output_path("fastxlsx-package-editor-worksheet-object-order-source.xlsx");
    const std::filesystem::path ole_restore_output =
        output_path("fastxlsx-package-editor-replace-after-remove-worksheet-ole-output.xlsx");
    const std::filesystem::path control_remove_output =
        output_path("fastxlsx-package-editor-remove-after-replace-worksheet-control-output.xlsx");

    const std::string content_types =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/embeddings/oleObject1.bin" ContentType="application/vnd.openxmlformats-officedocument.oleObject"/>)"
        R"(<Override PartName="/xl/ctrlProps/control1.xml" ContentType="application/vnd.ms-excel.controlproperties+xml"/>)"
        R"(</Types>)";
    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    const std::string worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<dimension ref="A1:B2"/>)"
        R"(<sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData>)"
        R"(<oleObjects><oleObject progId="Forms.CommandButton.1" r:id="rIdOle"/></oleObjects>)"
        R"(<controls><control shapeId="1" r:id="rIdControl"/></controls>)"
        R"(</worksheet>)";
    const std::string worksheet_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdOle" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/oleObject" Target="../embeddings/oleObject1.bin"/>)"
        R"(<Relationship Id="rIdControl" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/control" Target="../ctrlProps/control1.xml"/>)"
        R"(</Relationships>)";
    const std::string ole_object = std::string("OLE\0OBJECT\0opaque", 17);
    const std::string control_properties =
        R"(<controlPr xmlns="http://schemas.microsoft.com/office/spreadsheetml/2010/11/main" shapeId="1"/>)";

    fastxlsx::detail::write_package(source,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", workbook},
            {"xl/_rels/workbook.xml.rels", workbook_relationships},
            {"xl/worksheets/sheet1.xml", worksheet},
            {"xl/worksheets/_rels/sheet1.xml.rels", worksheet_relationships},
            {"xl/embeddings/oleObject1.bin", ole_object},
            {"xl/ctrlProps/control1.xml", control_properties},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName ole_part("/xl/embeddings/oleObject1.bin");
    const fastxlsx::detail::PartName control_part("/xl/ctrlProps/control1.xml");

    {
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source);

        editor.remove_part(ole_part, "temporary worksheet-owned OLE removal");
        check(editor.edit_plan().find_removed_part(ole_part) != nullptr,
            "worksheet-owned OLE restore setup should record removed-part audit");
        check(editor.manifest().find_part(ole_part) == nullptr,
            "worksheet-owned OLE restore setup should remove the manifest part");
        check(editor.manifest().content_types().override_for(ole_part) == nullptr,
            "worksheet-owned OLE restore setup should remove the content type override");

        const std::string restored_ole = std::string("RESTORED\0OLE", 12);
        replace_part_with_memory_chunks(editor, ole_part, restored_ole,
            "restored worksheet-owned OLE after removal");

        check(editor.edit_plan().find_removed_part(ole_part) == nullptr,
            "worksheet-owned OLE replacement after removal should clear stale removed-part audit");
        check(editor.edit_plan().find_removed_package_entry(
                  "xl/embeddings/_rels/oleObject1.bin.rels") == nullptr,
            "worksheet-owned OLE replacement after removal should not invent owner relationships omission");
        check(editor.edit_plan().removed_parts().empty(),
            "worksheet-owned OLE replacement after removal should leave no removed parts");
        const auto* ole_plan = editor.edit_plan().find_part(ole_part);
        check(ole_plan != nullptr,
            "worksheet-owned OLE replacement after removal should restore active edit-plan part");
        check(ole_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
            "worksheet-owned OLE replacement after removal should keep final write mode");
        check(ole_plan->reason.find("after removal") != std::string::npos,
            "worksheet-owned OLE replacement after removal should keep final reason");
        check_manifest_write_mode(editor, ole_part,
            fastxlsx::detail::PartWriteMode::StreamRewrite,
            "worksheet-owned OLE replacement after removal should restore manifest write mode");
        check(editor.manifest().content_types().override_for(ole_part) != nullptr,
            "worksheet-owned OLE replacement after removal should restore the OLE content type override");
        const auto* restored_content_types =
            editor.edit_plan().find_package_entry("[Content_Types].xml");
        check(restored_content_types != nullptr,
            "worksheet-owned OLE replacement after removal should keep content types audit");
        check(restored_content_types->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "worksheet-owned OLE replacement after removal should restore content types copy-original audit");
        check(restored_content_types->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
            "worksheet-owned OLE replacement after removal should keep content types audit role");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check(!output_plan.full_calculation_on_load,
            "worksheet-owned OLE replacement after removal output plan should not request full calculation");
        check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
            "worksheet-owned OLE replacement after removal output plan should preserve calcChain policy");
        check(output_plan.relationship_target_audits.empty(),
            "worksheet-owned OLE replacement after removal output plan should not invent relationship audits");
        check(output_plan.removed_parts.empty(),
            "worksheet-owned OLE replacement after removal output plan should clear removed parts");
        check(output_plan.removed_package_entries.empty(),
            "worksheet-owned OLE replacement after removal output plan should clear removed package entries");
        check_output_entry_plan(output_plan.entries, "xl/embeddings/oleObject1.bin",
            fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
            "worksheet-owned OLE replacement after removal output plan should rewrite OLE part");
        check_output_entry_part_context(output_plan.entries,
            "xl/embeddings/oleObject1.bin", true, ole_part.value(),
            "worksheet-owned OLE replacement after removal output plan should classify OLE part");
        const auto* output_ole_plan =
            find_output_entry_plan(output_plan.entries, "xl/embeddings/oleObject1.bin");
        check(output_ole_plan->reason.find("after removal") != std::string::npos,
            "worksheet-owned OLE replacement after removal output plan should keep reason");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "worksheet-owned OLE replacement after removal output plan should preserve content types");
        check_output_entry_part_context(output_plan.entries, "[Content_Types].xml",
            false, "",
            "worksheet-owned OLE replacement after removal output plan should classify content types as metadata");
        const auto* output_content_types_plan =
            find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
        check(output_content_types_plan->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
            "worksheet-owned OLE replacement after removal output plan should keep content types audit role");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "worksheet-owned OLE replacement after removal output plan should preserve worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/ctrlProps/control1.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "worksheet-owned OLE replacement after removal output plan should preserve sibling control properties");
        check(find_output_entry_plan(output_plan.entries,
                  "xl/embeddings/_rels/oleObject1.bin.rels") == nullptr,
            "worksheet-owned OLE replacement after removal output plan should not invent owner relationships");

        editor.save_as(ole_restore_output);

        const auto entries = fastxlsx::test::read_zip_entries(ole_restore_output);
        check(entries.find("xl/embeddings/oleObject1.bin") != entries.end(),
            "worksheet-owned OLE replacement after removal output should restore OLE part");
        check(entries.find("xl/embeddings/_rels/oleObject1.bin.rels") == entries.end(),
            "worksheet-owned OLE replacement after removal output should not invent owner relationships");

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(ole_restore_output);
        check_preserved_source_entries(editor.reader(), output_reader, ole_part.zip_path());
        check(output_reader.read_entry("xl/embeddings/oleObject1.bin") == restored_ole,
            "worksheet-owned OLE replacement after removal should write restored bytes");
        check(output_reader.read_entry("[Content_Types].xml") == content_types,
            "worksheet-owned OLE replacement after removal should restore source content types bytes");
        check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == worksheet_relationships,
            "worksheet-owned OLE replacement after removal should not prune worksheet relationships");
        check(output_reader.content_types().override_for(ole_part) != nullptr,
            "worksheet-owned OLE replacement after removal should restore OLE content type override");
        check(output_reader.relationships_for(ole_part) == nullptr,
            "worksheet-owned OLE replacement after removal should not create owner relationships");
    }

    {
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source);

        const std::string replacement_control =
            R"(<controlPr xmlns="http://schemas.microsoft.com/office/spreadsheetml/2010/11/main" shapeId="2"/>)";
        replace_part_with_memory_chunks(editor, control_part, replacement_control,
            "queued worksheet-owned control replacement");
        const auto* prior_control_plan = editor.edit_plan().find_part(control_part);
        check(prior_control_plan != nullptr,
            "worksheet-owned control removal-after-replacement setup should queue replacement");
        check(prior_control_plan->write_mode
                == fastxlsx::detail::PartWriteMode::StreamRewrite,
            "worksheet-owned control removal-after-replacement setup should local-DOM-rewrite control");
        check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
            "worksheet-owned control replacement setup should not rewrite content types");

        editor.remove_part(control_part,
            "final worksheet-owned control removal after replacement");

        check(editor.edit_plan().find_part(control_part) == nullptr,
            "worksheet-owned control removal after replacement should clear active replacement");
        const auto* removed_control =
            editor.edit_plan().find_removed_part(control_part);
        check(removed_control != nullptr,
            "worksheet-owned control removal after replacement should record removed-part audit");
        check(removed_control->reason.find("after replacement") != std::string::npos,
            "worksheet-owned control removal after replacement should keep final reason");
        check(removed_control->reason.find("inbound relationship preserved")
                != std::string::npos,
            "worksheet-owned control removal after replacement should audit preserved inbound relationship");
        check(removed_control->inbound_relationships.size() == 1,
            "worksheet-owned control removal after replacement should keep structured inbound audit");
        const auto& control_inbound = removed_control->inbound_relationships.front();
        check(control_inbound.owner_part == worksheet_part.value(),
            "worksheet-owned control removal after replacement should keep inbound owner part");
        check(control_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
            "worksheet-owned control removal after replacement should keep inbound owner entry");
        check(control_inbound.relationship_id == "rIdControl",
            "worksheet-owned control removal after replacement should keep inbound relationship id");
        check(control_inbound.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/control",
            "worksheet-owned control removal after replacement should keep inbound relationship type");
        check(control_inbound.relationship_target == "../ctrlProps/control1.xml",
            "worksheet-owned control removal after replacement should keep inbound raw target");
        check(control_inbound.target_part == control_part,
            "worksheet-owned control removal after replacement should keep normalized inbound target");
        check(editor.manifest().find_part(control_part) == nullptr,
            "worksheet-owned control removal after replacement should remove manifest part");
        check(editor.manifest().content_types().override_for(control_part) == nullptr,
            "worksheet-owned control removal after replacement should remove content type override");
        check(editor.manifest().content_types().override_for(ole_part) != nullptr,
            "worksheet-owned control removal after replacement should keep sibling OLE content type");
        const auto* content_types_entry =
            editor.edit_plan().find_package_entry("[Content_Types].xml");
        check(content_types_entry != nullptr,
            "worksheet-owned control removal after replacement should record content types rewrite");
        check(content_types_entry->write_mode
                == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
            "worksheet-owned control removal after replacement content types audit should be local-DOM-rewrite");
        check(content_types_entry->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
            "worksheet-owned control removal after replacement content types audit should keep structured role");
        check(editor.edit_plan().find_removed_package_entry(
                  "xl/ctrlProps/_rels/control1.xml.rels") == nullptr,
            "worksheet-owned control removal after replacement should not invent owner relationships omission");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check(!output_plan.full_calculation_on_load,
            "worksheet-owned control removal after replacement output plan should not request full calculation");
        check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
            "worksheet-owned control removal after replacement output plan should preserve calcChain policy");
        check(output_plan.relationship_target_audits.empty(),
            "worksheet-owned control removal after replacement output plan should not invent target audits");
        check(output_plan.removed_parts.size() == 1,
            "worksheet-owned control removal after replacement output plan should expose one removed part");
        check(output_plan.removed_parts.front().part_name == control_part,
            "worksheet-owned control removal after replacement output plan should expose removed control part");
        check(output_plan.removed_parts.front().reason.find("after replacement")
                != std::string::npos,
            "worksheet-owned control removal after replacement output plan should keep removed-part reason");
        check(output_plan.removed_parts.front().inbound_relationships.size() == 1,
            "worksheet-owned control removal after replacement output plan should keep removed-part inbound audit");
        check(output_plan.removed_package_entries.empty(),
            "worksheet-owned control removal after replacement output plan should not omit metadata entries");
        check_output_entry_plan(output_plan.entries, "xl/ctrlProps/control1.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
            "worksheet-owned control removal after replacement output plan should omit control part");
        check_output_entry_part_context(output_plan.entries, "xl/ctrlProps/control1.xml",
            true, control_part.value(),
            "worksheet-owned control removal after replacement output plan should classify omitted control part");
        const auto* output_control_plan =
            find_output_entry_plan(output_plan.entries, "xl/ctrlProps/control1.xml");
        check(output_control_plan->reason.find("after replacement") != std::string::npos,
            "worksheet-owned control removal after replacement output plan should keep final removal reason");
        check_output_entry_has_inbound_relationship(output_plan.entries,
            "xl/ctrlProps/control1.xml", worksheet_part.value(),
            "xl/worksheets/_rels/sheet1.xml.rels", "rIdControl",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/control",
            "../ctrlProps/control1.xml", control_part,
            "worksheet-owned control removal after replacement output plan should keep inbound audit");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
            "worksheet-owned control removal after replacement output plan should rewrite content types");
        check_output_entry_part_context(output_plan.entries, "[Content_Types].xml",
            false, "",
            "worksheet-owned control removal after replacement output plan should classify content types as metadata");
        const auto* output_content_types_plan =
            find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
        check(output_content_types_plan->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
            "worksheet-owned control removal after replacement output plan should keep content types audit role");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "worksheet-owned control removal after replacement output plan should preserve worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/embeddings/oleObject1.bin",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "worksheet-owned control removal after replacement output plan should preserve sibling OLE part");
        check(find_output_entry_plan(output_plan.entries,
                  "xl/ctrlProps/_rels/control1.xml.rels") == nullptr,
            "worksheet-owned control removal after replacement output plan should not invent owner relationships");

        editor.save_as(control_remove_output);

        const auto entries = fastxlsx::test::read_zip_entries(control_remove_output);
        check(entries.find("xl/ctrlProps/control1.xml") == entries.end(),
            "worksheet-owned control removal after replacement output should omit control part");
        check(entries.find("xl/ctrlProps/_rels/control1.xml.rels") == entries.end(),
            "worksheet-owned control removal after replacement output should not invent owner relationships");

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(control_remove_output);
        check(output_reader.content_types().override_for(control_part) == nullptr,
            "worksheet-owned control removal after replacement output should remove control content type override");
        check_not_contains(output_reader.read_entry("[Content_Types].xml"),
            "/xl/ctrlProps/control1.xml",
            "worksheet-owned control removal after replacement content types should omit control override");
        check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == worksheet_relationships,
            "worksheet-owned control removal after replacement should not prune worksheet relationships");
        check(output_reader.read_entry("xl/embeddings/oleObject1.bin") == ole_object,
            "worksheet-owned control removal after replacement should preserve sibling OLE bytes");
        check(output_reader.relationships_for(control_part) == nullptr,
            "worksheet-owned control removal after replacement should not create owner relationships");
        const auto* output_relationships =
            output_reader.relationships_for(worksheet_part);
        check(output_relationships != nullptr,
            "worksheet-owned control removal after replacement should keep worksheet relationships readable");
        const auto* control_link = output_relationships->find_by_id("rIdControl");
        check(control_link != nullptr,
            "worksheet-owned control removal after replacement should keep inbound relationship id");
        check(control_link->target == "../ctrlProps/control1.xml",
            "worksheet-owned control removal after replacement should not rewrite inbound target");
    }

    {
        const std::filesystem::path output =
            output_path("fastxlsx-package-editor-repeat-worksheet-ole-output.xlsx");
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source);

        const std::string stale_ole = std::string("STALE\0OLE", 9);
        const std::string final_ole = std::string("FINAL\0OLE", 9);
        replace_part_with_memory_chunks(editor, ole_part, stale_ole,
            "stale repeated worksheet-owned OLE replacement");
        replace_part_with_memory_chunks(editor, ole_part, final_ole,
            "final repeated worksheet-owned OLE replacement");

        const auto* ole_plan = editor.edit_plan().find_part(ole_part);
        check(ole_plan != nullptr,
            "repeated worksheet-owned OLE replacement should keep an active edit-plan part");
        check(ole_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
            "repeated worksheet-owned OLE replacement should keep final write mode");
        check(ole_plan->reason.find("final repeated") != std::string::npos,
            "repeated worksheet-owned OLE replacement should keep final reason");
        check(ole_plan->reason.find("stale repeated") == std::string::npos,
            "repeated worksheet-owned OLE replacement should drop stale reason");
        check_manifest_write_mode(editor, ole_part,
            fastxlsx::detail::PartWriteMode::StreamRewrite,
            "repeated worksheet-owned OLE replacement should mirror final write mode into manifest");
        check(editor.manifest().content_types().override_for(ole_part) != nullptr,
            "repeated worksheet-owned OLE replacement should keep OLE content type override");
        check(editor.edit_plan().find_removed_part(ole_part) == nullptr,
            "repeated worksheet-owned OLE replacement should not leave removed-part audit");
        check(editor.edit_plan().find_removed_package_entry(
                  "xl/embeddings/_rels/oleObject1.bin.rels") == nullptr,
            "repeated worksheet-owned OLE replacement should not leave owner relationships omission");
        check(editor.edit_plan().find_package_entry(
                  "xl/embeddings/_rels/oleObject1.bin.rels") == nullptr,
            "repeated worksheet-owned OLE replacement should not invent owner relationships audit");
        check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
            "repeated worksheet-owned OLE replacement should not rewrite content types audit");
        check(editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == nullptr,
            "repeated worksheet-owned OLE replacement should not rewrite inbound worksheet relationships");
        check(editor.edit_plan().find_part(control_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "repeated worksheet-owned OLE replacement should keep sibling control copy-original");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check(!output_plan.full_calculation_on_load,
            "repeated worksheet-owned OLE replacement output plan should not request full calculation");
        check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
            "repeated worksheet-owned OLE replacement output plan should preserve calcChain policy");
        check(output_plan.relationship_target_audits.empty(),
            "repeated worksheet-owned OLE replacement output plan should not invent target audits");
        check(output_plan.removed_parts.empty(),
            "repeated worksheet-owned OLE replacement output plan should not expose removed parts");
        check(output_plan.removed_package_entries.empty(),
            "repeated worksheet-owned OLE replacement output plan should not omit metadata entries");
        check_output_entry_plan(output_plan.entries, "xl/embeddings/oleObject1.bin",
            fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
            "repeated worksheet-owned OLE replacement output plan should rewrite OLE part");
        const auto* output_ole_plan =
            find_output_entry_plan(output_plan.entries, "xl/embeddings/oleObject1.bin");
        check(output_ole_plan->reason.find("final repeated") != std::string::npos,
            "repeated worksheet-owned OLE replacement output plan should keep final reason");
        check(output_ole_plan->reason.find("stale repeated") == std::string::npos,
            "repeated worksheet-owned OLE replacement output plan should drop stale reason");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "repeated worksheet-owned OLE replacement output plan should preserve content types");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "repeated worksheet-owned OLE replacement output plan should preserve worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/ctrlProps/control1.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "repeated worksheet-owned OLE replacement output plan should preserve sibling control");
        check(find_output_entry_plan(output_plan.entries,
                  "xl/embeddings/_rels/oleObject1.bin.rels") == nullptr,
            "repeated worksheet-owned OLE replacement output plan should not invent owner relationships");

        editor.save_as(output);

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(output);
        check_preserved_source_entries(editor.reader(), output_reader, ole_part.zip_path());
        check(output_reader.read_entry("xl/embeddings/oleObject1.bin") == final_ole,
            "repeated worksheet-owned OLE replacement should write final bytes");
        check(output_reader.read_entry("xl/embeddings/oleObject1.bin") != stale_ole,
            "repeated worksheet-owned OLE replacement should not write stale bytes");
        check(output_reader.read_entry("[Content_Types].xml") == content_types,
            "repeated worksheet-owned OLE replacement should preserve content types bytes");
        check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == worksheet_relationships,
            "repeated worksheet-owned OLE replacement should not prune worksheet relationships");
        check(output_reader.read_entry("xl/ctrlProps/control1.xml") == control_properties,
            "repeated worksheet-owned OLE replacement should preserve sibling control bytes");
        check(output_reader.relationships_for(ole_part) == nullptr,
            "repeated worksheet-owned OLE replacement should not create owner relationships");
        const auto* output_relationships =
            output_reader.relationships_for(worksheet_part);
        check(output_relationships != nullptr,
            "repeated worksheet-owned OLE replacement should keep worksheet relationships readable");
        const auto* ole_link = output_relationships->find_by_id("rIdOle");
        check(ole_link != nullptr,
            "repeated worksheet-owned OLE replacement should keep inbound relationship id");
        check(ole_link->target == "../embeddings/oleObject1.bin",
            "repeated worksheet-owned OLE replacement should not rewrite inbound target");
    }

    {
        const std::filesystem::path output =
            output_path("fastxlsx-package-editor-repeat-worksheet-control-output.xlsx");
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source);

        const std::string stale_control =
            R"(<controlPr xmlns="http://schemas.microsoft.com/office/spreadsheetml/2010/11/main" shapeId="2"/>)";
        const std::string final_control =
            R"(<controlPr xmlns="http://schemas.microsoft.com/office/spreadsheetml/2010/11/main" shapeId="3"/>)";
        replace_part_with_memory_chunks(editor, control_part, stale_control,
            "stale repeated worksheet-owned control replacement");
        replace_part_with_memory_chunks(editor, control_part, final_control,
            "final repeated worksheet-owned control replacement");

        const auto* control_plan = editor.edit_plan().find_part(control_part);
        check(control_plan != nullptr,
            "repeated worksheet-owned control replacement should keep an active edit-plan part");
        check(control_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
            "repeated worksheet-owned control replacement should keep final write mode");
        check(control_plan->reason.find("final repeated") != std::string::npos,
            "repeated worksheet-owned control replacement should keep final reason");
        check(control_plan->reason.find("stale repeated") == std::string::npos,
            "repeated worksheet-owned control replacement should drop stale reason");
        check_manifest_write_mode(editor, control_part,
            fastxlsx::detail::PartWriteMode::StreamRewrite,
            "repeated worksheet-owned control replacement should mirror final write mode into manifest");
        check(editor.manifest().content_types().override_for(control_part) != nullptr,
            "repeated worksheet-owned control replacement should keep control content type override");
        check(editor.edit_plan().find_removed_part(control_part) == nullptr,
            "repeated worksheet-owned control replacement should not leave removed-part audit");
        check(editor.edit_plan().find_removed_package_entry(
                  "xl/ctrlProps/_rels/control1.xml.rels") == nullptr,
            "repeated worksheet-owned control replacement should not leave owner relationships omission");
        check(editor.edit_plan().find_package_entry(
                  "xl/ctrlProps/_rels/control1.xml.rels") == nullptr,
            "repeated worksheet-owned control replacement should not invent owner relationships audit");
        check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
            "repeated worksheet-owned control replacement should not rewrite content types audit");
        check(editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == nullptr,
            "repeated worksheet-owned control replacement should not rewrite inbound worksheet relationships");
        check(editor.edit_plan().find_part(ole_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "repeated worksheet-owned control replacement should keep sibling OLE copy-original");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check(!output_plan.full_calculation_on_load,
            "repeated worksheet-owned control replacement output plan should not request full calculation");
        check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
            "repeated worksheet-owned control replacement output plan should preserve calcChain policy");
        check(output_plan.relationship_target_audits.empty(),
            "repeated worksheet-owned control replacement output plan should not invent target audits");
        check(output_plan.removed_parts.empty(),
            "repeated worksheet-owned control replacement output plan should not expose removed parts");
        check(output_plan.removed_package_entries.empty(),
            "repeated worksheet-owned control replacement output plan should not omit metadata entries");
        check_output_entry_plan(output_plan.entries, "xl/ctrlProps/control1.xml",
            fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
            "repeated worksheet-owned control replacement output plan should rewrite control part");
        const auto* output_control_plan =
            find_output_entry_plan(output_plan.entries, "xl/ctrlProps/control1.xml");
        check(output_control_plan->reason.find("final repeated") != std::string::npos,
            "repeated worksheet-owned control replacement output plan should keep final reason");
        check(output_control_plan->reason.find("stale repeated") == std::string::npos,
            "repeated worksheet-owned control replacement output plan should drop stale reason");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "repeated worksheet-owned control replacement output plan should preserve content types");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "repeated worksheet-owned control replacement output plan should preserve worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/embeddings/oleObject1.bin",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "repeated worksheet-owned control replacement output plan should preserve sibling OLE");
        check(find_output_entry_plan(output_plan.entries,
                  "xl/ctrlProps/_rels/control1.xml.rels") == nullptr,
            "repeated worksheet-owned control replacement output plan should not invent owner relationships");

        editor.save_as(output);

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(output);
        check_preserved_source_entries(editor.reader(), output_reader,
            control_part.zip_path());
        check(output_reader.read_entry("xl/ctrlProps/control1.xml") == final_control,
            "repeated worksheet-owned control replacement should write final XML");
        check(output_reader.read_entry("xl/ctrlProps/control1.xml") != stale_control,
            "repeated worksheet-owned control replacement should not write stale XML");
        check(output_reader.read_entry("[Content_Types].xml") == content_types,
            "repeated worksheet-owned control replacement should preserve content types bytes");
        check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == worksheet_relationships,
            "repeated worksheet-owned control replacement should not prune worksheet relationships");
        check(output_reader.read_entry("xl/embeddings/oleObject1.bin") == ole_object,
            "repeated worksheet-owned control replacement should preserve sibling OLE bytes");
        check(output_reader.relationships_for(control_part) == nullptr,
            "repeated worksheet-owned control replacement should not create owner relationships");
        const auto* output_relationships =
            output_reader.relationships_for(worksheet_part);
        check(output_relationships != nullptr,
            "repeated worksheet-owned control replacement should keep worksheet relationships readable");
        const auto* control_link = output_relationships->find_by_id("rIdControl");
        check(control_link != nullptr,
            "repeated worksheet-owned control replacement should keep inbound relationship id");
        check(control_link->target == "../ctrlProps/control1.xml",
            "repeated worksheet-owned control replacement should not rewrite inbound target");
    }
}

void test_package_editor_sheet_data_patch_preserves_background_picture_and_header_footer_vml()
{
    const std::filesystem::path source =
        output_path("fastxlsx-package-editor-sheetdata-picture-vml-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-sheetdata-picture-vml-output.xlsx");

    const std::string content_types =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="png" ContentType="image/png"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/drawings/vmlDrawingHF1.vml" ContentType="application/vnd.openxmlformats-officedocument.vmlDrawing"/>)"
        R"(</Types>)";
    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    const std::string worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<dimension ref="A1:B2"/>)"
        R"(<sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData>)"
        R"(<headerFooter><oddHeader>&amp;G</oddHeader></headerFooter>)"
        R"(<picture r:id="rIdPicture"/>)"
        R"(<legacyDrawingHF r:id="rIdHeaderFooter"/>)"
        R"(</worksheet>)";
    const std::string worksheet_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdPicture" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="../media/background.png"/>)"
        R"(<Relationship Id="rIdHeaderFooter" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing" Target="../drawings/vmlDrawingHF1.vml"/>)"
        R"(</Relationships>)";
    const std::string background_picture = "opaque-background-image-bytes";
    const std::string header_footer_vml = R"(<xml><v:shape id="headerPicture"/></xml>)";

    fastxlsx::detail::write_package(source,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", workbook},
            {"xl/_rels/workbook.xml.rels", workbook_relationships},
            {"xl/worksheets/sheet1.xml", worksheet},
            {"xl/worksheets/_rels/sheet1.xml.rels", worksheet_relationships},
            {"xl/media/background.png", background_picture},
            {"xl/drawings/vmlDrawingHF1.vml", header_footer_vml},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName background_picture_part("/xl/media/background.png");
    const fastxlsx::detail::PartName header_footer_vml_part(
        "/xl/drawings/vmlDrawingHF1.vml");

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="2"><c r="A2"><v>2</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_from_single_chunk_source(editor, worksheet_part, replacement_sheet_data);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "picture sheetData replacement should keep worksheet in edit plan");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "picture sheetData replacement should local-DOM-rewrite worksheet");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "picture sheetData replacement should plan workbook calc metadata rewrite");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "picture sheetData replacement should local-DOM-rewrite workbook calc metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "background picture reference metadata",
                  "caller review"}),
        "picture sheetData replacement should audit preserved background picture metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "header/footer drawing reference metadata",
                  "caller review"}),
        "picture sheetData replacement should audit preserved header/footer drawing metadata");

    const auto* picture_plan = editor.edit_plan().find_part(background_picture_part);
    check(picture_plan != nullptr,
        "worksheet-owned background picture part should remain visible in the edit plan");
    check(picture_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet-owned background picture part should remain copy-original");
    check(picture_plan->reason.find("worksheet relationship rIdPicture")
            != std::string::npos
            && picture_plan->reason.find("relationships/image") != std::string::npos
            && picture_plan->reason.find("/xl/media/background.png")
                != std::string::npos,
        "background picture copy reason should come from worksheet relationship traversal");
    check(picture_plan->relationship_owner_part == worksheet_part.value(),
        "background picture audit should keep structured relationship owner");
    check(picture_plan->relationship_id == "rIdPicture",
        "background picture audit should keep structured relationship id");
    check(picture_plan->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image",
        "background picture audit should keep structured relationship type");
    check(picture_plan->relationship_target == "../media/background.png",
        "background picture audit should keep structured relationship target");

    const auto* header_footer_plan = editor.edit_plan().find_part(header_footer_vml_part);
    check(header_footer_plan != nullptr,
        "worksheet-owned header/footer VML part should remain visible in the edit plan");
    check(header_footer_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet-owned header/footer VML part should remain copy-original");
    check(header_footer_plan->reason.find("worksheet relationship rIdHeaderFooter")
            != std::string::npos
            && header_footer_plan->reason.find("relationships/vmlDrawing")
                != std::string::npos
            && header_footer_plan->reason.find("/xl/drawings/vmlDrawingHF1.vml")
                != std::string::npos,
        "header/footer VML copy reason should come from worksheet relationship traversal");
    check(header_footer_plan->relationship_owner_part == worksheet_part.value(),
        "header/footer VML audit should keep structured relationship owner");
    check(header_footer_plan->relationship_id == "rIdHeaderFooter",
        "header/footer VML audit should keep structured relationship id");
    check(header_footer_plan->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing",
        "header/footer VML audit should keep structured relationship type");
    check(header_footer_plan->relationship_target == "../drawings/vmlDrawingHF1.vml",
        "header/footer VML audit should keep structured relationship target");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load,
        "picture/VML sheetData output plan should request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Remove,
        "picture/VML sheetData output plan should expose calcChain removal policy");
    check(output_plan.relationship_target_audits.empty(),
        "picture/VML sheetData output plan should not invent dependency audits");
    check(output_plan.worksheet_relationship_reference_audits.empty(),
        "picture/VML sheetData output plan should not invent relationship-id audits");
    check(output_plan.removed_parts.empty(),
        "picture/VML sheetData output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "picture/VML sheetData output plan should not expose removed package entries");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "background picture reference metadata",
                  "caller review"}),
        "picture/VML sheetData output plan should snapshot preserved picture notes");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "header/footer drawing reference metadata",
                  "caller review"}),
        "picture/VML sheetData output plan should snapshot preserved header/footer VML notes");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "picture/VML sheetData output plan should rewrite worksheet");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/sheet1.xml",
        true, worksheet_part.value(),
        "picture/VML sheetData output plan should classify worksheet as a part");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "picture/VML sheetData output plan should rewrite workbook metadata");
    check_output_entry_part_context(output_plan.entries, "xl/workbook.xml",
        true, workbook_part.value(),
        "picture/VML sheetData output plan should classify workbook as a part");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "picture/VML sheetData output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "picture/VML sheetData output plan should classify content types as metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "picture/VML sheetData output plan should preserve package relationships");
    check_output_entry_part_context(output_plan.entries, "_rels/.rels", false, "",
        "picture/VML sheetData output plan should classify package relationships as metadata");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "picture/VML sheetData output plan should preserve workbook relationships");
    check_output_entry_part_context(output_plan.entries, "xl/_rels/workbook.xml.rels",
        false, "",
        "picture/VML sheetData output plan should classify workbook relationships as metadata");
    check_output_entry_plan(output_plan.entries, "xl/media/background.png",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "picture sheetData output plan should preserve worksheet-owned background picture");
    check_output_entry_part_context(output_plan.entries, "xl/media/background.png",
        true, background_picture_part.value(),
        "picture sheetData output plan should classify background picture as a part");
    check_output_entry_relationship_context(output_plan.entries, "xl/media/background.png",
        worksheet_part.value(), "rIdPicture",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image",
        "../media/background.png",
        "picture sheetData output plan should keep worksheet relationship audit");
    check_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawingHF1.vml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "picture sheetData output plan should preserve worksheet-owned header/footer VML");
    check_output_entry_part_context(output_plan.entries, "xl/drawings/vmlDrawingHF1.vml",
        true, header_footer_vml_part.value(),
        "picture sheetData output plan should classify header/footer VML as a part");
    check_output_entry_relationship_context(output_plan.entries,
        "xl/drawings/vmlDrawingHF1.vml", worksheet_part.value(), "rIdHeaderFooter",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing",
        "../drawings/vmlDrawingHF1.vml",
        "picture sheetData output plan should keep header/footer VML relationship audit");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "picture sheetData output plan should preserve worksheet relationships");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        false, "",
        "picture/VML sheetData output plan should classify worksheet relationships as metadata");
    const auto* output_worksheet_relationships_plan =
        find_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels");
    check(output_worksheet_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "picture/VML sheetData output plan should classify worksheet relationships metadata");
    check(output_worksheet_relationships_plan->owner_part == worksheet_part.value(),
        "picture/VML sheetData output plan should keep worksheet relationships owner context");
    check(find_output_entry_plan(output_plan.entries, "xl/calcChain.xml") == nullptr,
        "picture/VML sheetData output plan should not invent calcChain output");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string expected_worksheet =
        std::string(
            R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
            R"(<dimension ref="A1:B2"/>)")
        + replacement_sheet_data
        + R"(<headerFooter><oddHeader>&amp;G</oddHeader></headerFooter>)"
          R"(<picture r:id="rIdPicture"/>)"
          R"(<legacyDrawingHF r:id="rIdHeaderFooter"/>)"
          R"(</worksheet>)";
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == expected_worksheet,
        "picture sheetData replacement should preserve picture metadata around sheetData");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == worksheet_relationships,
        "picture sheetData replacement should byte-preserve worksheet relationships");
    check(output_reader.read_entry("xl/media/background.png") == background_picture,
        "picture sheetData replacement should byte-preserve background picture");
    check(output_reader.read_entry("xl/drawings/vmlDrawingHF1.vml") == header_footer_vml,
        "picture sheetData replacement should byte-preserve header/footer VML drawing");

    const auto* output_relationships = output_reader.relationships_for(worksheet_part);
    check(output_relationships != nullptr,
        "picture sheetData replacement should keep worksheet relationships readable");
    const auto* output_picture_relationship =
        output_relationships->find_by_id("rIdPicture");
    check(output_picture_relationship != nullptr,
        "picture sheetData replacement should keep background picture relationship readable");
    check(output_picture_relationship->target == "../media/background.png",
        "picture sheetData replacement should preserve background picture target");
    check(output_picture_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "picture sheetData replacement should keep background picture relationship internal");
    const auto* output_header_footer_relationship =
        output_relationships->find_by_id("rIdHeaderFooter");
    check(output_header_footer_relationship != nullptr,
        "picture sheetData replacement should keep header/footer VML relationship readable");
    check(output_header_footer_relationship->target == "../drawings/vmlDrawingHF1.vml",
        "picture sheetData replacement should preserve header/footer VML target");
    check(output_header_footer_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "picture sheetData replacement should keep header/footer VML relationship internal");
    check(output_reader.content_types().default_for("png") != nullptr,
        "picture sheetData replacement should preserve PNG content type default");
    check(output_reader.content_types().override_for(background_picture_part) == nullptr,
        "picture sheetData replacement should not promote background picture default");
    check(output_reader.content_types().override_for(header_footer_vml_part) != nullptr,
        "picture sheetData replacement should preserve header/footer VML content type override");
}

void test_package_editor_sheet_data_patch_preserves_page_setup_printer_settings()
{
    const std::filesystem::path source =
        output_path("fastxlsx-package-editor-sheetdata-printer-settings-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-sheetdata-printer-settings-output.xlsx");

    const std::string content_types =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/printerSettings/printerSettings1.bin" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.printerSettings"/>)"
        R"(</Types>)";
    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    const std::string worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<dimension ref="A1:B2"/>)"
        R"(<sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData>)"
        R"(<pageMargins left="0.7" right="0.7" top="0.75" bottom="0.75" header="0.3" footer="0.3"/>)"
        R"(<pageSetup paperSize="9" orientation="landscape" r:id="rIdPrinter"/>)"
        R"(</worksheet>)";
    const std::string worksheet_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdPrinter" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/printerSettings" Target="../printerSettings/printerSettings1.bin"/>)"
        R"(</Relationships>)";
    const std::string printer_settings =
        std::string("opaque-printer-settings\0bytes", 30);

    fastxlsx::detail::write_package(source,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", workbook},
            {"xl/_rels/workbook.xml.rels", workbook_relationships},
            {"xl/worksheets/sheet1.xml", worksheet},
            {"xl/worksheets/_rels/sheet1.xml.rels", worksheet_relationships},
            {"xl/printerSettings/printerSettings1.bin", printer_settings},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName printer_settings_part(
        "/xl/printerSettings/printerSettings1.bin");

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="2"><c r="A2"><v>2</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_from_single_chunk_source(editor, worksheet_part, replacement_sheet_data);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "printer settings sheetData replacement should keep worksheet in edit plan");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "printer settings sheetData replacement should local-DOM-rewrite worksheet");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "printer settings sheetData replacement should plan workbook calc metadata rewrite");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "printer settings sheetData replacement should local-DOM-rewrite workbook calc metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "page setup metadata", "caller review"}),
        "printer settings sheetData replacement should audit preserved page setup metadata");

    const auto* printer_settings_plan =
        editor.edit_plan().find_part(printer_settings_part);
    check(printer_settings_plan != nullptr,
        "worksheet-owned printer settings part should remain visible in the edit plan");
    check(printer_settings_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet-owned printer settings part should remain copy-original");
    check(printer_settings_plan->reason.find("worksheet relationship rIdPrinter")
            != std::string::npos
            && printer_settings_plan->reason.find("relationships/printerSettings")
                != std::string::npos
            && printer_settings_plan->reason.find(
                   "/xl/printerSettings/printerSettings1.bin")
                != std::string::npos,
        "printer settings copy reason should come from worksheet relationship traversal");
    check(printer_settings_plan->relationship_owner_part == worksheet_part.value(),
        "printer settings audit should keep structured relationship owner");
    check(printer_settings_plan->relationship_id == "rIdPrinter",
        "printer settings audit should keep structured relationship id");
    check(printer_settings_plan->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/printerSettings",
        "printer settings audit should keep structured relationship type");
    check(printer_settings_plan->relationship_target
            == "../printerSettings/printerSettings1.bin",
        "printer settings audit should keep structured relationship target");
    check(editor.edit_plan().worksheet_relationship_reference_audits().empty(),
        "printer settings sheetData replacement should not flag a valid preserved pageSetup relationship");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan =
        editor.planned_output();
    check(output_plan.full_calculation_on_load,
        "printer settings sheetData output plan should request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Remove,
        "printer settings sheetData output plan should expose calcChain removal policy");
    check(output_plan.relationship_target_audits.empty(),
        "printer settings sheetData output plan should not invent dependency audits");
    check(output_plan.worksheet_relationship_reference_audits.empty(),
        "printer settings output plan should not invent pageSetup relationship-id audits");
    check(output_plan.removed_parts.empty(),
        "printer settings sheetData output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "printer settings sheetData output plan should not expose removed package entries");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "page setup metadata", "caller review"}),
        "printer settings sheetData output plan should snapshot preserved page setup notes");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "printer settings sheetData output plan should rewrite worksheet");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/sheet1.xml",
        true, worksheet_part.value(),
        "printer settings sheetData output plan should classify worksheet as a part");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "printer settings sheetData output plan should rewrite workbook metadata");
    check_output_entry_part_context(output_plan.entries, "xl/workbook.xml",
        true, workbook_part.value(),
        "printer settings sheetData output plan should classify workbook as a part");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "printer settings sheetData output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "printer settings sheetData output plan should classify content types as metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "printer settings sheetData output plan should preserve package relationships");
    check_output_entry_part_context(output_plan.entries, "_rels/.rels", false, "",
        "printer settings sheetData output plan should classify package relationships as metadata");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "printer settings sheetData output plan should preserve workbook relationships");
    check_output_entry_part_context(output_plan.entries, "xl/_rels/workbook.xml.rels",
        false, "",
        "printer settings sheetData output plan should classify workbook relationships as metadata");
    check_output_entry_plan(output_plan.entries,
        "xl/printerSettings/printerSettings1.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "printer settings sheetData output plan should preserve printer settings part");
    check_output_entry_part_context(output_plan.entries,
        "xl/printerSettings/printerSettings1.bin", true, printer_settings_part.value(),
        "printer settings sheetData output plan should classify printer settings as a part");
    check_output_entry_relationship_context(output_plan.entries,
        "xl/printerSettings/printerSettings1.bin", worksheet_part.value(),
        "rIdPrinter",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/printerSettings",
        "../printerSettings/printerSettings1.bin",
        "printer settings sheetData output plan should keep worksheet relationship audit");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "printer settings sheetData output plan should preserve worksheet relationships");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        false, "",
        "printer settings sheetData output plan should classify worksheet relationships as metadata");
    const auto* output_worksheet_relationships_plan =
        find_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels");
    check(output_worksheet_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "printer settings sheetData output plan should classify worksheet relationships metadata");
    check(output_worksheet_relationships_plan->owner_part == worksheet_part.value(),
        "printer settings sheetData output plan should keep worksheet relationships owner context");
    check(find_output_entry_plan(output_plan.entries, "xl/calcChain.xml") == nullptr,
        "printer settings sheetData output plan should not invent calcChain output");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string expected_worksheet =
        std::string(
            R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
            R"(<dimension ref="A1:B2"/>)")
        + replacement_sheet_data
        + R"(<pageMargins left="0.7" right="0.7" top="0.75" bottom="0.75" header="0.3" footer="0.3"/>)"
          R"(<pageSetup paperSize="9" orientation="landscape" r:id="rIdPrinter"/>)"
          R"(</worksheet>)";
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == expected_worksheet,
        "printer settings sheetData replacement should preserve pageSetup around sheetData");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == worksheet_relationships,
        "printer settings sheetData replacement should byte-preserve worksheet relationships");
    check(output_reader.read_entry("xl/printerSettings/printerSettings1.bin")
            == printer_settings,
        "printer settings sheetData replacement should byte-preserve printer settings");
    check(output_reader.content_types().override_for(printer_settings_part) != nullptr,
        "printer settings sheetData replacement should preserve printer settings content type");

    const auto* output_relationships = output_reader.relationships_for(worksheet_part);
    check(output_relationships != nullptr,
        "printer settings sheetData replacement should keep worksheet relationships readable");
    const auto* output_printer_relationship =
        output_relationships->find_by_id("rIdPrinter");
    check(output_printer_relationship != nullptr,
        "printer settings sheetData replacement should keep printer settings relationship readable");
    check(output_printer_relationship->target
            == "../printerSettings/printerSettings1.bin",
        "printer settings sheetData replacement should preserve printer settings target");
    check(output_printer_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "printer settings sheetData replacement should keep printer settings relationship internal");
    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    const auto* graph_worksheet_relationships =
        output_graph.relationships_for(worksheet_part);
    check(graph_worksheet_relationships != nullptr,
        "printer settings sheetData replacement should keep worksheet graph relationships");
    const auto* graph_printer_relationship =
        graph_worksheet_relationships->find_by_id("rIdPrinter");
    check(graph_printer_relationship != nullptr,
        "printer settings sheetData replacement should keep printer settings relationship in graph");
    check(graph_printer_relationship->type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/printerSettings",
        "printer settings sheetData replacement graph should preserve printer settings type");
    check(graph_printer_relationship->target
            == "../printerSettings/printerSettings1.bin",
        "printer settings sheetData replacement graph should preserve printer settings target");
}

void test_package_editor_removes_background_picture_and_header_footer_vml_with_inbound_audit()
{
    const std::filesystem::path source =
        output_path("fastxlsx-package-editor-remove-picture-vml-source.xlsx");
    const std::filesystem::path picture_output =
        output_path("fastxlsx-package-editor-remove-background-picture-output.xlsx");
    const std::filesystem::path header_footer_vml_output =
        output_path("fastxlsx-package-editor-remove-header-footer-vml-output.xlsx");

    const std::string content_types =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="png" ContentType="image/png"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/drawings/vmlDrawingHF1.vml" ContentType="application/vnd.openxmlformats-officedocument.vmlDrawing"/>)"
        R"(</Types>)";
    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    const std::string worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<dimension ref="A1:B2"/>)"
        R"(<sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData>)"
        R"(<headerFooter><oddHeader>&amp;G</oddHeader></headerFooter>)"
        R"(<picture r:id="rIdPicture"/>)"
        R"(<legacyDrawingHF r:id="rIdHeaderFooter"/>)"
        R"(</worksheet>)";
    const std::string worksheet_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdPicture" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="../media/background.png"/>)"
        R"(<Relationship Id="rIdHeaderFooter" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing" Target="../drawings/vmlDrawingHF1.vml"/>)"
        R"(</Relationships>)";
    const std::string background_picture = "opaque-background-image-bytes";
    const std::string header_footer_vml = R"(<xml><v:shape id="headerPicture"/></xml>)";

    fastxlsx::detail::write_package(source,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", workbook},
            {"xl/_rels/workbook.xml.rels", workbook_relationships},
            {"xl/worksheets/sheet1.xml", worksheet},
            {"xl/worksheets/_rels/sheet1.xml.rels", worksheet_relationships},
            {"xl/media/background.png", background_picture},
            {"xl/drawings/vmlDrawingHF1.vml", header_footer_vml},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName background_picture_part("/xl/media/background.png");
    const fastxlsx::detail::PartName header_footer_vml_part(
        "/xl/drawings/vmlDrawingHF1.vml");

    {
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source);

        editor.remove_part(background_picture_part,
            "explicit background picture part removal");

        check(editor.edit_plan().find_part(background_picture_part) == nullptr,
            "background picture removal should clear the active edit-plan part");
        const auto* removed_picture =
            editor.edit_plan().find_removed_part(background_picture_part);
        check(removed_picture != nullptr,
            "background picture removal should record removed-part audit");
        check(removed_picture->reason.find("background picture") != std::string::npos,
            "background picture removal should retain the removal reason");
        check(removed_picture->reason.find("inbound relationship preserved")
                != std::string::npos,
            "background picture removal should audit preserved inbound relationships");
        check(removed_picture->reason.find("/xl/worksheets/sheet1.xml")
                != std::string::npos,
            "background picture removal inbound audit should include owner part");
        check(removed_picture->reason.find("rIdPicture") != std::string::npos,
            "background picture removal inbound audit should include relationship id");
        check(removed_picture->reason.find("../media/background.png")
                != std::string::npos,
            "background picture removal inbound audit should include original target");
        check(removed_picture->inbound_relationships.size() == 1,
            "background picture removal should keep structured inbound audit");
        const auto& picture_inbound = removed_picture->inbound_relationships.front();
        check(picture_inbound.owner_part == worksheet_part.value(),
            "background picture removal should keep inbound owner part");
        check(picture_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
            "background picture removal should keep inbound owner relationship entry");
        check(picture_inbound.relationship_id == "rIdPicture",
            "background picture removal should keep inbound relationship id");
        check(picture_inbound.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image",
            "background picture removal should keep inbound relationship type");
        check(picture_inbound.relationship_target == "../media/background.png",
            "background picture removal should keep inbound raw target");
        check(picture_inbound.target_part == background_picture_part,
            "background picture removal should keep normalized target part");
        check(editor.manifest().find_part(background_picture_part) == nullptr,
            "background picture removal should remove the part from the manifest");
        check(editor.manifest().content_types().default_for("png") != nullptr,
            "background picture removal should retain PNG default content type");
        check(editor.manifest().content_types().override_for(background_picture_part)
                == nullptr,
            "background picture removal should not add a media content type override");
        check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
            "background picture removal should not rewrite default-only content types");
        check(editor.edit_plan().find_removed_package_entry(
                  "xl/media/_rels/background.png.rels") == nullptr,
            "background picture removal should not invent missing owner relationships omission");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check(output_plan.relationship_target_audits.empty(),
            "background picture removal output plan should not invent target audits");
        check_output_entry_plan(output_plan.entries, "xl/media/background.png",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
            "background picture removal output plan should omit picture part");
        check_output_entry_has_inbound_relationship(output_plan.entries,
            "xl/media/background.png", worksheet_part.value(),
            "xl/worksheets/_rels/sheet1.xml.rels", "rIdPicture",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image",
            "../media/background.png", background_picture_part,
            "background picture removal output plan should keep worksheet inbound audit");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "background picture removal output plan should preserve worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawingHF1.vml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "background picture removal output plan should preserve header/footer VML");

        editor.save_as(picture_output);

        const auto entries = fastxlsx::test::read_zip_entries(picture_output);
        check(entries.find("xl/media/background.png") == entries.end(),
            "background picture removal output should omit picture part");
        check(entries.find("xl/media/_rels/background.png.rels") == entries.end(),
            "background picture removal output should not invent owner relationships");

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(picture_output);
        check(output_reader.read_entry("[Content_Types].xml") == content_types,
            "background picture removal should preserve content types bytes");
        check(output_reader.content_types().default_for("png") != nullptr,
            "background picture removal output should preserve PNG default");
        check(output_reader.content_types().override_for(background_picture_part)
                == nullptr,
            "background picture removal output should not promote PNG media to override");
        check(output_reader.content_types().override_for(header_footer_vml_part)
                != nullptr,
            "background picture removal output should keep header/footer VML override");
        check(output_reader.read_entry("_rels/.rels") == package_relationships,
            "background picture removal should preserve package relationships bytes");
        check(output_reader.read_entry("xl/workbook.xml") == workbook,
            "background picture removal should preserve workbook bytes");
        check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
                == workbook_relationships,
            "background picture removal should preserve workbook relationships bytes");
        check(output_reader.read_entry("xl/worksheets/sheet1.xml") == worksheet,
            "background picture removal should preserve worksheet bytes");
        check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == worksheet_relationships,
            "background picture removal should not prune worksheet relationships");
        check(output_reader.read_entry("xl/drawings/vmlDrawingHF1.vml")
                == header_footer_vml,
            "background picture removal should preserve header/footer VML bytes");
        check(output_reader.relationships_for(background_picture_part) == nullptr,
            "background picture removal should not create owner relationships for absent media");

        const auto* output_relationships = output_reader.relationships_for(worksheet_part);
        check(output_relationships != nullptr,
            "background picture removal should keep worksheet relationships readable");
        const auto* picture_link = output_relationships->find_by_id("rIdPicture");
        check(picture_link != nullptr,
            "background picture removal should keep inbound picture relationship id");
        check(picture_link->target == "../media/background.png",
            "background picture removal should not rewrite inbound picture target");
        check(picture_link->target_mode
                == fastxlsx::detail::Relationship::TargetMode::Internal,
            "background picture removal should keep inbound picture target mode");
        check(output_relationships->find_by_id("rIdHeaderFooter") != nullptr,
            "background picture removal should keep header/footer VML relationship");
    }

    {
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source);

        editor.remove_part(header_footer_vml_part,
            "explicit header/footer VML drawing part removal");

        check(editor.edit_plan().find_part(header_footer_vml_part) == nullptr,
            "header/footer VML removal should clear the active edit-plan part");
        const auto* removed_vml =
            editor.edit_plan().find_removed_part(header_footer_vml_part);
        check(removed_vml != nullptr,
            "header/footer VML removal should record removed-part audit");
        check(removed_vml->reason.find("header/footer VML") != std::string::npos,
            "header/footer VML removal should retain the removal reason");
        check(removed_vml->reason.find("inbound relationship preserved")
                != std::string::npos,
            "header/footer VML removal should audit preserved inbound relationships");
        check(removed_vml->reason.find("/xl/worksheets/sheet1.xml")
                != std::string::npos,
            "header/footer VML removal inbound audit should include owner part");
        check(removed_vml->reason.find("rIdHeaderFooter") != std::string::npos,
            "header/footer VML removal inbound audit should include relationship id");
        check(removed_vml->reason.find("../drawings/vmlDrawingHF1.vml")
                != std::string::npos,
            "header/footer VML removal inbound audit should include original target");
        check(removed_vml->inbound_relationships.size() == 1,
            "header/footer VML removal should keep structured inbound audit");
        const auto& vml_inbound = removed_vml->inbound_relationships.front();
        check(vml_inbound.owner_part == worksheet_part.value(),
            "header/footer VML removal should keep inbound owner part");
        check(vml_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
            "header/footer VML removal should keep inbound owner relationship entry");
        check(vml_inbound.relationship_id == "rIdHeaderFooter",
            "header/footer VML removal should keep inbound relationship id");
        check(vml_inbound.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing",
            "header/footer VML removal should keep inbound relationship type");
        check(vml_inbound.relationship_target == "../drawings/vmlDrawingHF1.vml",
            "header/footer VML removal should keep inbound raw target");
        check(vml_inbound.target_part == header_footer_vml_part,
            "header/footer VML removal should keep normalized target part");
        check(editor.manifest().find_part(header_footer_vml_part) == nullptr,
            "header/footer VML removal should remove the part from the manifest");
        check(editor.manifest().content_types().override_for(header_footer_vml_part)
                == nullptr,
            "header/footer VML removal should remove the content type override");
        const auto* content_types_entry =
            editor.edit_plan().find_package_entry("[Content_Types].xml");
        check(content_types_entry != nullptr,
            "header/footer VML removal should record content types rewrite");
        check(content_types_entry->write_mode
                == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
            "header/footer VML removal content types rewrite should be local-DOM-rewrite");
        check(content_types_entry->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
            "header/footer VML removal content types audit should keep structured role");
        check(editor.edit_plan().find_removed_package_entry(
                  "xl/drawings/_rels/vmlDrawingHF1.vml.rels") == nullptr,
            "header/footer VML removal should not invent missing owner relationships omission");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check(output_plan.relationship_target_audits.empty(),
            "header/footer VML removal output plan should not invent target audits");
        check_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawingHF1.vml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
            "header/footer VML removal output plan should omit VML part");
        check_output_entry_has_inbound_relationship(output_plan.entries,
            "xl/drawings/vmlDrawingHF1.vml", worksheet_part.value(),
            "xl/worksheets/_rels/sheet1.xml.rels", "rIdHeaderFooter",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing",
            "../drawings/vmlDrawingHF1.vml", header_footer_vml_part,
            "header/footer VML removal output plan should keep worksheet inbound audit");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
            "header/footer VML removal output plan should rewrite content types");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "header/footer VML removal output plan should preserve worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/media/background.png",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "header/footer VML removal output plan should preserve background picture");

        editor.save_as(header_footer_vml_output);

        const auto entries = fastxlsx::test::read_zip_entries(header_footer_vml_output);
        check(entries.find("xl/drawings/vmlDrawingHF1.vml") == entries.end(),
            "header/footer VML removal output should omit VML part");
        check(entries.find("xl/drawings/_rels/vmlDrawingHF1.vml.rels") == entries.end(),
            "header/footer VML removal output should not invent owner relationships");

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(header_footer_vml_output);
        check(output_reader.content_types().override_for(header_footer_vml_part)
                == nullptr,
            "header/footer VML removal output should remove VML content type override");
        const std::string output_content_types =
            output_reader.read_entry("[Content_Types].xml");
        check_not_contains(output_content_types, "/xl/drawings/vmlDrawingHF1.vml",
            "header/footer VML removal content types XML should omit VML override");
        check(output_reader.content_types().default_for("png") != nullptr,
            "header/footer VML removal output should preserve PNG default");
        check(output_reader.content_types().override_for(background_picture_part)
                == nullptr,
            "header/footer VML removal output should not promote PNG media to override");
        check(output_reader.read_entry("_rels/.rels") == package_relationships,
            "header/footer VML removal should preserve package relationships bytes");
        check(output_reader.read_entry("xl/workbook.xml") == workbook,
            "header/footer VML removal should preserve workbook bytes");
        check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
                == workbook_relationships,
            "header/footer VML removal should preserve workbook relationships bytes");
        check(output_reader.read_entry("xl/worksheets/sheet1.xml") == worksheet,
            "header/footer VML removal should preserve worksheet bytes");
        check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == worksheet_relationships,
            "header/footer VML removal should not prune worksheet relationships");
        check(output_reader.read_entry("xl/media/background.png") == background_picture,
            "header/footer VML removal should preserve background picture bytes");
        check(output_reader.relationships_for(header_footer_vml_part) == nullptr,
            "header/footer VML removal should not create owner relationships for absent VML");

        const auto* output_relationships = output_reader.relationships_for(worksheet_part);
        check(output_relationships != nullptr,
            "header/footer VML removal should keep worksheet relationships readable");
        const auto* vml_link = output_relationships->find_by_id("rIdHeaderFooter");
        check(vml_link != nullptr,
            "header/footer VML removal should keep inbound VML relationship id");
        check(vml_link->target == "../drawings/vmlDrawingHF1.vml",
            "header/footer VML removal should not rewrite inbound VML target");
        check(vml_link->target_mode
                == fastxlsx::detail::Relationship::TargetMode::Internal,
            "header/footer VML removal should keep inbound VML target mode");
        check(output_relationships->find_by_id("rIdPicture") != nullptr,
            "header/footer VML removal should keep background picture relationship");
    }
}

void test_package_editor_background_picture_and_header_footer_vml_same_path_ordering()
{
    const std::filesystem::path source =
        output_path("fastxlsx-package-editor-picture-vml-order-source.xlsx");
    const std::filesystem::path picture_restore_output =
        output_path("fastxlsx-package-editor-replace-after-remove-background-picture-output.xlsx");
    const std::filesystem::path header_footer_vml_remove_output =
        output_path("fastxlsx-package-editor-remove-after-replace-header-footer-vml-output.xlsx");

    const std::string content_types =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="png" ContentType="image/png"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/drawings/vmlDrawingHF1.vml" ContentType="application/vnd.openxmlformats-officedocument.vmlDrawing"/>)"
        R"(</Types>)";
    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    const std::string worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<dimension ref="A1:B2"/>)"
        R"(<sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData>)"
        R"(<headerFooter><oddHeader>&amp;G</oddHeader></headerFooter>)"
        R"(<picture r:id="rIdPicture"/>)"
        R"(<legacyDrawingHF r:id="rIdHeaderFooter"/>)"
        R"(</worksheet>)";
    const std::string worksheet_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdPicture" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="../media/background.png"/>)"
        R"(<Relationship Id="rIdHeaderFooter" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing" Target="../drawings/vmlDrawingHF1.vml"/>)"
        R"(</Relationships>)";
    const std::string background_picture = "opaque-background-image-bytes";
    const std::string header_footer_vml = R"(<xml><v:shape id="headerPicture"/></xml>)";

    fastxlsx::detail::write_package(source,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", workbook},
            {"xl/_rels/workbook.xml.rels", workbook_relationships},
            {"xl/worksheets/sheet1.xml", worksheet},
            {"xl/worksheets/_rels/sheet1.xml.rels", worksheet_relationships},
            {"xl/media/background.png", background_picture},
            {"xl/drawings/vmlDrawingHF1.vml", header_footer_vml},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName background_picture_part("/xl/media/background.png");
    const fastxlsx::detail::PartName header_footer_vml_part(
        "/xl/drawings/vmlDrawingHF1.vml");

    {
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source);

        editor.remove_part(background_picture_part,
            "temporary worksheet-owned background picture removal");
        check(editor.edit_plan().find_removed_part(background_picture_part) != nullptr,
            "background picture restore setup should record removed-part audit");
        check(editor.manifest().find_part(background_picture_part) == nullptr,
            "background picture restore setup should remove the manifest part");
        check(editor.manifest().content_types().default_for("png") != nullptr,
            "background picture restore setup should retain the PNG default");
        check(editor.manifest().content_types().override_for(background_picture_part)
                == nullptr,
            "background picture restore setup should not create a media override");

        const std::string restored_picture = "restored-background-image-bytes";
        replace_part_with_memory_chunks(editor, background_picture_part, restored_picture,
            "restored worksheet-owned background picture after removal");

        check(editor.edit_plan().find_removed_part(background_picture_part) == nullptr,
            "background picture replacement after removal should clear stale removed-part audit");
        check(editor.edit_plan().find_removed_package_entry(
                  "xl/media/_rels/background.png.rels") == nullptr,
            "background picture replacement after removal should not invent owner relationships omission");
        check(editor.edit_plan().removed_parts().empty(),
            "background picture replacement after removal should leave no removed parts");
        const auto* picture_plan =
            editor.edit_plan().find_part(background_picture_part);
        check(picture_plan != nullptr,
            "background picture replacement after removal should restore active edit-plan part");
        check(picture_plan->write_mode
                == fastxlsx::detail::PartWriteMode::StreamRewrite,
            "background picture replacement after removal should keep final write mode");
        check(picture_plan->reason.find("after removal") != std::string::npos,
            "background picture replacement after removal should keep final reason");
        check_manifest_write_mode(editor, background_picture_part,
            fastxlsx::detail::PartWriteMode::StreamRewrite,
            "background picture replacement after removal should restore manifest write mode");
        check(editor.manifest().content_types().default_for("png") != nullptr,
            "background picture replacement after removal should retain PNG default");
        check(editor.manifest().content_types().override_for(background_picture_part)
                == nullptr,
            "background picture replacement after removal should not promote PNG media to override");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check(!output_plan.full_calculation_on_load,
            "background picture replacement after removal output plan should not request full calculation");
        check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
            "background picture replacement after removal output plan should preserve calcChain policy");
        check(output_plan.relationship_target_audits.empty(),
            "background picture replacement after removal output plan should not invent target audits");
        check(output_plan.removed_parts.empty(),
            "background picture replacement after removal output plan should clear removed parts");
        check(output_plan.removed_package_entries.empty(),
            "background picture replacement after removal output plan should clear removed package entries");
        check_output_entry_plan(output_plan.entries, "xl/media/background.png",
            fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
            "background picture replacement after removal output plan should rewrite picture part");
        check_output_entry_part_context(output_plan.entries, "xl/media/background.png",
            true, background_picture_part.value(),
            "background picture replacement after removal output plan should classify picture part");
        const auto* output_picture_plan =
            find_output_entry_plan(output_plan.entries, "xl/media/background.png");
        check(output_picture_plan->reason.find("after removal") != std::string::npos,
            "background picture replacement after removal output plan should keep reason");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "background picture replacement after removal output plan should preserve content types");
        check_output_entry_part_context(output_plan.entries, "[Content_Types].xml",
            false, "",
            "background picture replacement after removal output plan should classify content types as metadata");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "background picture replacement after removal output plan should preserve worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawingHF1.vml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "background picture replacement after removal output plan should preserve header/footer VML");
        check_output_entry_part_context(output_plan.entries,
            "xl/drawings/vmlDrawingHF1.vml", true, header_footer_vml_part.value(),
            "background picture replacement after removal output plan should classify sibling VML part");
        check(find_output_entry_plan(output_plan.entries,
                  "xl/media/_rels/background.png.rels") == nullptr,
            "background picture replacement after removal output plan should not invent owner relationships");

        editor.save_as(picture_restore_output);

        const auto entries = fastxlsx::test::read_zip_entries(picture_restore_output);
        check(entries.find("xl/media/background.png") != entries.end(),
            "background picture replacement after removal output should restore picture part");
        check(entries.find("xl/media/_rels/background.png.rels") == entries.end(),
            "background picture replacement after removal output should not invent owner relationships");

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(picture_restore_output);
        check_preserved_source_entries(editor.reader(), output_reader,
            background_picture_part.zip_path());
        check(output_reader.read_entry("xl/media/background.png") == restored_picture,
            "background picture replacement after removal should write restored bytes");
        check(output_reader.read_entry("[Content_Types].xml") == content_types,
            "background picture replacement after removal should preserve source content types bytes");
        check(output_reader.content_types().default_for("png") != nullptr,
            "background picture replacement after removal output should preserve PNG default");
        check(output_reader.content_types().override_for(background_picture_part) == nullptr,
            "background picture replacement after removal output should not promote PNG media to override");
        check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == worksheet_relationships,
            "background picture replacement after removal should not prune worksheet relationships");
        check(output_reader.read_entry("xl/drawings/vmlDrawingHF1.vml")
                == header_footer_vml,
            "background picture replacement after removal should preserve header/footer VML bytes");
        check(output_reader.relationships_for(background_picture_part) == nullptr,
            "background picture replacement after removal should not create owner relationships");
        const auto* output_relationships =
            output_reader.relationships_for(worksheet_part);
        check(output_relationships != nullptr,
            "background picture replacement after removal should keep worksheet relationships readable");
        const auto* picture_link = output_relationships->find_by_id("rIdPicture");
        check(picture_link != nullptr,
            "background picture replacement after removal should keep inbound relationship id");
        check(picture_link->target == "../media/background.png",
            "background picture replacement after removal should not rewrite inbound target");
    }

    {
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source);

        const std::string replacement_vml =
            R"(<xml><v:shape id="queuedHeaderPicture"/></xml>)";
        replace_part_with_memory_chunks(editor, header_footer_vml_part, replacement_vml,
            "queued worksheet-owned header/footer VML replacement");
        const auto* prior_vml_plan =
            editor.edit_plan().find_part(header_footer_vml_part);
        check(prior_vml_plan != nullptr,
            "header/footer VML removal-after-replacement setup should queue replacement");
        check(prior_vml_plan->write_mode
                == fastxlsx::detail::PartWriteMode::StreamRewrite,
            "header/footer VML removal-after-replacement setup should local-DOM-rewrite VML");
        check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
            "header/footer VML replacement setup should not rewrite content types");

        editor.remove_part(header_footer_vml_part,
            "final worksheet-owned header/footer VML removal after replacement");

        check(editor.edit_plan().find_part(header_footer_vml_part) == nullptr,
            "header/footer VML removal after replacement should clear active replacement");
        const auto* removed_vml =
            editor.edit_plan().find_removed_part(header_footer_vml_part);
        check(removed_vml != nullptr,
            "header/footer VML removal after replacement should record removed-part audit");
        check(removed_vml->reason.find("after replacement") != std::string::npos,
            "header/footer VML removal after replacement should keep final reason");
        check(removed_vml->reason.find("inbound relationship preserved")
                != std::string::npos,
            "header/footer VML removal after replacement should audit preserved inbound relationship");
        check(removed_vml->inbound_relationships.size() == 1,
            "header/footer VML removal after replacement should keep structured inbound audit");
        const auto& vml_inbound = removed_vml->inbound_relationships.front();
        check(vml_inbound.owner_part == worksheet_part.value(),
            "header/footer VML removal after replacement should keep inbound owner part");
        check(vml_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
            "header/footer VML removal after replacement should keep inbound owner entry");
        check(vml_inbound.relationship_id == "rIdHeaderFooter",
            "header/footer VML removal after replacement should keep inbound relationship id");
        check(vml_inbound.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing",
            "header/footer VML removal after replacement should keep inbound relationship type");
        check(vml_inbound.relationship_target == "../drawings/vmlDrawingHF1.vml",
            "header/footer VML removal after replacement should keep inbound raw target");
        check(vml_inbound.target_part == header_footer_vml_part,
            "header/footer VML removal after replacement should keep normalized inbound target");
        check(editor.manifest().find_part(header_footer_vml_part) == nullptr,
            "header/footer VML removal after replacement should remove manifest part");
        check(editor.manifest().content_types().override_for(header_footer_vml_part)
                == nullptr,
            "header/footer VML removal after replacement should remove content type override");
        check(editor.manifest().content_types().default_for("png") != nullptr,
            "header/footer VML removal after replacement should keep PNG default");
        check(editor.manifest().content_types().override_for(background_picture_part)
                == nullptr,
            "header/footer VML removal after replacement should not promote PNG media to override");
        const auto* content_types_entry =
            editor.edit_plan().find_package_entry("[Content_Types].xml");
        check(content_types_entry != nullptr,
            "header/footer VML removal after replacement should record content types rewrite");
        check(content_types_entry->write_mode
                == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
            "header/footer VML removal after replacement content types audit should be local-DOM-rewrite");
        check(content_types_entry->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
            "header/footer VML removal after replacement content types audit should keep structured role");
        check(editor.edit_plan().find_removed_package_entry(
                  "xl/drawings/_rels/vmlDrawingHF1.vml.rels") == nullptr,
            "header/footer VML removal after replacement should not invent owner relationships omission");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check(!output_plan.full_calculation_on_load,
            "header/footer VML removal after replacement output plan should not request full calculation");
        check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
            "header/footer VML removal after replacement output plan should preserve calcChain policy");
        check(output_plan.relationship_target_audits.empty(),
            "header/footer VML removal after replacement output plan should not invent target audits");
        check(output_plan.removed_parts.size() == 1,
            "header/footer VML removal after replacement output plan should expose one removed part");
        check(output_plan.removed_parts.front().part_name == header_footer_vml_part,
            "header/footer VML removal after replacement output plan should expose removed VML part");
        check(output_plan.removed_parts.front().reason.find("after replacement")
                != std::string::npos,
            "header/footer VML removal after replacement output plan should keep removed-part reason");
        check(output_plan.removed_parts.front().inbound_relationships.size() == 1,
            "header/footer VML removal after replacement output plan should keep removed-part inbound audit");
        check(output_plan.removed_package_entries.empty(),
            "header/footer VML removal after replacement output plan should not omit metadata entries");
        check_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawingHF1.vml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
            "header/footer VML removal after replacement output plan should omit VML part");
        check_output_entry_part_context(output_plan.entries,
            "xl/drawings/vmlDrawingHF1.vml", true, header_footer_vml_part.value(),
            "header/footer VML removal after replacement output plan should classify omitted VML part");
        const auto* output_vml_plan =
            find_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawingHF1.vml");
        check(output_vml_plan->reason.find("after replacement") != std::string::npos,
            "header/footer VML removal after replacement output plan should keep final removal reason");
        check_output_entry_has_inbound_relationship(output_plan.entries,
            "xl/drawings/vmlDrawingHF1.vml", worksheet_part.value(),
            "xl/worksheets/_rels/sheet1.xml.rels", "rIdHeaderFooter",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing",
            "../drawings/vmlDrawingHF1.vml", header_footer_vml_part,
            "header/footer VML removal after replacement output plan should keep inbound audit");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
            "header/footer VML removal after replacement output plan should rewrite content types");
        check_output_entry_part_context(output_plan.entries, "[Content_Types].xml",
            false, "",
            "header/footer VML removal after replacement output plan should classify content types as metadata");
        const auto* output_content_types_plan =
            find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
        check(output_content_types_plan->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
            "header/footer VML removal after replacement output plan should keep content types audit role");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "header/footer VML removal after replacement output plan should preserve worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/media/background.png",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "header/footer VML removal after replacement output plan should preserve background picture");
        check_output_entry_part_context(output_plan.entries, "xl/media/background.png",
            true, background_picture_part.value(),
            "header/footer VML removal after replacement output plan should classify sibling picture part");
        check(find_output_entry_plan(output_plan.entries,
                  "xl/drawings/_rels/vmlDrawingHF1.vml.rels") == nullptr,
            "header/footer VML removal after replacement output plan should not invent owner relationships");

        editor.save_as(header_footer_vml_remove_output);

        const auto entries =
            fastxlsx::test::read_zip_entries(header_footer_vml_remove_output);
        check(entries.find("xl/drawings/vmlDrawingHF1.vml") == entries.end(),
            "header/footer VML removal after replacement output should omit VML part");
        check(entries.find("xl/drawings/_rels/vmlDrawingHF1.vml.rels") == entries.end(),
            "header/footer VML removal after replacement output should not invent owner relationships");

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(header_footer_vml_remove_output);
        check(output_reader.content_types().override_for(header_footer_vml_part)
                == nullptr,
            "header/footer VML removal after replacement output should remove VML content type override");
        check_not_contains(output_reader.read_entry("[Content_Types].xml"),
            "/xl/drawings/vmlDrawingHF1.vml",
            "header/footer VML removal after replacement content types should omit VML override");
        check(output_reader.content_types().default_for("png") != nullptr,
            "header/footer VML removal after replacement output should preserve PNG default");
        check(output_reader.content_types().override_for(background_picture_part)
                == nullptr,
            "header/footer VML removal after replacement output should not promote PNG media to override");
        check(output_reader.read_entry("_rels/.rels") == package_relationships,
            "header/footer VML removal after replacement should preserve package relationships bytes");
        check(output_reader.read_entry("xl/workbook.xml") == workbook,
            "header/footer VML removal after replacement should preserve workbook bytes");
        check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
                == workbook_relationships,
            "header/footer VML removal after replacement should preserve workbook relationships bytes");
        check(output_reader.read_entry("xl/worksheets/sheet1.xml") == worksheet,
            "header/footer VML removal after replacement should preserve worksheet bytes");
        check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == worksheet_relationships,
            "header/footer VML removal after replacement should not prune worksheet relationships");
        check(output_reader.read_entry("xl/media/background.png") == background_picture,
            "header/footer VML removal after replacement should preserve background picture bytes");
        check(output_reader.relationships_for(header_footer_vml_part) == nullptr,
            "header/footer VML removal after replacement should not create owner relationships");
        const auto* output_relationships =
            output_reader.relationships_for(worksheet_part);
        check(output_relationships != nullptr,
            "header/footer VML removal after replacement should keep worksheet relationships readable");
        const auto* vml_link = output_relationships->find_by_id("rIdHeaderFooter");
        check(vml_link != nullptr,
            "header/footer VML removal after replacement should keep inbound relationship id");
        check(vml_link->target == "../drawings/vmlDrawingHF1.vml",
            "header/footer VML removal after replacement should not rewrite inbound target");
        check(output_relationships->find_by_id("rIdPicture") != nullptr,
            "header/footer VML removal after replacement should keep background picture relationship");
    }

    {
        const std::filesystem::path output =
            output_path("fastxlsx-package-editor-repeat-background-picture-output.xlsx");
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source);

        const std::string stale_picture = "stale-background-image-bytes";
        const std::string final_picture = "final-background-image-bytes";
        replace_part_with_memory_chunks(editor, background_picture_part, stale_picture,
            "stale repeated worksheet-owned background picture replacement");
        replace_part_with_memory_chunks(editor, background_picture_part, final_picture,
            "final repeated worksheet-owned background picture replacement");

        const auto* picture_plan =
            editor.edit_plan().find_part(background_picture_part);
        check(picture_plan != nullptr,
            "repeated background picture replacement should keep an active edit-plan part");
        check(picture_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
            "repeated background picture replacement should keep final write mode");
        check(picture_plan->reason.find("final repeated") != std::string::npos,
            "repeated background picture replacement should keep final reason");
        check(picture_plan->reason.find("stale repeated") == std::string::npos,
            "repeated background picture replacement should drop stale reason");
        check_manifest_write_mode(editor, background_picture_part,
            fastxlsx::detail::PartWriteMode::StreamRewrite,
            "repeated background picture replacement should mirror final write mode into manifest");
        check(editor.manifest().content_types().default_for("png") != nullptr,
            "repeated background picture replacement should keep PNG default");
        check(editor.manifest().content_types().override_for(background_picture_part)
                == nullptr,
            "repeated background picture replacement should not promote PNG media to override");
        check(editor.edit_plan().find_removed_part(background_picture_part) == nullptr,
            "repeated background picture replacement should not leave removed-part audit");
        check(editor.edit_plan().find_removed_package_entry(
                  "xl/media/_rels/background.png.rels") == nullptr,
            "repeated background picture replacement should not leave owner relationships omission");
        check(editor.edit_plan().find_package_entry("xl/media/_rels/background.png.rels")
                == nullptr,
            "repeated background picture replacement should not invent owner relationships audit");
        check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
            "repeated background picture replacement should not rewrite content types audit");
        check(editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == nullptr,
            "repeated background picture replacement should not rewrite inbound worksheet relationships");
        check(editor.edit_plan().find_part(header_footer_vml_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "repeated background picture replacement should keep sibling header/footer VML copy-original");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check(!output_plan.full_calculation_on_load,
            "repeated background picture replacement output plan should not request full calculation");
        check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
            "repeated background picture replacement output plan should preserve calcChain policy");
        check(output_plan.relationship_target_audits.empty(),
            "repeated background picture replacement output plan should not invent target audits");
        check(output_plan.removed_parts.empty(),
            "repeated background picture replacement output plan should not expose removed parts");
        check(output_plan.removed_package_entries.empty(),
            "repeated background picture replacement output plan should not omit metadata entries");
        check_output_entry_plan(output_plan.entries, "xl/media/background.png",
            fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
            "repeated background picture replacement output plan should rewrite picture part");
        const auto* output_picture_plan =
            find_output_entry_plan(output_plan.entries, "xl/media/background.png");
        check(output_picture_plan->reason.find("final repeated") != std::string::npos,
            "repeated background picture replacement output plan should keep final reason");
        check(output_picture_plan->reason.find("stale repeated") == std::string::npos,
            "repeated background picture replacement output plan should drop stale reason");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "repeated background picture replacement output plan should preserve content types");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "repeated background picture replacement output plan should preserve worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawingHF1.vml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "repeated background picture replacement output plan should preserve sibling VML");
        check(find_output_entry_plan(output_plan.entries,
                  "xl/media/_rels/background.png.rels") == nullptr,
            "repeated background picture replacement output plan should not invent owner relationships");

        editor.save_as(output);

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(output);
        check_preserved_source_entries(editor.reader(), output_reader,
            background_picture_part.zip_path());
        check(output_reader.read_entry("xl/media/background.png") == final_picture,
            "repeated background picture replacement should write final bytes");
        check(output_reader.read_entry("xl/media/background.png") != stale_picture,
            "repeated background picture replacement should not write stale bytes");
        check(output_reader.read_entry("[Content_Types].xml") == content_types,
            "repeated background picture replacement should preserve content types bytes");
        check(output_reader.content_types().default_for("png") != nullptr,
            "repeated background picture replacement output should preserve PNG default");
        check(output_reader.content_types().override_for(background_picture_part) == nullptr,
            "repeated background picture replacement output should not promote PNG media to override");
        check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == worksheet_relationships,
            "repeated background picture replacement should not prune worksheet relationships");
        check(output_reader.read_entry("xl/drawings/vmlDrawingHF1.vml")
                == header_footer_vml,
            "repeated background picture replacement should preserve sibling VML bytes");
        check(output_reader.relationships_for(background_picture_part) == nullptr,
            "repeated background picture replacement should not create owner relationships");
        const auto* output_relationships =
            output_reader.relationships_for(worksheet_part);
        check(output_relationships != nullptr,
            "repeated background picture replacement should keep worksheet relationships readable");
        const auto* picture_link = output_relationships->find_by_id("rIdPicture");
        check(picture_link != nullptr,
            "repeated background picture replacement should keep inbound relationship id");
        check(picture_link->target == "../media/background.png",
            "repeated background picture replacement should not rewrite inbound target");
    }
}


} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor sheetdata shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "sheetdata-linked")) {
            test_package_editor_replaces_worksheet_and_preserves_linked_object_parts();
            test_package_editor_sheet_data_patch_preserves_worksheet_owned_object_parts();
            test_package_editor_removes_worksheet_owned_object_parts_with_inbound_audit();
            test_package_editor_worksheet_owned_object_part_same_path_ordering();
            test_package_editor_sheet_data_patch_preserves_background_picture_and_header_footer_vml();
            test_package_editor_sheet_data_patch_preserves_page_setup_printer_settings();
            test_package_editor_removes_background_picture_and_header_footer_vml_with_inbound_audit();
            test_package_editor_background_picture_and_header_footer_vml_same_path_ordering();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
