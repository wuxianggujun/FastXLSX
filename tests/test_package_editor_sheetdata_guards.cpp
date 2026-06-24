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
    return shard == "all" || shard == "sheetdata-guards";
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
void test_package_editor_replaces_self_closing_source_sheet_data()
{
    CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-sheetdata-self-closing-source.xlsx");
    source.worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<dimension ref="A1"/>)"
        R"(<sheetData/>)"
        R"(<autoFilter ref="A1:C3"/>)"
        R"(</worksheet>)";

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
        output_path("fastxlsx-package-editor-sheetdata-self-closing-output.xlsx");
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const std::string replacement_sheet_data =
        R"(<sheetData><row r="1"><c r="A1"><v>11</v></c><c r="C3"><f>A1*2</f><v>22</v></c></row></sheetData>)";

    replace_worksheet_sheet_data_from_single_chunk_source(editor, worksheet_part, replacement_sheet_data);

    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "self-closing sheetData patch should local-DOM-rewrite the worksheet");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "self-closing sheetData patch should rewrite workbook calc metadata");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "self-closing sheetData patch should remove stale calcChain");
    check(editor.edit_plan().full_calculation_on_load(),
        "self-closing sheetData patch should request full calculation");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "dimension metadata", "caller review"}),
        "self-closing sheetData patch should audit preserved dimension metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "autoFilter metadata", "caller review"}),
        "self-closing sheetData patch should audit preserved autoFilter metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "contains formulas", "calcChain policy"}),
        "self-closing sheetData patch should audit replacement formulas");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "self-closing sheetData patch output should omit stale calcChain.xml");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string expected_worksheet =
        std::string(
            R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
            R"(<dimension ref="A1"/>)")
        + replacement_sheet_data
        + R"(<autoFilter ref="A1:C3"/>)"
          R"(</worksheet>)";
    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check(worksheet_xml == expected_worksheet,
        "self-closing source sheetData should be replaced while preserving wrapper XML");
    check_not_contains(worksheet_xml, "<sheetData/>",
        "self-closing source sheetData should not remain in output");
    check(output_reader.find_entry("xl/calcChain.xml") == nullptr,
        "self-closing sheetData patch should remove calcChain payload");
    check(output_reader.content_types().override_for(calc_chain_part) == nullptr,
        "self-closing sheetData patch should remove calcChain content type override");
    check_not_contains(output_reader.read_entry("xl/_rels/workbook.xml.rels"),
        "relationships/calcChain",
        "self-closing sheetData patch should remove calcChain workbook relationship");
    check_contains(output_reader.read_entry("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "self-closing sheetData patch should request workbook recalculation");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "self-closing sheetData patch should preserve unknown bytes");
}

void test_package_editor_replaces_prefixed_sheet_data()
{
    CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-sheetdata-prefixed-source.xlsx");
    source.worksheet =
        R"(<x:worksheet xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<x:dimension ref="A1:B2"/>)"
        R"(<x:sheetData><x:row r="1"><x:c r="A1"><x:v>1</x:v></x:c></x:row></x:sheetData>)"
        R"(<x:autoFilter ref="A1:B2"/>)"
        R"(</x:worksheet>)";

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
        output_path("fastxlsx-package-editor-sheetdata-prefixed-output.xlsx");
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const std::string replacement_sheet_data =
        R"(<x:sheetData><x:row r="2"><x:c r="B2"><x:v>22</x:v></x:c></x:row></x:sheetData>)";

    replace_worksheet_sheet_data_from_single_chunk_source(editor, worksheet_part, replacement_sheet_data);

    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "prefixed sheetData patch should local-DOM-rewrite the worksheet");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "prefixed sheetData patch should rewrite workbook calc metadata");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "prefixed sheetData patch should remove stale calcChain");
    check(editor.edit_plan().full_calculation_on_load(),
        "prefixed sheetData patch should request full calculation");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "dimension metadata", "caller review"}),
        "prefixed sheetData patch should audit preserved dimension metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "autoFilter metadata", "caller review"}),
        "prefixed sheetData patch should audit preserved autoFilter metadata");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "prefixed sheetData patch output should omit stale calcChain.xml");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string expected_worksheet =
        std::string(
            R"(<x:worksheet xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
            R"(<x:dimension ref="A1:B2"/>)")
        + replacement_sheet_data
        + R"(<x:autoFilter ref="A1:B2"/>)"
          R"(</x:worksheet>)";
    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check(worksheet_xml == expected_worksheet,
        "prefixed sheetData patch should preserve worksheet prefixes and wrapper XML");
    check_not_contains(worksheet_xml, R"(<x:v>1</x:v>)",
        "prefixed sheetData patch should remove old prefixed row data");
    check(output_reader.find_entry("xl/calcChain.xml") == nullptr,
        "prefixed sheetData patch should remove calcChain payload");
    check(output_reader.content_types().override_for(calc_chain_part) == nullptr,
        "prefixed sheetData patch should remove calcChain content type override");
    check_not_contains(output_reader.read_entry("xl/_rels/workbook.xml.rels"),
        "relationships/calcChain",
        "prefixed sheetData patch should remove calcChain workbook relationship");
    check_contains(output_reader.read_entry("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "prefixed sheetData patch should request workbook recalculation");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "prefixed sheetData patch should preserve unknown bytes");
}

void test_package_editor_replaces_with_self_closing_sheet_data_payload()
{
    CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-sheetdata-empty-payload-source.xlsx");
    source.worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<dimension ref="A1:C3"/>)"
        R"(<sheetData><row r="1"><c r="A1"><f>SUM(B1:C1)</f><v>3</v></c></row><row r="3"><c r="C3"><v>9</v></c></row></sheetData>)"
        R"(<autoFilter ref="A1:C3"/>)"
        R"(</worksheet>)";

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
        output_path("fastxlsx-package-editor-sheetdata-empty-payload-output.xlsx");
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const std::string replacement_sheet_data = R"(<sheetData/>)";

    replace_worksheet_sheet_data_from_single_chunk_source(editor, worksheet_part, replacement_sheet_data);

    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "empty sheetData patch should local-DOM-rewrite the worksheet");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "empty sheetData patch should rewrite workbook calc metadata");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "empty sheetData patch should remove stale calcChain");
    check(editor.edit_plan().full_calculation_on_load(),
        "empty sheetData patch should request full calculation");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "dimension metadata", "caller review"}),
        "empty sheetData patch should audit preserved dimension metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "autoFilter metadata", "caller review"}),
        "empty sheetData patch should audit preserved autoFilter metadata");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "contains formulas", "calcChain policy"}),
        "empty sheetData patch should not audit formulas for an empty replacement payload");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "shared string indexes", "xl/sharedStrings.xml"}),
        "empty sheetData patch should not audit shared string indexes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "style id references", "xl/styles.xml"}),
        "empty sheetData patch should not audit style id references");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "empty sheetData patch output should omit stale calcChain.xml");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string expected_worksheet =
        std::string(
            R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
            R"(<dimension ref="A1:C3"/>)")
        + replacement_sheet_data
        + R"(<autoFilter ref="A1:C3"/>)"
          R"(</worksheet>)";
    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check(worksheet_xml == expected_worksheet,
        "empty replacement sheetData should preserve worksheet wrapper XML");
    check_not_contains(worksheet_xml, R"(<row r="1">)",
        "empty replacement sheetData should remove old first row");
    check_not_contains(worksheet_xml, R"(<row r="3">)",
        "empty replacement sheetData should remove old later row");
    check_not_contains(worksheet_xml, "<f>",
        "empty replacement sheetData should remove old formula cells");
    check(output_reader.find_entry("xl/calcChain.xml") == nullptr,
        "empty sheetData patch should remove calcChain payload");
    check(output_reader.content_types().override_for(calc_chain_part) == nullptr,
        "empty sheetData patch should remove calcChain content type override");
    check_not_contains(output_reader.read_entry("xl/_rels/workbook.xml.rels"),
        "relationships/calcChain",
        "empty sheetData patch should remove calcChain workbook relationship");
    check_contains(output_reader.read_entry("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "empty sheetData patch should request workbook recalculation");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "empty sheetData patch should preserve unknown bytes");
}

void test_package_editor_sheet_data_patch_uses_queued_worksheet_replacement()
{
    const LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package("fastxlsx-package-editor-sheetdata-queued-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-sheetdata-queued-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string queued_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetViews><sheetView workbookViewId="42"/></sheetViews>)"
        R"(<sheetData><row r="9"><c r="C9"><v>999</v></c></row></sheetData>)"
        R"(<autoFilter ref="C1:D2"/>)"
        R"(<extLst><ext uri="{queued-wrapper}"/></extLst>)"
        R"(</worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, queued_worksheet);

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="1"><c r="C1"><v>303</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_from_single_chunk_source(editor, worksheet_part, replacement_sheet_data);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "queued sheetData patch should keep worksheet in the edit plan");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "queued sheetData patch should finish as worksheet local-DOM rewrite");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "queued sheetData patch should keep workbook metadata rewrite");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "queued sheetData patch should keep workbook local-DOM-rewrite");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "queued sheetData patch should keep stale calcChain removed-part audit");
    check(editor.edit_plan().full_calculation_on_load(),
        "queued sheetData patch should keep full calculation request");
    check(has_note_containing(editor.edit_plan().notes(),
              {"pull-based chunk source", "file-backed staged chunk",
                  "follow-up planned-input transforms"}),
        "queued sheetData patch should preserve evidence that prior worksheet replacement was staged");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "view metadata", "caller review"}),
        "queued sheetData patch should audit queued worksheet view metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "autoFilter metadata", "caller review"}),
        "queued sheetData patch should audit queued worksheet autoFilter metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "extension metadata", "caller review"}),
        "queued sheetData patch should audit queued worksheet extension metadata");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "protection metadata", "caller review"}),
        "queued sheetData patch should not audit source-only sheet protection metadata");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "legacy drawing reference metadata", "caller review"}),
        "queued sheetData patch should not audit source-only legacy drawing metadata");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string expected_worksheet =
        std::string(
            R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
            R"(<sheetViews><sheetView workbookViewId="42"/></sheetViews>)")
        + replacement_sheet_data
        + R"(<autoFilter ref="C1:D2"/>)"
          R"(<extLst><ext uri="{queued-wrapper}"/></extLst>)"
          R"(</worksheet>)";
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == expected_worksheet,
        "queued sheetData patch should replace sheetData inside the queued worksheet bytes");
    check_not_contains(output_reader.read_entry("xl/worksheets/sheet1.xml"), R"(<v>999</v>)",
        "queued sheetData patch should remove the prior queued sheetData rows");
    check_not_contains(output_reader.read_entry("xl/worksheets/sheet1.xml"), "sheetProtection",
        "queued sheetData patch should not resurrect source-only sheet protection");
    check_not_contains(output_reader.read_entry("xl/worksheets/sheet1.xml"), "legacyDrawing",
        "queued sheetData patch should not resurrect source-only legacy drawing markup");
    check_not_contains(output_reader.read_entry("xl/worksheets/sheet1.xml"), "tableParts",
        "queued sheetData patch should not resurrect source-only tableParts markup");
    check(output_reader.find_entry("xl/calcChain.xml") == nullptr,
        "queued sheetData patch output should keep stale calcChain omitted");
    check_contains(output_reader.read_entry("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "queued sheetData patch should keep workbook fullCalcOnLoad metadata");
    check(output_reader.read_entry("custom/opaque-extension.bin") == source.opaque_extension,
        "queued sheetData patch should preserve unknown extension bytes");
    check(output_reader.content_types().override_for(opaque_extension_part) == nullptr,
        "queued sheetData patch should keep unknown extension on default content type");
}

void test_package_editor_sheet_data_patch_replaces_self_closing_queued_worksheet()
{
    const LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-sheetdata-queued-self-closing-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-sheetdata-queued-self-closing-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string queued_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetViews><sheetView workbookViewId="7"/></sheetViews>)"
        R"(<sheetData/>)"
        R"(<autoFilter ref="D1:E2"/>)"
        R"(<extLst><ext uri="{queued-self-closing-wrapper}"/></extLst>)"
        R"(</worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, queued_worksheet);

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="4"><c r="D4"><v>404</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_from_single_chunk_source(editor, worksheet_part, replacement_sheet_data);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "queued self-closing sheetData patch should keep worksheet in the edit plan");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "queued self-closing sheetData patch should finish as worksheet local-DOM rewrite");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "queued self-closing sheetData patch should keep workbook metadata rewrite");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "queued self-closing sheetData patch should keep workbook local-DOM-rewrite");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "queued self-closing sheetData patch should keep stale calcChain removed-part audit");
    check(editor.edit_plan().full_calculation_on_load(),
        "queued self-closing sheetData patch should keep full calculation request");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "view metadata", "caller review"}),
        "queued self-closing sheetData patch should audit queued worksheet view metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "autoFilter metadata", "caller review"}),
        "queued self-closing sheetData patch should audit queued worksheet autoFilter metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "extension metadata", "caller review"}),
        "queued self-closing sheetData patch should audit queued worksheet extension metadata");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "table reference metadata", "caller review"}),
        "queued self-closing sheetData patch should not audit source-only table metadata");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string expected_worksheet =
        std::string(
            R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
            R"(<sheetViews><sheetView workbookViewId="7"/></sheetViews>)")
        + replacement_sheet_data
        + R"(<autoFilter ref="D1:E2"/>)"
          R"(<extLst><ext uri="{queued-self-closing-wrapper}"/></extLst>)"
          R"(</worksheet>)";
    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check(worksheet_xml == expected_worksheet,
        "queued self-closing sheetData patch should replace current planned self-closing sheetData");
    check_not_contains(worksheet_xml, "<sheetData/>",
        "queued self-closing sheetData patch should remove queued self-closing sheetData");
    check_not_contains(worksheet_xml, "sheetProtection",
        "queued self-closing sheetData patch should not resurrect source-only sheet protection");
    check_not_contains(worksheet_xml, "legacyDrawing",
        "queued self-closing sheetData patch should not resurrect source-only legacy drawing markup");
    check_not_contains(worksheet_xml, "tableParts",
        "queued self-closing sheetData patch should not resurrect source-only tableParts markup");
    check(output_reader.find_entry("xl/calcChain.xml") == nullptr,
        "queued self-closing sheetData patch output should keep stale calcChain omitted");
    check_contains(output_reader.read_entry("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "queued self-closing sheetData patch should keep workbook fullCalcOnLoad metadata");
    check(output_reader.read_entry("custom/opaque-extension.bin") == source.opaque_extension,
        "queued self-closing sheetData patch should preserve unknown extension bytes");
    check(output_reader.content_types().override_for(opaque_extension_part) == nullptr,
        "queued self-closing sheetData patch should keep unknown extension on default content type");
}

void test_package_editor_rejects_worksheet_sheet_data_replacement_without_state_changes()
{
    const LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package("fastxlsx-package-editor-sheetdata-invalid-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-sheetdata-invalid-output.xlsx");

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
        replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Missing", "<sheetData/>");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "sheet name",
            "missing sheet-name replacement failure should name sheet lookup");
    }
    check(failed,
        "PackageEditor should reject sheetData replacement for a missing sheet name");
    check(editor.edit_plan().size() == initial_plan_size,
        "missing sheet-name replacement should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "missing sheet-name replacement should not add audit notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "missing sheet-name replacement should not add relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "missing sheet-name replacement should not add worksheet relationship reference audits");
    check(editor.edit_plan().removed_parts().empty(),
        "missing sheet-name replacement should not record removed parts");
    check(editor.edit_plan().package_entries().empty(),
        "missing sheet-name replacement should not record package-entry audit");
    check(!editor.edit_plan().full_calculation_on_load(),
        "missing sheet-name replacement should not request recalculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "missing sheet-name replacement should not change calcChain policy");
    check(editor.manifest().find_part(calc_chain_part) != nullptr,
        "missing sheet-name replacement should keep calcChain in the manifest");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "missing sheet-name replacement should keep worksheet copy-original");

    failed = false;
    try {
        replace_worksheet_sheet_data_from_single_chunk_source(editor, worksheet_part, "<row/>");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "sheetData",
            "invalid sheetData replacement failure should name sheetData");
    }
    check(failed,
        "PackageEditor should reject replacement XML that is not a sheetData element");
    check(editor.edit_plan().size() == initial_plan_size,
        "invalid sheetData replacement should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "invalid sheetData replacement should not add audit notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "invalid sheetData replacement should not add relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "invalid sheetData replacement should not add worksheet relationship reference audits");
    check(editor.edit_plan().removed_parts().empty(),
        "invalid sheetData replacement should not record removed parts");
    check(editor.edit_plan().package_entries().empty(),
        "invalid sheetData replacement should not record package-entry audit");
    check(!editor.edit_plan().full_calculation_on_load(),
        "invalid sheetData replacement should not request recalculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "invalid sheetData replacement should not change calcChain policy");
    check(editor.manifest().find_part(calc_chain_part) != nullptr,
        "invalid sheetData replacement should keep calcChain in the manifest");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "invalid sheetData replacement should keep worksheet copy-original");

    failed = false;
    try {
        replace_worksheet_sheet_data_from_single_chunk_source(editor, worksheet_part,
            R"(<sheetData><row r="1"><c r="A1"><v>2</v></c></row>)");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "sheetData",
            "malformed sheetData replacement failure should name sheetData");
    }
    check(failed,
        "PackageEditor should reject sheetData replacement XML with no closing tag");
    check(editor.edit_plan().size() == initial_plan_size,
        "malformed sheetData replacement should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "malformed sheetData replacement should not add audit notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "malformed sheetData replacement should not add relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "malformed sheetData replacement should not add worksheet relationship reference audits");
    check(editor.edit_plan().removed_parts().empty(),
        "malformed sheetData replacement should not record removed parts");
    check(editor.edit_plan().package_entries().empty(),
        "malformed sheetData replacement should not record package-entry audit");
    check(!editor.edit_plan().full_calculation_on_load(),
        "malformed sheetData replacement should not request recalculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "malformed sheetData replacement should not change calcChain policy");
    check(editor.manifest().find_part(calc_chain_part) != nullptr,
        "malformed sheetData replacement should keep calcChain in the manifest");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "malformed sheetData replacement should keep worksheet copy-original");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader);

    const SourcePackage missing_source =
        write_missing_sheet_data_source_package(
            "fastxlsx-package-editor-sheetdata-missing-source.xlsx");
    const std::filesystem::path missing_output =
        output_path("fastxlsx-package-editor-sheetdata-missing-output.xlsx");
    fastxlsx::detail::PackageEditor missing_editor =
        fastxlsx::detail::PackageEditor::open(missing_source.path);
    const std::size_t missing_initial_plan_size = missing_editor.edit_plan().size();
    const std::size_t missing_initial_worksheet_relationship_reference_audit_count =
        missing_editor.edit_plan().worksheet_relationship_reference_audits().size();

    failed = false;
    int missing_source_replacement_reads = 0;
    try {
        missing_editor.replace_worksheet_sheet_data_from_chunk_source(worksheet_part,
            [&](std::string& chunk) {
                ++missing_source_replacement_reads;
                chunk = "<sheetData/>";
                return true;
            });
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "sheetData",
            "missing source sheetData failure should name sheetData");
        check_contains(error.what(),
            "current worksheet input for worksheet sheetData replacement output",
            "missing source sheetData failure should identify the source worksheet input boundary");
        check_not_contains(error.what(), "sheetData replacement XML",
            "missing source sheetData failure should not be mislabeled as replacement payload input");
    }
    check(failed,
        "PackageEditor should reject sheetData replacement when the source worksheet has no sheetData");
    check(missing_source_replacement_reads == 0,
        "missing source sheetData failure should not consume replacement sheetData chunks");
    check(missing_editor.edit_plan().size() == missing_initial_plan_size,
        "missing source sheetData failure should not change edit plan size");
    check(missing_editor.edit_plan().notes().empty(),
        "missing source sheetData failure should not add audit notes");
    check(missing_editor.edit_plan().worksheet_relationship_reference_audits().size()
            == missing_initial_worksheet_relationship_reference_audit_count,
        "missing source sheetData failure should not add worksheet relationship reference audits");
    check(missing_editor.edit_plan().removed_parts().empty(),
        "missing source sheetData failure should not record removed parts");
    check(missing_editor.edit_plan().package_entries().empty(),
        "missing source sheetData failure should not record package-entry audit");
    check(!missing_editor.edit_plan().full_calculation_on_load(),
        "missing source sheetData failure should not request recalculation");
    check_manifest_write_mode(missing_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "missing source sheetData failure should keep worksheet copy-original");

    missing_editor.save_as(missing_output);

    const fastxlsx::detail::PackageReader missing_output_reader =
        fastxlsx::detail::PackageReader::open(missing_output);
    check_preserved_source_entries(missing_editor.reader(), missing_output_reader);

    CalcSourcePackage invalid_root_source =
        write_calc_source_package("fastxlsx-package-editor-sheetdata-invalid-root-source.xlsx");
    invalid_root_source.worksheet =
        R"(<notWorksheet><sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData></notWorksheet>)";
    fastxlsx::detail::write_package(invalid_root_source.path,
        {
            {"[Content_Types].xml", invalid_root_source.content_types},
            {"_rels/.rels", invalid_root_source.package_relationships},
            {"xl/workbook.xml", invalid_root_source.workbook},
            {"xl/_rels/workbook.xml.rels", invalid_root_source.workbook_relationships},
            {"xl/worksheets/sheet1.xml", invalid_root_source.worksheet},
            {"xl/calcChain.xml", invalid_root_source.calc_chain},
            {"custom/opaque.bin", invalid_root_source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const std::filesystem::path invalid_root_output =
        output_path("fastxlsx-package-editor-sheetdata-invalid-root-output.xlsx");
    fastxlsx::detail::PackageEditor invalid_root_editor =
        fastxlsx::detail::PackageEditor::open(invalid_root_source.path);
    const std::size_t invalid_root_initial_plan_size =
        invalid_root_editor.edit_plan().size();
    const std::size_t invalid_root_initial_note_count =
        invalid_root_editor.edit_plan().notes().size();
    const std::vector<std::filesystem::path> invalid_root_temp_files_before =
        package_editor_temp_files();
    int invalid_root_replacement_reads = 0;

    failed = false;
    try {
        invalid_root_editor.replace_worksheet_sheet_data_from_chunk_source(worksheet_part,
            [&](std::string& chunk) {
                ++invalid_root_replacement_reads;
                chunk = "<sheetData/>";
                return true;
            });
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(),
            "current worksheet input for worksheet sheetData replacement output",
            "invalid source worksheet root failure should identify the source worksheet input boundary");
        check_not_contains(error.what(), "sheetData replacement XML",
            "invalid source worksheet root failure should not be mislabeled as replacement payload input");
    }
    check(failed,
        "PackageEditor should reject sheetData replacement when the source root is not worksheet");
    check(invalid_root_replacement_reads == 0,
        "invalid source worksheet root should fail before consuming replacement sheetData chunks");
    check(invalid_root_editor.edit_plan().size() == invalid_root_initial_plan_size,
        "invalid source worksheet root failure should not change edit plan size");
    check(invalid_root_editor.edit_plan().notes().size() == invalid_root_initial_note_count,
        "invalid source worksheet root failure should not add audit notes");
    check_no_new_package_editor_temp_files(invalid_root_temp_files_before,
        "invalid source worksheet root failure should not leak PackageEditor temp files");

    invalid_root_editor.save_as(invalid_root_output);
    const fastxlsx::detail::PackageReader invalid_root_output_reader =
        fastxlsx::detail::PackageReader::open(invalid_root_output);
    check_preserved_source_entries(invalid_root_editor.reader(), invalid_root_output_reader);

    const std::filesystem::path by_name_invalid_root_output =
        output_path("fastxlsx-package-editor-sheetdata-by-name-invalid-root-output.xlsx");
    fastxlsx::detail::PackageEditor by_name_invalid_root_editor =
        fastxlsx::detail::PackageEditor::open(invalid_root_source.path);
    const std::size_t by_name_invalid_root_initial_plan_size =
        by_name_invalid_root_editor.edit_plan().size();
    const std::size_t by_name_invalid_root_initial_note_count =
        by_name_invalid_root_editor.edit_plan().notes().size();
    const std::size_t by_name_invalid_root_initial_package_entry_count =
        by_name_invalid_root_editor.edit_plan().package_entries().size();
    const std::size_t by_name_invalid_root_initial_removed_package_entry_count =
        by_name_invalid_root_editor.edit_plan().removed_package_entries().size();
    const std::size_t by_name_invalid_root_initial_relationship_target_audit_count =
        by_name_invalid_root_editor.edit_plan().relationship_target_audits().size();
    const std::size_t by_name_invalid_root_initial_worksheet_relationship_reference_audit_count =
        by_name_invalid_root_editor.edit_plan().worksheet_relationship_reference_audits().size();
    const bool by_name_invalid_root_initial_full_calculation =
        by_name_invalid_root_editor.edit_plan().full_calculation_on_load();
    const fastxlsx::detail::CalcChainAction by_name_invalid_root_initial_calc_chain_action =
        by_name_invalid_root_editor.edit_plan().calc_chain_action();
    const std::vector<std::filesystem::path> by_name_invalid_root_temp_files_before =
        package_editor_temp_files();
    int by_name_invalid_root_replacement_reads = 0;

    failed = false;
    try {
        by_name_invalid_root_editor.replace_worksheet_sheet_data_from_chunk_source_by_name(
            "Sheet1",
            [&](std::string& chunk) {
                ++by_name_invalid_root_replacement_reads;
                chunk = "<sheetData/>";
                return true;
            });
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(),
            "current worksheet input for worksheet sheetData replacement output",
            "by-name invalid source worksheet root should identify the source worksheet input boundary");
        check_not_contains(error.what(), "sheetData replacement XML",
            "by-name invalid source worksheet root should not be mislabeled as replacement payload input");
    }
    check(failed,
        "by-name PackageEditor should reject sheetData replacement when the source root is not worksheet");
    check(by_name_invalid_root_replacement_reads == 0,
        "by-name invalid source worksheet root should fail before consuming replacement sheetData chunks");
    check(by_name_invalid_root_editor.edit_plan().size()
            == by_name_invalid_root_initial_plan_size,
        "by-name invalid source worksheet root failure should not change edit plan size");
    check(by_name_invalid_root_editor.edit_plan().notes().size()
            == by_name_invalid_root_initial_note_count,
        "by-name invalid source worksheet root failure should not add audit notes");
    check(by_name_invalid_root_editor.edit_plan().package_entries().size()
            == by_name_invalid_root_initial_package_entry_count,
        "by-name invalid source worksheet root failure should not add package-entry audit");
    check(by_name_invalid_root_editor.edit_plan().removed_package_entries().size()
            == by_name_invalid_root_initial_removed_package_entry_count,
        "by-name invalid source worksheet root failure should not add removed package-entry audit");
    check(by_name_invalid_root_editor.edit_plan().relationship_target_audits().size()
            == by_name_invalid_root_initial_relationship_target_audit_count,
        "by-name invalid source worksheet root failure should not add relationship target audits");
    check(by_name_invalid_root_editor.edit_plan().worksheet_relationship_reference_audits().size()
            == by_name_invalid_root_initial_worksheet_relationship_reference_audit_count,
        "by-name invalid source worksheet root failure should not add worksheet relationship reference audits");
    check(by_name_invalid_root_editor.edit_plan().removed_parts().empty(),
        "by-name invalid source worksheet root failure should not record removed parts");
    check(by_name_invalid_root_editor.edit_plan().full_calculation_on_load()
            == by_name_invalid_root_initial_full_calculation,
        "by-name invalid source worksheet root failure should not request recalculation");
    check(by_name_invalid_root_editor.edit_plan().calc_chain_action()
            == by_name_invalid_root_initial_calc_chain_action,
        "by-name invalid source worksheet root failure should not change calcChain policy");
    check(by_name_invalid_root_editor.manifest().find_part(calc_chain_part) != nullptr,
        "by-name invalid source worksheet root failure should keep calcChain in the manifest");
    check_manifest_write_mode(by_name_invalid_root_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "by-name invalid source worksheet root failure should keep worksheet copy-original");
    check_no_new_package_editor_temp_files(by_name_invalid_root_temp_files_before,
        "by-name invalid source worksheet root failure should not leak PackageEditor temp files");

    by_name_invalid_root_editor.save_as(by_name_invalid_root_output);
    const fastxlsx::detail::PackageReader by_name_invalid_root_output_reader =
        fastxlsx::detail::PackageReader::open(by_name_invalid_root_output);
    check_preserved_source_entries(
        by_name_invalid_root_editor.reader(), by_name_invalid_root_output_reader);

    CalcSourcePackage malformed_source =
        write_calc_source_package("fastxlsx-package-editor-sheetdata-malformed-source.xlsx");
    malformed_source.worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<dimension ref="A1"/>)"
        R"(<sheetData><row r="1"><c r="A1"><v>1</v></c></row>)"
        R"(<autoFilter ref="A1:A1"/>)"
        R"(</worksheet>)";
    fastxlsx::detail::write_package(malformed_source.path,
        {
            {"[Content_Types].xml", malformed_source.content_types},
            {"_rels/.rels", malformed_source.package_relationships},
            {"xl/workbook.xml", malformed_source.workbook},
            {"xl/_rels/workbook.xml.rels", malformed_source.workbook_relationships},
            {"xl/worksheets/sheet1.xml", malformed_source.worksheet},
            {"xl/calcChain.xml", malformed_source.calc_chain},
            {"custom/opaque.bin", malformed_source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const std::filesystem::path malformed_output =
        output_path("fastxlsx-package-editor-sheetdata-malformed-output.xlsx");
    fastxlsx::detail::PackageEditor malformed_editor =
        fastxlsx::detail::PackageEditor::open(malformed_source.path);
    const std::size_t malformed_initial_plan_size =
        malformed_editor.edit_plan().size();
    const std::size_t malformed_initial_note_count =
        malformed_editor.edit_plan().notes().size();
    const std::size_t malformed_initial_relationship_target_audit_count =
        malformed_editor.edit_plan().relationship_target_audits().size();
    const std::size_t malformed_initial_worksheet_relationship_reference_audit_count =
        malformed_editor.edit_plan().worksheet_relationship_reference_audits().size();

    failed = false;
    try {
        replace_worksheet_sheet_data_from_single_chunk_source(malformed_editor,
            worksheet_part,
            R"(<sheetData><row r="1"><c r="A1"><v>2</v></c></row></sheetData>)");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(),
            "current worksheet input for worksheet sheetData replacement output",
            "malformed source sheetData failure should identify the source worksheet input boundary");
        check_contains(error.what(), "worksheet event reader found an invalid worksheet boundary",
            "malformed source sheetData failure should name the event-reader boundary error");
        check_not_contains(error.what(), "sheetData replacement XML",
            "malformed source sheetData failure should not be mislabeled as replacement payload input");
    }
    check(failed,
        "PackageEditor should reject sheetData replacement when source sheetData is not closed");
    check(malformed_editor.edit_plan().size() == malformed_initial_plan_size,
        "malformed source sheetData failure should not change edit plan size");
    check(malformed_editor.edit_plan().notes().size() == malformed_initial_note_count,
        "malformed source sheetData failure should not add audit notes");
    check(malformed_editor.edit_plan().relationship_target_audits().size()
            == malformed_initial_relationship_target_audit_count,
        "malformed source sheetData failure should not add relationship target audits");
    check(malformed_editor.edit_plan().worksheet_relationship_reference_audits().size()
            == malformed_initial_worksheet_relationship_reference_audit_count,
        "malformed source sheetData failure should not add worksheet relationship reference audits");
    check(malformed_editor.edit_plan().removed_parts().empty(),
        "malformed source sheetData failure should not record removed parts");
    check(malformed_editor.edit_plan().package_entries().empty(),
        "malformed source sheetData failure should not record package-entry audit");
    check(!malformed_editor.edit_plan().full_calculation_on_load(),
        "malformed source sheetData failure should not request recalculation");
    check(malformed_editor.edit_plan().calc_chain_action()
            == fastxlsx::detail::CalcChainAction::Preserve,
        "malformed source sheetData failure should not change calcChain policy");
    check(malformed_editor.manifest().find_part(calc_chain_part) != nullptr,
        "malformed source sheetData failure should keep calcChain in the manifest");
    check_manifest_write_mode(malformed_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "malformed source sheetData failure should keep worksheet copy-original");

    malformed_editor.save_as(malformed_output);

    const fastxlsx::detail::PackageReader malformed_output_reader =
        fastxlsx::detail::PackageReader::open(malformed_output);
    check_preserved_source_entries(malformed_editor.reader(), malformed_output_reader);
}

void test_package_editor_rejects_sheet_data_input_event_window_over_limit_without_state_changes()
{
    LinkedObjectSourcePackage source = write_sheet_data_patch_source_package(
        "fastxlsx-package-editor-sheetdata-over-limit-source.xlsx");
    const std::string oversized_comment(
        fastxlsx::detail::package_editor_cell_replacement_event_window_byte_limit + 1U,
        'x');
    source.worksheet = std::string(R"(<worksheet><!--)")
        + oversized_comment
        + R"(--><sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData></worksheet>)";
    rewrite_linked_object_source_package(source);

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-sheetdata-over-limit-output.xlsx");
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
            "over-limit sheetData replacement should not change edit plan size");
        check(editor.edit_plan().notes().size() == initial_note_count,
            "over-limit sheetData replacement should not add audit notes");
        check(editor.edit_plan().relationship_target_audits().size()
                == initial_relationship_target_audit_count,
            "over-limit sheetData replacement should not add relationship target audits");
        check(editor.edit_plan().worksheet_relationship_reference_audits().size()
                == initial_worksheet_relationship_reference_audit_count,
            "over-limit sheetData replacement should not add worksheet relationship audits");
        check(editor.edit_plan().removed_parts().empty(),
            "over-limit sheetData replacement should not record removed parts");
        check(editor.edit_plan().package_entries().empty(),
            "over-limit sheetData replacement should not record package-entry audit");
        check(editor.edit_plan().removed_package_entries().empty(),
            "over-limit sheetData replacement should not record removed package-entry audit");
        check(!editor.edit_plan().full_calculation_on_load(),
            "over-limit sheetData replacement should not request recalculation");
        check(editor.edit_plan().calc_chain_action()
                == fastxlsx::detail::CalcChainAction::Preserve,
            "over-limit sheetData replacement should not change calcChain policy");
        check_manifest_write_mode(editor, worksheet_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "over-limit sheetData replacement should keep worksheet copy-original");
        check_manifest_write_mode(editor, workbook_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "over-limit sheetData replacement should keep workbook copy-original");
        check_manifest_write_mode(editor, calc_chain_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "over-limit sheetData replacement should keep calcChain copy-original");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check(output_plan.notes.empty(),
            "over-limit sheetData replacement should leave planned output notes empty");
        check(!output_plan.full_calculation_on_load,
            "over-limit planned output should not request full calculation");
        check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
            "over-limit planned output should preserve calcChain policy");
        check(output_plan.removed_parts.empty(),
            "over-limit planned output should not expose removed parts");
        check(output_plan.removed_package_entries.empty(),
            "over-limit planned output should not expose removed package entries");
        check(output_plan.relationship_target_audits.empty(),
            "over-limit planned output should not expose relationship target audits");
        check(output_plan.worksheet_relationship_reference_audits.empty(),
            "over-limit planned output should not expose worksheet reference audits");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "over-limit planned output should keep worksheet copy-original");
        check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "over-limit planned output should keep workbook copy-original");
        check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "over-limit planned output should keep calcChain copy-original");
    };

    bool failed = false;
    try {
        replace_worksheet_sheet_data_from_single_chunk_source(editor, worksheet_part, "<sheetData/>");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "worksheet event reader exceeded bounded input window",
            "over-limit sheetData failure should name the event-reader window");
    }
    check(failed,
        "PackageEditor should reject sheetData replacement when worksheet XML exceeds the event-reader window");
    check_no_state_change();

    failed = false;
    try {
        replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Sheet1", "<sheetData/>");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "worksheet event reader exceeded bounded input window",
            "by-name over-limit sheetData failure should share the event-reader window");
    }
    check(failed,
        "PackageEditor by-name helper should reject sheetData replacement over the event-reader window");
    check_no_state_change();

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "over-limit sheetData failure output should preserve oversized worksheet bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "over-limit sheetData failure output should preserve unknown extension bytes");
}

void test_package_editor_rejects_sheet_data_payload_over_limit_without_state_changes()
{
    const LinkedObjectSourcePackage source = write_sheet_data_patch_source_package(
        "fastxlsx-package-editor-sheetdata-payload-over-limit-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-sheetdata-payload-over-limit-output.xlsx");
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const std::string oversized_payload = std::string("<sheetData><!--")
        + std::string(
            fastxlsx::detail::package_editor_sheet_data_replacement_payload_byte_limit + 1U,
            'y')
        + "--></sheetData>";
    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t initial_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();

    const auto check_no_state_change = [&]() {
        check(editor.edit_plan().size() == initial_plan_size,
            "over-limit sheetData payload should not change edit plan size");
        check(editor.edit_plan().notes().size() == initial_note_count,
            "over-limit sheetData payload should not add audit notes");
        check(editor.edit_plan().relationship_target_audits().size()
                == initial_relationship_target_audit_count,
            "over-limit sheetData payload should not add relationship target audits");
        check(editor.edit_plan().worksheet_relationship_reference_audits().size()
                == initial_worksheet_relationship_reference_audit_count,
            "over-limit sheetData payload should not add worksheet relationship audits");
        check(editor.edit_plan().removed_parts().empty(),
            "over-limit sheetData payload should not record removed parts");
        check(editor.edit_plan().package_entries().empty(),
            "over-limit sheetData payload should not record package-entry audit");
        check(editor.edit_plan().removed_package_entries().empty(),
            "over-limit sheetData payload should not record removed package-entry audit");
        check(!editor.edit_plan().full_calculation_on_load(),
            "over-limit sheetData payload should not request recalculation");
        check(editor.edit_plan().calc_chain_action()
                == fastxlsx::detail::CalcChainAction::Preserve,
            "over-limit sheetData payload should not change calcChain policy");
        check_manifest_write_mode(editor, worksheet_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "over-limit sheetData payload should keep worksheet copy-original");
        check_manifest_write_mode(editor, workbook_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "over-limit sheetData payload should keep workbook copy-original");
        check_manifest_write_mode(editor, calc_chain_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "over-limit sheetData payload should keep calcChain copy-original");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check(output_plan.notes.empty(),
            "over-limit sheetData payload should leave planned output notes empty");
        check(output_plan.removed_parts.empty(),
            "over-limit sheetData payload should not expose removed parts");
        check(output_plan.removed_package_entries.empty(),
            "over-limit sheetData payload should not expose removed package entries");
        check(output_plan.relationship_target_audits.empty(),
            "over-limit sheetData payload should not expose relationship target audits");
        check(output_plan.worksheet_relationship_reference_audits.empty(),
            "over-limit sheetData payload should not expose worksheet reference audits");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "over-limit payload planned output should keep worksheet copy-original");
        check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "over-limit payload planned output should keep workbook copy-original");
        check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "over-limit payload planned output should keep calcChain copy-original");
    };

    bool failed = false;
    try {
        replace_worksheet_sheet_data_from_single_chunk_source(editor, worksheet_part, oversized_payload);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "sheetData",
            "over-limit replacement payload failure should name sheetData");
        check_contains(error.what(), "bounded payload limit",
            "over-limit replacement payload failure should name the bounded payload limit");
        check_contains(error.what(), "replacement sheetData XML",
            "over-limit replacement payload failure should identify the oversized payload");
    }
    check(failed,
        "PackageEditor should reject sheetData replacement payloads that exceed the payload limit");
    check_no_state_change();

    failed = false;
    try {
        replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Sheet1", oversized_payload);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "replacement sheetData XML",
            "by-name over-limit payload failure should identify the oversized payload");
    }
    check(failed,
        "PackageEditor by-name helper should reject over-limit sheetData payloads");
    check_no_state_change();

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "over-limit payload failure output should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "over-limit payload failure output should preserve unknown extension bytes");
}

void test_package_editor_allows_rewritten_sheet_data_output_over_payload_limit()
{
    LinkedObjectSourcePackage source = write_sheet_data_patch_source_package(
        "fastxlsx-package-editor-sheetdata-rewritten-over-limit-source.xlsx");
    const std::filesystem::path direct_output =
        output_path("fastxlsx-package-editor-sheetdata-rewritten-over-limit-direct-output.xlsx");
    const std::filesystem::path by_name_output =
        output_path("fastxlsx-package-editor-sheetdata-rewritten-over-limit-by-name-output.xlsx");

    const std::size_t limit =
        fastxlsx::detail::package_editor_sheet_data_replacement_payload_byte_limit;
    const std::size_t event_window_limit =
        fastxlsx::detail::package_editor_cell_replacement_event_window_byte_limit;
    const std::string old_sheet_data =
        R"(<sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData>)";
    const std::string source_prefix = R"(<worksheet><!--)";
    const std::string source_suffix = std::string("-->") + old_sheet_data + "</worksheet>";
    const std::size_t source_padding_size =
        limit - source_prefix.size() - source_suffix.size() - 8U;
    source.worksheet = source_prefix + std::string(source_padding_size, 'w')
        + source_suffix;
    const std::string replacement_sheet_data = std::string("<sheetData><!--")
        + std::string(old_sheet_data.size() + 32U, 'r') + "--></sheetData>";
    check(source.worksheet.size() < limit,
        "rewritten-over-limit fixture source worksheet should stay within the payload-limit-sized fixture");
    check(source.worksheet.size() < event_window_limit,
        "rewritten-over-limit fixture source worksheet should stay within the event-reader window");
    check(replacement_sheet_data.size() < limit,
        "rewritten-over-limit fixture replacement payload should stay within the payload limit");
    check(source.worksheet.size() - old_sheet_data.size() + replacement_sheet_data.size()
            > limit,
        "rewritten-over-limit fixture should exceed the limit only after sheetData replacement");
    rewrite_linked_object_source_package(source);

    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::string expected_worksheet =
        source_prefix + std::string(source_padding_size, 'w') + "-->"
        + replacement_sheet_data + "</worksheet>";
    check(expected_worksheet.size() > limit,
        "rewritten-over-limit expected worksheet should exceed the former output-size guard");

    {
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);
        replace_worksheet_sheet_data_from_single_chunk_source(
            editor, worksheet_part, replacement_sheet_data);

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
            fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
            "rewritten-over-limit direct output should rewrite worksheet");
        check_output_entry_staged_replacement_chunks(output_plan.entries,
            "xl/worksheets/sheet1.xml", true,
            "rewritten-over-limit direct output should stay file-backed staged");
        check_output_entry_materialized_replacement(output_plan.entries,
            "xl/worksheets/sheet1.xml", false,
            "rewritten-over-limit direct output should not use materialized worksheet replacement");
        check(output_plan.full_calculation_on_load,
            "rewritten-over-limit direct output should still request recalculation");
        check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Remove,
            "rewritten-over-limit direct output should still remove calcChain");

        editor.save_as(direct_output);
        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(direct_output);
        check(output_reader.read_entry("xl/worksheets/sheet1.xml") == expected_worksheet,
            "rewritten-over-limit direct output should contain the large rewritten worksheet");
        check(output_reader.find_entry("xl/calcChain.xml") == nullptr,
            "rewritten-over-limit direct output should omit stale calcChain");
        check(output_reader.read_entry("custom/opaque-extension.bin")
                == source.opaque_extension,
            "rewritten-over-limit direct output should preserve unknown extension bytes");
    }

    {
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);
        replace_worksheet_sheet_data_by_name_from_single_chunk_source(
            editor, "Sheet1", replacement_sheet_data);

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check_output_entry_staged_replacement_chunks(output_plan.entries,
            "xl/worksheets/sheet1.xml", true,
            "rewritten-over-limit by-name output should stay file-backed staged");
        check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
            "rewritten-over-limit by-name output should omit calcChain");

        editor.save_as(by_name_output);
        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(by_name_output);
        check(output_reader.read_entry("xl/worksheets/sheet1.xml") == expected_worksheet,
            "rewritten-over-limit by-name output should contain the large rewritten worksheet");
        check(output_reader.find_entry("xl/calcChain.xml") == nullptr,
            "rewritten-over-limit by-name output should omit stale calcChain");
        check(output_reader.read_entry("custom/opaque-extension.bin")
                == source.opaque_extension,
            "rewritten-over-limit by-name output should preserve unknown extension bytes");
    }
}

void test_package_editor_allows_queued_sheet_data_output_over_payload_limit()
{
    const LinkedObjectSourcePackage source = write_sheet_data_patch_source_package(
        "fastxlsx-package-editor-sheetdata-queued-over-limit-source.xlsx");
    const std::filesystem::path direct_output =
        output_path("fastxlsx-package-editor-sheetdata-queued-over-limit-direct-output.xlsx");
    const std::filesystem::path by_name_output =
        output_path("fastxlsx-package-editor-sheetdata-queued-over-limit-by-name-output.xlsx");

    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const std::size_t limit =
        fastxlsx::detail::package_editor_sheet_data_replacement_payload_byte_limit;
    const std::size_t event_window_limit =
        fastxlsx::detail::package_editor_cell_replacement_event_window_byte_limit;
    const std::string old_sheet_data =
        R"(<sheetData><row r="1"><c r="A1"><v>42</v></c></row></sheetData>)";
    const std::string replacement_sheet_data = "<sheetData/>";
    constexpr std::string_view worksheet_close = "</worksheet>";
    std::string oversized_queued_worksheet = "<worksheet>";
    append_bounded_xml_comment_padding(oversized_queued_worksheet,
        limit - replacement_sheet_data.size() - worksheet_close.size(), 'z');
    oversized_queued_worksheet += "<!--";
    oversized_queued_worksheet += std::string(32U, 'z');
    oversized_queued_worksheet += "-->";
    oversized_queued_worksheet += old_sheet_data;
    oversized_queued_worksheet += worksheet_close;
    check(oversized_queued_worksheet.size() > limit,
        "queued over-limit fixture worksheet should exceed the former payload-size output guard");
    check(oversized_queued_worksheet.size() > event_window_limit,
        "queued over-limit fixture worksheet should also exceed the event-window-sized total input");
    check(oversized_queued_worksheet.size() - old_sheet_data.size()
            + replacement_sheet_data.size() > limit,
        "queued over-limit fixture should still exceed the limit after sheetData replacement");

    std::string expected_worksheet = oversized_queued_worksheet;
    const std::size_t old_sheet_data_position = expected_worksheet.find(old_sheet_data);
    check(old_sheet_data_position != std::string::npos,
        "queued over-limit fixture should contain old sheetData");
    expected_worksheet.replace(
        old_sheet_data_position, old_sheet_data.size(), replacement_sheet_data);
    check(expected_worksheet.size() > limit,
        "queued over-limit expected worksheet should exceed the former output-size guard");

    {
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);
        replace_worksheet_part_from_single_chunk_source(
            editor, worksheet_part, oversized_queued_worksheet);
        replace_worksheet_sheet_data_from_single_chunk_source(
            editor, worksheet_part, replacement_sheet_data);

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
            fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
            "queued over-limit direct output should local-DOM-rewrite worksheet");
        check_output_entry_staged_replacement_chunks(output_plan.entries,
            "xl/worksheets/sheet1.xml", true,
            "queued over-limit direct output should stay file-backed staged");
        check_output_entry_materialized_replacement(output_plan.entries,
            "xl/worksheets/sheet1.xml", false,
            "queued over-limit direct output should not use materialized worksheet replacement");
        check(output_plan.full_calculation_on_load,
            "queued over-limit direct output should preserve recalculation request");
        check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Remove,
            "queued over-limit direct output should preserve calcChain removal policy");

        editor.save_as(direct_output);
        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(direct_output);
        check(output_reader.read_entry("xl/worksheets/sheet1.xml") == expected_worksheet,
            "queued over-limit direct output should contain rewritten staged worksheet");
        check(output_reader.find_entry("xl/calcChain.xml") == nullptr,
            "queued over-limit direct output should keep calcChain omitted");
        check(output_reader.read_entry("custom/opaque-extension.bin")
                == source.opaque_extension,
            "queued over-limit direct output should preserve unknown extension bytes");
    }

    {
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);
        replace_worksheet_part_from_single_chunk_source(
            editor, worksheet_part, oversized_queued_worksheet);
        replace_worksheet_sheet_data_by_name_from_single_chunk_source(
            editor, "Sheet1", replacement_sheet_data);

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check_output_entry_staged_replacement_chunks(output_plan.entries,
            "xl/worksheets/sheet1.xml", true,
            "queued over-limit by-name output should stay file-backed staged");
        check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
            "queued over-limit by-name output should omit calcChain");

        editor.save_as(by_name_output);
        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(by_name_output);
        check(output_reader.read_entry("xl/worksheets/sheet1.xml") == expected_worksheet,
            "queued over-limit by-name output should contain rewritten staged worksheet");
        check(output_reader.find_entry("xl/calcChain.xml") == nullptr,
            "queued over-limit by-name output should keep calcChain omitted");
        check(output_reader.read_entry("custom/opaque-extension.bin")
                == source.opaque_extension,
            "queued over-limit by-name output should preserve unknown extension bytes");
    }
}

void test_package_editor_allows_rewritten_queued_sheet_data_output_over_payload_limit()
{
    const LinkedObjectSourcePackage source = write_sheet_data_patch_source_package(
        "fastxlsx-package-editor-sheetdata-queued-rewritten-over-limit-source.xlsx");
    const std::filesystem::path direct_output =
        output_path("fastxlsx-package-editor-sheetdata-queued-rewritten-over-limit-direct-output.xlsx");
    const std::filesystem::path by_name_output =
        output_path("fastxlsx-package-editor-sheetdata-queued-rewritten-over-limit-by-name-output.xlsx");

    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const std::size_t limit =
        fastxlsx::detail::package_editor_sheet_data_replacement_payload_byte_limit;
    const std::size_t event_window_limit =
        fastxlsx::detail::package_editor_cell_replacement_event_window_byte_limit;
    const std::string old_sheet_data =
        R"(<sheetData><row r="1"><c r="A1"><v>42</v></c></row></sheetData>)";
    constexpr std::string_view worksheet_close = "</worksheet>";
    std::string queued_worksheet = "<worksheet>";
    append_bounded_xml_comment_padding(queued_worksheet,
        limit - old_sheet_data.size() - worksheet_close.size() - 8U, 'q');
    queued_worksheet += old_sheet_data;
    queued_worksheet += worksheet_close;
    const std::string replacement_sheet_data = std::string("<sheetData><!--")
        + std::string(old_sheet_data.size() + 32U, 's') + "--></sheetData>";
    check(queued_worksheet.size() < limit,
        "queued rewritten-over-limit fixture worksheet should stay within the payload-limit-sized fixture");
    check(queued_worksheet.size() < event_window_limit,
        "queued rewritten-over-limit fixture worksheet should stay within the event-reader window");
    check(replacement_sheet_data.size() < limit,
        "queued rewritten-over-limit fixture payload should stay within the payload limit");
    check(queued_worksheet.size() - old_sheet_data.size() + replacement_sheet_data.size()
            > limit,
        "queued rewritten-over-limit fixture should exceed the limit only after sheetData replacement");

    std::string expected_worksheet = queued_worksheet;
    const std::size_t old_sheet_data_position = expected_worksheet.find(old_sheet_data);
    check(old_sheet_data_position != std::string::npos,
        "queued rewritten-over-limit fixture should contain old sheetData");
    expected_worksheet.replace(
        old_sheet_data_position, old_sheet_data.size(), replacement_sheet_data);
    check(expected_worksheet.size() > limit,
        "queued rewritten-over-limit expected worksheet should exceed the former output-size guard");

    {
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);
        replace_worksheet_part_from_single_chunk_source(
            editor, worksheet_part, queued_worksheet);
        replace_worksheet_sheet_data_from_single_chunk_source(
            editor, worksheet_part, replacement_sheet_data);

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
            fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
            "queued rewritten-over-limit direct output should local-DOM-rewrite worksheet");
        check_output_entry_staged_replacement_chunks(output_plan.entries,
            "xl/worksheets/sheet1.xml", true,
            "queued rewritten-over-limit direct output should stay file-backed staged");
        check_output_entry_materialized_replacement(output_plan.entries,
            "xl/worksheets/sheet1.xml", false,
            "queued rewritten-over-limit direct output should not use materialized worksheet replacement");
        check(output_plan.full_calculation_on_load,
            "queued rewritten-over-limit direct output should preserve recalculation request");
        check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Remove,
            "queued rewritten-over-limit direct output should preserve calcChain removal policy");

        editor.save_as(direct_output);
        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(direct_output);
        check(output_reader.read_entry("xl/worksheets/sheet1.xml") == expected_worksheet,
            "queued rewritten-over-limit direct output should contain the large rewritten worksheet");
        check(output_reader.find_entry("xl/calcChain.xml") == nullptr,
            "queued rewritten-over-limit direct output should keep calcChain omitted");
        check(output_reader.read_entry("custom/opaque-extension.bin")
                == source.opaque_extension,
            "queued rewritten-over-limit direct output should preserve unknown extension bytes");
    }

    {
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);
        replace_worksheet_part_from_single_chunk_source(
            editor, worksheet_part, queued_worksheet);
        replace_worksheet_sheet_data_by_name_from_single_chunk_source(
            editor, "Sheet1", replacement_sheet_data);

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check_output_entry_staged_replacement_chunks(output_plan.entries,
            "xl/worksheets/sheet1.xml", true,
            "queued rewritten-over-limit by-name output should stay file-backed staged");
        check_output_entry_materialized_replacement(output_plan.entries,
            "xl/worksheets/sheet1.xml", false,
            "queued rewritten-over-limit by-name output should not use materialized worksheet replacement");
        check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
            "queued rewritten-over-limit by-name output should omit calcChain");

        editor.save_as(by_name_output);
        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(by_name_output);
        check(output_reader.read_entry("xl/worksheets/sheet1.xml") == expected_worksheet,
            "queued rewritten-over-limit by-name output should contain the large rewritten worksheet");
        check(output_reader.find_entry("xl/calcChain.xml") == nullptr,
            "queued rewritten-over-limit by-name output should keep calcChain omitted");
        check(output_reader.read_entry("custom/opaque-extension.bin")
                == source.opaque_extension,
            "queued rewritten-over-limit by-name output should preserve unknown extension bytes");
    }
}


} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor sheetdata shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "sheetdata-guards")) {
            test_package_editor_replaces_self_closing_source_sheet_data();
            test_package_editor_replaces_prefixed_sheet_data();
            test_package_editor_replaces_with_self_closing_sheet_data_payload();
            test_package_editor_sheet_data_patch_uses_queued_worksheet_replacement();
            test_package_editor_sheet_data_patch_replaces_self_closing_queued_worksheet();
            test_package_editor_rejects_worksheet_sheet_data_replacement_without_state_changes();
            test_package_editor_rejects_sheet_data_input_event_window_over_limit_without_state_changes();
            test_package_editor_rejects_sheet_data_payload_over_limit_without_state_changes();
            test_package_editor_allows_rewritten_sheet_data_output_over_payload_limit();
            test_package_editor_allows_queued_sheet_data_output_over_payload_limit();
            test_package_editor_allows_rewritten_queued_sheet_data_output_over_payload_limit();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
