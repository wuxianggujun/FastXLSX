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
    return shard == "all" || shard == "preservation-removal";
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
void test_package_editor_removes_unknown_extension_and_omits_owner_relationships()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-opaque-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-opaque-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    editor.remove_part(opaque_extension_part, "explicit opaque extension removal");

    check(editor.edit_plan().find_part(opaque_extension_part) == nullptr,
        "explicit unknown extension removal should clear the active edit-plan part");
    const auto* removed_opaque = editor.edit_plan().find_removed_part(opaque_extension_part);
    check(removed_opaque != nullptr,
        "explicit unknown extension removal should record removed-part audit");
    check(removed_opaque->reason.find("opaque extension") != std::string::npos,
        "explicit unknown extension removal should retain the removal reason");
    check(removed_opaque->reason.find("inbound relationship preserved")
            != std::string::npos,
        "explicit unknown extension removal should audit preserved inbound relationships");
    check(removed_opaque->reason.find("/xl/worksheets/sheet1.xml")
            != std::string::npos,
        "explicit unknown extension removal inbound audit should include owner part");
    check(removed_opaque->reason.find("rId9") != std::string::npos,
        "explicit unknown extension removal inbound audit should include relationship id");
    check(removed_opaque->reason.find("../../custom/opaque-extension.bin")
            != std::string::npos,
        "explicit unknown extension removal inbound audit should include original target");
    check(removed_opaque->inbound_relationships.size() == 1,
        "explicit unknown extension removal should keep structured inbound audit");
    const auto& opaque_inbound = removed_opaque->inbound_relationships.front();
    check(opaque_inbound.owner_part == worksheet_part.value(),
        "explicit unknown extension removal should keep inbound owner part");
    check(opaque_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "explicit unknown extension removal should keep inbound owner relationship entry");
    check(opaque_inbound.relationship_id == "rId9",
        "explicit unknown extension removal should keep inbound relationship id");
    check(opaque_inbound.relationship_type
            == "https://fastxlsx.invalid/relationships/opaque-extension",
        "explicit unknown extension removal should keep inbound relationship type");
    check(opaque_inbound.relationship_target == "../../custom/opaque-extension.bin",
        "explicit unknown extension removal should keep inbound raw target");
    check(opaque_inbound.target_part == opaque_extension_part,
        "explicit unknown extension removal should keep normalized target part");
    check(editor.manifest().find_part(opaque_extension_part) == nullptr,
        "explicit unknown extension removal should remove the part from the manifest");
    const auto* removed_opaque_relationships =
        editor.edit_plan().find_removed_package_entry("custom/_rels/opaque-extension.bin.rels");
    check(removed_opaque_relationships != nullptr,
        "explicit unknown extension removal should omit source-owned relationships");
    check(removed_opaque_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "explicit unknown extension removal should keep source relationship audit role");
    check(removed_opaque_relationships->owner_part == opaque_extension_part.value(),
        "explicit unknown extension removal should keep owner part in removed relationship audit");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "explicit unknown extension removal should not rewrite content types when only default applies");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "explicit unknown extension removal should not rewrite package relationships");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "explicit unknown extension removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "explicit unknown extension removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "explicit unknown extension removal output plan should not invent dependency audits");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "explicit unknown extension removal output plan should omit target part");
    check_output_entry_part_context(output_plan.entries, "custom/opaque-extension.bin",
        true, opaque_extension_part.value(),
        "explicit unknown extension removal output plan should classify omitted target");
    const auto* output_opaque_plan =
        find_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin");
    check(output_opaque_plan->reason.find("opaque extension") != std::string::npos,
        "explicit unknown extension removal output plan should keep removal reason");
    check(output_opaque_plan->inbound_relationships.size() == 1,
        "explicit unknown extension removal output plan should expose inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "custom/opaque-extension.bin", worksheet_part.value(),
        "xl/worksheets/_rels/sheet1.xml.rels", "rId9",
        "https://fastxlsx.invalid/relationships/opaque-extension",
        "../../custom/opaque-extension.bin", opaque_extension_part,
        "explicit unknown extension removal output plan should keep inbound context");
    check_output_entry_plan(output_plan.entries,
        "custom/_rels/opaque-extension.bin.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "explicit unknown extension removal output plan should omit owner relationships");
    check_output_entry_part_context(output_plan.entries,
        "custom/_rels/opaque-extension.bin.rels", false, "",
        "explicit unknown extension removal output plan should classify owner relationships as metadata");
    const auto* output_opaque_relationships_plan = find_output_entry_plan(
        output_plan.entries, "custom/_rels/opaque-extension.bin.rels");
    check(output_opaque_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "explicit unknown extension removal output plan should classify owner relationships metadata");
    check(output_opaque_relationships_plan->owner_part == opaque_extension_part.value(),
        "explicit unknown extension removal output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "explicit unknown extension removal output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false,
        "",
        "explicit unknown extension removal output plan should not classify content types as package part");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "explicit unknown extension removal output plan should preserve package relationships");
    check_output_entry_part_context(output_plan.entries, "_rels/.rels", false, "",
        "explicit unknown extension removal output plan should not classify package relationships as package part");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "explicit unknown extension removal output plan should preserve inbound worksheet relationships");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("custom/opaque-extension.bin") == entries.end(),
        "explicit unknown extension removal output should omit target part");
    check(entries.find("custom/_rels/opaque-extension.bin.rels") == entries.end(),
        "explicit unknown extension removal output should omit target owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "explicit unknown extension removal should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "explicit unknown extension removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "explicit unknown extension removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "explicit unknown extension removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "explicit unknown extension removal should not prune inbound worksheet relationships");
    check(output_reader.relationships_for(opaque_extension_part) == nullptr,
        "explicit unknown extension removal should not keep owner relationships for absent part");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "explicit unknown extension removal should keep worksheet relationships readable");
    const auto* opaque_link = worksheet_relationships->find_by_id("rId9");
    check(opaque_link != nullptr,
        "explicit unknown extension removal should keep inbound unknown relationship id");
    check(opaque_link->target == "../../custom/opaque-extension.bin",
        "explicit unknown extension removal should not rewrite inbound relationship target");
    check(output_reader.content_types().default_for("bin") != nullptr,
        "explicit unknown extension removal should keep BIN default content type");
}

void test_package_editor_removes_workbook_and_preserves_package_links()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-workbook-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-workbook-output.xlsx");

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

    editor.remove_part(workbook_part, "explicit workbook part removal");

    check(editor.edit_plan().find_part(workbook_part) == nullptr,
        "explicit workbook removal should clear the active edit-plan part");
    const auto* removed_workbook = editor.edit_plan().find_removed_part(workbook_part);
    check(removed_workbook != nullptr,
        "explicit workbook removal should record removed-part audit");
    check(removed_workbook->reason.find("workbook part") != std::string::npos,
        "explicit workbook removal should retain the removal reason");
    check(removed_workbook->reason.find("inbound relationship preserved")
            != std::string::npos,
        "explicit workbook removal should audit preserved inbound relationships");
    check(removed_workbook->reason.find("package _rels/.rels") != std::string::npos
            || removed_workbook->reason.find("_rels/.rels") != std::string::npos,
        "explicit workbook removal inbound audit should include package relationships entry");
    check(removed_workbook->reason.find("rId1") != std::string::npos,
        "explicit workbook removal inbound audit should include package relationship id");
    check(removed_workbook->reason.find("xl/workbook.xml") != std::string::npos,
        "explicit workbook removal inbound audit should include package target");
    check(removed_workbook->inbound_relationships.size() == 1,
        "explicit workbook removal should keep one structured inbound audit");
    const auto& inbound = removed_workbook->inbound_relationships.front();
    check(inbound.owner_part.empty(),
        "explicit workbook removal should keep package inbound owner part empty");
    check(inbound.owner_entry == "_rels/.rels",
        "explicit workbook removal should keep package relationships entry");
    check(inbound.relationship_id == "rId1",
        "explicit workbook removal should keep package relationship id");
    check(inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument",
        "explicit workbook removal should keep package relationship type");
    check(inbound.relationship_target == "xl/workbook.xml",
        "explicit workbook removal should keep package raw target");
    check(inbound.target_part == workbook_part,
        "explicit workbook removal should keep normalized workbook target part");
    check(editor.manifest().find_part(workbook_part) == nullptr,
        "explicit workbook removal should remove the part from the manifest");
    check(editor.manifest().content_types().override_for(workbook_part) == nullptr,
        "explicit workbook removal should remove the workbook content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "explicit workbook removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "explicit workbook removal content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "explicit workbook removal content types audit should keep structured role");
    const auto* removed_workbook_relationships =
        editor.edit_plan().find_removed_package_entry("xl/_rels/workbook.xml.rels");
    check(removed_workbook_relationships != nullptr,
        "explicit workbook removal should omit source-owned workbook relationships");
    check(removed_workbook_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "explicit workbook owner relationships omission should keep source relationship role");
    check(removed_workbook_relationships->owner_part == workbook_part.value(),
        "explicit workbook owner relationships omission should keep owner part");
    check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels") == nullptr,
        "explicit workbook removal should not keep active owner relationships audit");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "explicit workbook removal should not rewrite package relationships");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit workbook removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit workbook removal should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit workbook removal should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit workbook removal should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit workbook removal should keep table copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit workbook removal should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit workbook removal should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit workbook removal should keep VBA copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit workbook removal should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit workbook removal should keep unknown extension copy-original");

    const std::vector<fastxlsx::detail::PackageEditorOutputEntryPlan> output_plan =
        editor.planned_output_entries();
    check_output_entry_plan(output_plan, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "explicit workbook removal output plan should omit workbook part");
    check_output_entry_part_context(output_plan, "xl/workbook.xml", true,
        "/xl/workbook.xml",
        "explicit workbook removal output plan should classify omitted workbook as package part");
    check_output_entry_plan(output_plan, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "explicit workbook removal output plan should omit workbook owner relationships");
    check_output_entry_part_context(output_plan, "xl/_rels/workbook.xml.rels", false, "",
        "explicit workbook removal output plan should classify owner relationships as metadata entry");
    const auto* output_workbook_relationships_plan =
        find_output_entry_plan(output_plan, "xl/_rels/workbook.xml.rels");
    check(output_workbook_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "explicit workbook removal output plan should classify owner relationships omission");
    check(output_workbook_relationships_plan->owner_part == workbook_part.value(),
        "explicit workbook removal output plan should keep owner relationships context");
    check_output_entry_plan(output_plan, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "explicit workbook removal output plan should rewrite content types");
    check_output_entry_part_context(output_plan, "[Content_Types].xml", false, "",
        "explicit workbook removal output plan should classify content types as metadata entry");
    check_output_entry_plan(output_plan, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "explicit workbook removal output plan should preserve package relationships");
    check_output_entry_part_context(output_plan, "_rels/.rels", false, "",
        "explicit workbook removal output plan should classify package relationships as metadata entry");
    check_output_entry_plan(output_plan, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "explicit workbook removal output plan should preserve unknown extension");
    check_output_entry_part_context(output_plan, "custom/opaque-extension.bin", true,
        "/custom/opaque-extension.bin",
        "explicit workbook removal output plan should classify unknown extension as package part");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/workbook.xml") == entries.end(),
        "explicit workbook removal output should omit workbook part");
    check(entries.find("xl/_rels/workbook.xml.rels") == entries.end(),
        "explicit workbook removal output should omit workbook owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(workbook_part) == nullptr,
        "explicit workbook removal output should remove workbook content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/workbook.xml",
        "explicit workbook removal content types XML should omit workbook override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "explicit workbook removal should preserve package relationships bytes");
    check(output_reader.relationships_for(workbook_part) == nullptr,
        "explicit workbook removal should not keep owner relationships for absent workbook");
    const auto& package_relationships = output_reader.package_relationships();
    const auto* workbook_link = package_relationships.find_by_id("rId1");
    check(workbook_link != nullptr,
        "explicit workbook removal should keep inbound workbook relationship id");
    check(workbook_link->target == "xl/workbook.xml",
        "explicit workbook removal should not rewrite inbound workbook target");
    check(workbook_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "explicit workbook removal should keep inbound workbook target mode");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "explicit workbook removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "explicit workbook removal should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "explicit workbook removal should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "explicit workbook removal should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "explicit workbook removal should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "explicit workbook removal should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "explicit workbook removal should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "explicit workbook removal should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "explicit workbook removal should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "explicit workbook removal should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "explicit workbook removal should preserve VBA bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "explicit workbook removal should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "explicit workbook removal should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "explicit workbook removal should preserve unknown extension relationships bytes");
    check(output_reader.content_types().override_for(worksheet_part) != nullptr,
        "explicit workbook removal should keep worksheet content type override");
    check(output_reader.content_types().override_for(drawing_part) != nullptr,
        "explicit workbook removal should keep drawing content type override");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "explicit workbook removal should keep chart content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "explicit workbook removal should keep table content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "explicit workbook removal should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "explicit workbook removal should keep styles content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "explicit workbook removal should keep VBA content type override");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "explicit workbook removal should keep calcChain content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "explicit workbook removal should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "explicit workbook removal should not promote PNG media default to override");
}

void test_package_editor_removes_worksheet_and_preserves_workbook_links()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-worksheet-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-worksheet-output.xlsx");

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

    editor.remove_part(worksheet_part, "explicit worksheet part removal");

    check(editor.edit_plan().find_part(worksheet_part) == nullptr,
        "explicit worksheet removal should clear the active edit-plan part");
    const auto* removed_worksheet = editor.edit_plan().find_removed_part(worksheet_part);
    check(removed_worksheet != nullptr,
        "explicit worksheet removal should record removed-part audit");
    check(removed_worksheet->reason.find("worksheet part") != std::string::npos,
        "explicit worksheet removal should retain the removal reason");
    check(removed_worksheet->reason.find("inbound relationship preserved")
            != std::string::npos,
        "explicit worksheet removal should audit preserved inbound relationships");
    check(removed_worksheet->reason.find("/xl/workbook.xml") != std::string::npos,
        "explicit worksheet removal inbound audit should include workbook owner part");
    check(removed_worksheet->reason.find("rId1") != std::string::npos,
        "explicit worksheet removal inbound audit should include workbook relationship id");
    check(removed_worksheet->reason.find("worksheets/sheet1.xml") != std::string::npos,
        "explicit worksheet removal inbound audit should include workbook target");
    check(removed_worksheet->inbound_relationships.size() == 1,
        "explicit worksheet removal should keep one structured inbound audit");
    const auto& inbound = removed_worksheet->inbound_relationships.front();
    check(inbound.owner_part == workbook_part.value(),
        "explicit worksheet removal should keep workbook owner part");
    check(inbound.owner_entry == "xl/_rels/workbook.xml.rels",
        "explicit worksheet removal should keep workbook relationships entry");
    check(inbound.relationship_id == "rId1",
        "explicit worksheet removal should keep workbook relationship id");
    check(inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet",
        "explicit worksheet removal should keep workbook relationship type");
    check(inbound.relationship_target == "worksheets/sheet1.xml",
        "explicit worksheet removal should keep workbook raw target");
    check(inbound.target_part == worksheet_part,
        "explicit worksheet removal should keep normalized worksheet target part");
    check(editor.manifest().find_part(worksheet_part) == nullptr,
        "explicit worksheet removal should remove the part from the manifest");
    check(editor.manifest().content_types().override_for(worksheet_part) == nullptr,
        "explicit worksheet removal should remove the worksheet content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "explicit worksheet removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "explicit worksheet removal content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "explicit worksheet removal content types audit should keep structured role");
    const auto* removed_worksheet_relationships =
        editor.edit_plan().find_removed_package_entry("xl/worksheets/_rels/sheet1.xml.rels");
    check(removed_worksheet_relationships != nullptr,
        "explicit worksheet removal should omit source-owned worksheet relationships");
    check(removed_worksheet_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "explicit worksheet owner relationships omission should keep source relationship role");
    check(removed_worksheet_relationships->owner_part == worksheet_part.value(),
        "explicit worksheet owner relationships omission should keep owner part");
    check(editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == nullptr,
        "explicit worksheet removal should not keep active owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit worksheet removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit worksheet removal should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit worksheet removal should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit worksheet removal should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit worksheet removal should keep table copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit worksheet removal should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit worksheet removal should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit worksheet removal should keep VBA copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit worksheet removal should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit worksheet removal should keep unknown extension copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.removed_parts.size() == editor.edit_plan().removed_parts().size(),
        "explicit worksheet removal output plan should mirror removed-part audits");
    const auto output_removed_worksheet =
        std::find_if(output_plan.removed_parts.begin(), output_plan.removed_parts.end(),
            [&](const fastxlsx::detail::EditPlanRemovedPart& removed_part) {
                return removed_part.part_name == worksheet_part;
            });
    check(output_removed_worksheet != output_plan.removed_parts.end(),
        "explicit worksheet removal output plan should expose removed worksheet audit");
    check(output_removed_worksheet->reason.find("worksheet part") != std::string::npos,
        "explicit worksheet removal output plan should keep removed worksheet reason");
    check(output_removed_worksheet->inbound_relationships.size() == 1,
        "explicit worksheet removal output plan should keep removed worksheet inbound audit");
    check(output_removed_worksheet->inbound_relationships.front().owner_part
            == workbook_part.value(),
        "explicit worksheet removal output plan should keep removed worksheet inbound owner");
    check(output_removed_worksheet->inbound_relationships.front().relationship_id == "rId1",
        "explicit worksheet removal output plan should keep removed worksheet inbound id");
    check(output_plan.removed_package_entries.size()
            == editor.edit_plan().removed_package_entries().size(),
        "explicit worksheet removal output plan should mirror removed package-entry audits");
    const auto output_removed_worksheet_relationships =
        std::find_if(output_plan.removed_package_entries.begin(),
            output_plan.removed_package_entries.end(),
            [](const fastxlsx::detail::EditPlanRemovedPackageEntry& removed_entry) {
                return removed_entry.entry_name == "xl/worksheets/_rels/sheet1.xml.rels";
            });
    check(output_removed_worksheet_relationships
            != output_plan.removed_package_entries.end(),
        "explicit worksheet removal output plan should expose omitted worksheet relationships audit");
    check(output_removed_worksheet_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "explicit worksheet removal output plan should keep omitted worksheet relationships role");
    check(output_removed_worksheet_relationships->owner_part == worksheet_part.value(),
        "explicit worksheet removal output plan should keep omitted worksheet relationships owner");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/worksheets/sheet1.xml") == entries.end(),
        "explicit worksheet removal output should omit worksheet part");
    check(entries.find("xl/worksheets/_rels/sheet1.xml.rels") == entries.end(),
        "explicit worksheet removal output should omit worksheet owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(worksheet_part) == nullptr,
        "explicit worksheet removal output should remove worksheet content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/worksheets/sheet1.xml",
        "explicit worksheet removal content types XML should omit worksheet override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "explicit worksheet removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "explicit worksheet removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "explicit worksheet removal should preserve workbook relationships bytes");
    check(output_reader.relationships_for(worksheet_part) == nullptr,
        "explicit worksheet removal should not keep owner relationships for absent worksheet");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "explicit worksheet removal should keep workbook relationships readable");
    const auto* worksheet_link = workbook_relationships->find_by_id("rId1");
    check(worksheet_link != nullptr,
        "explicit worksheet removal should keep inbound worksheet relationship id");
    check(worksheet_link->target == "worksheets/sheet1.xml",
        "explicit worksheet removal should not rewrite inbound worksheet target");
    check(worksheet_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "explicit worksheet removal should keep inbound worksheet target mode");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "explicit worksheet removal should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "explicit worksheet removal should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "explicit worksheet removal should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "explicit worksheet removal should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "explicit worksheet removal should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "explicit worksheet removal should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "explicit worksheet removal should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "explicit worksheet removal should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "explicit worksheet removal should preserve VBA bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "explicit worksheet removal should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "explicit worksheet removal should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "explicit worksheet removal should preserve unknown extension relationships bytes");
    check(output_reader.content_types().override_for(drawing_part) != nullptr,
        "explicit worksheet removal should keep drawing content type override");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "explicit worksheet removal should keep chart content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "explicit worksheet removal should keep table content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "explicit worksheet removal should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "explicit worksheet removal should keep styles content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "explicit worksheet removal should keep VBA content type override");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "explicit worksheet removal should keep calcChain content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "explicit worksheet removal should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "explicit worksheet removal should not promote PNG media default to override");
}

void test_package_editor_removes_drawing_and_omits_owner_relationships()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-drawing-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-drawing-output.xlsx");

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

    editor.remove_part(drawing_part, "explicit drawing part removal");

    check(editor.edit_plan().find_part(drawing_part) == nullptr,
        "explicit drawing removal should clear the active edit-plan part");
    const auto* removed_drawing = editor.edit_plan().find_removed_part(drawing_part);
    check(removed_drawing != nullptr,
        "explicit drawing removal should record removed-part audit");
    check(removed_drawing->reason.find("drawing part") != std::string::npos,
        "explicit drawing removal should retain the removal reason");
    check(removed_drawing->reason.find("inbound relationship preserved")
            != std::string::npos,
        "explicit drawing removal should audit preserved inbound relationships");
    check(removed_drawing->reason.find("/xl/worksheets/sheet1.xml")
            != std::string::npos,
        "explicit drawing removal inbound audit should include owner part");
    check(removed_drawing->reason.find("rId1") != std::string::npos,
        "explicit drawing removal inbound audit should include direct relationship id");
    check(removed_drawing->reason.find("rId5") != std::string::npos,
        "explicit drawing removal inbound audit should include URI-qualified relationship id");
    check(removed_drawing->reason.find("../drawings/drawing1.xml")
            != std::string::npos,
        "explicit drawing removal inbound audit should include direct target");
    check(removed_drawing->reason.find("../drawings/drawing1.xml#shape1")
            != std::string::npos,
        "explicit drawing removal inbound audit should include URI-qualified target");
    check(removed_drawing->inbound_relationships.size() == 2,
        "explicit drawing removal should keep structured inbound audits");
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
        "explicit drawing removal should keep direct drawing inbound audit");
    check(direct_inbound->owner_part == worksheet_part.value(),
        "explicit drawing removal should keep direct inbound owner part");
    check(direct_inbound->owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "explicit drawing removal should keep direct inbound owner relationship entry");
    check(direct_inbound->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
        "explicit drawing removal should keep direct inbound relationship type");
    check(direct_inbound->relationship_target == "../drawings/drawing1.xml",
        "explicit drawing removal should keep direct inbound raw target");
    check(direct_inbound->target_part == drawing_part,
        "explicit drawing removal should keep direct normalized target part");
    check(fragment_inbound != nullptr,
        "explicit drawing removal should keep URI-qualified drawing inbound audit");
    check(fragment_inbound->owner_part == worksheet_part.value(),
        "explicit drawing removal should keep URI-qualified inbound owner part");
    check(fragment_inbound->owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "explicit drawing removal should keep URI-qualified inbound owner relationship entry");
    check(fragment_inbound->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
        "explicit drawing removal should keep URI-qualified inbound relationship type");
    check(fragment_inbound->relationship_target == "../drawings/drawing1.xml#shape1",
        "explicit drawing removal should keep URI-qualified raw target");
    check(fragment_inbound->target_part == drawing_part,
        "explicit drawing removal should keep URI-qualified normalized target part");
    check(editor.manifest().find_part(drawing_part) == nullptr,
        "explicit drawing removal should remove the part from the manifest");
    check(editor.manifest().content_types().override_for(drawing_part) == nullptr,
        "explicit drawing removal should remove the manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "explicit drawing removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "explicit drawing removal content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "explicit drawing removal content types audit should keep structured role");
    const auto* removed_drawing_relationships =
        editor.edit_plan().find_removed_package_entry("xl/drawings/_rels/drawing1.xml.rels");
    check(removed_drawing_relationships != nullptr,
        "explicit drawing removal should omit source-owned drawing relationships");
    check(removed_drawing_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "explicit drawing owner relationships omission should keep source relationship role");
    check(removed_drawing_relationships->owner_part == drawing_part.value(),
        "explicit drawing owner relationships omission should keep owner part");
    check(editor.edit_plan().find_package_entry("xl/drawings/_rels/drawing1.xml.rels")
            == nullptr,
        "explicit drawing removal should not keep active owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit drawing removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit drawing removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit drawing removal should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit drawing removal should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit drawing removal should keep table copy-original");
    check(editor.edit_plan().find_part(vml_drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit drawing removal should keep VML drawing copy-original");
    check(editor.edit_plan().find_part(percent_encoded_drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit drawing removal should keep percent-decoded drawing copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit drawing removal should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit drawing removal should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit drawing removal should keep VBA copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit drawing removal should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit drawing removal should keep unknown extension copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/drawings/drawing1.xml") == entries.end(),
        "explicit drawing removal output should omit drawing part");
    check(entries.find("xl/drawings/_rels/drawing1.xml.rels") == entries.end(),
        "explicit drawing removal output should omit drawing owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(drawing_part) == nullptr,
        "explicit drawing removal output should remove drawing content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/drawings/drawing1.xml",
        "explicit drawing removal content types XML should omit drawing override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "explicit drawing removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "explicit drawing removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "explicit drawing removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "explicit drawing removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "explicit drawing removal should not prune inbound worksheet relationships");
    check(output_reader.relationships_for(drawing_part) == nullptr,
        "explicit drawing removal should not keep owner relationships for absent drawing");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "explicit drawing removal should keep worksheet relationships readable");
    const auto* drawing_link = worksheet_relationships->find_by_id("rId1");
    check(drawing_link != nullptr,
        "explicit drawing removal should keep inbound drawing relationship id");
    check(drawing_link->target == "../drawings/drawing1.xml",
        "explicit drawing removal should not rewrite inbound drawing target");
    check(drawing_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "explicit drawing removal should keep inbound drawing target mode");
    const auto* fragment_link = worksheet_relationships->find_by_id("rId5");
    check(fragment_link != nullptr,
        "explicit drawing removal should keep URI-qualified inbound drawing relationship id");
    check(fragment_link->target == "../drawings/drawing1.xml#shape1",
        "explicit drawing removal should not rewrite URI-qualified inbound drawing target");
    check(fragment_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "explicit drawing removal should keep URI-qualified inbound drawing target mode");
    check(worksheet_relationships->find_by_id("rId3") != nullptr,
        "explicit drawing removal should keep worksheet table relationship");
    check(worksheet_relationships->find_by_id("rId7") != nullptr,
        "explicit drawing removal should keep worksheet VML relationship");
    check(worksheet_relationships->find_by_id("rId8") != nullptr,
        "explicit drawing removal should keep percent-encoded drawing relationship");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "explicit drawing removal should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "explicit drawing removal should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "explicit drawing removal should preserve table bytes");
    check(output_reader.read_entry("xl/drawings/vmlDrawing1.vml") == source.vml_drawing,
        "explicit drawing removal should preserve VML bytes");
    check(output_reader.read_entry("xl/drawings/drawing space.xml")
            == source.percent_encoded_drawing,
        "explicit drawing removal should preserve percent-decoded drawing bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "explicit drawing removal should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "explicit drawing removal should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "explicit drawing removal should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "explicit drawing removal should preserve VBA bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "explicit drawing removal should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "explicit drawing removal should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "explicit drawing removal should preserve unknown extension relationships bytes");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "explicit drawing removal should keep chart content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "explicit drawing removal should keep table content type override");
    check(output_reader.content_types().override_for(vml_drawing_part) != nullptr,
        "explicit drawing removal should keep VML content type override");
    check(output_reader.content_types().override_for(percent_encoded_drawing_part) != nullptr,
        "explicit drawing removal should keep percent-decoded drawing content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "explicit drawing removal should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "explicit drawing removal should keep styles content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "explicit drawing removal should keep VBA content type override");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "explicit drawing removal should keep calcChain content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "explicit drawing removal should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "explicit drawing removal should not promote PNG media to an override");
}

void test_package_editor_removes_chart_and_rewrites_content_types_without_pruning_links()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-chart-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-chart-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");

    editor.remove_part(chart_part, "explicit chart part removal");

    check(editor.edit_plan().find_part(chart_part) == nullptr,
        "explicit chart removal should clear the active edit-plan part");
    const auto* removed_chart = editor.edit_plan().find_removed_part(chart_part);
    check(removed_chart != nullptr,
        "explicit chart removal should record removed-part audit");
    check(removed_chart->reason.find("chart part") != std::string::npos,
        "explicit chart removal should retain the removal reason");
    check(removed_chart->reason.find("inbound relationship preserved")
            != std::string::npos,
        "explicit chart removal should audit preserved inbound relationships");
    check(removed_chart->reason.find("/xl/drawings/drawing1.xml")
            != std::string::npos,
        "explicit chart removal inbound audit should include owner part");
    check(removed_chart->reason.find("rId2") != std::string::npos,
        "explicit chart removal inbound audit should include relationship id");
    check(removed_chart->reason.find("../charts/chart1.xml") != std::string::npos,
        "explicit chart removal inbound audit should include original target");
    check(removed_chart->inbound_relationships.size() == 2,
        "explicit chart removal should keep structured inbound relationship audit");
    const fastxlsx::detail::RemovedPartInboundRelationshipAudit* chart_inbound = nullptr;
    const fastxlsx::detail::RemovedPartInboundRelationshipAudit* chart_fragment_inbound = nullptr;
    for (const auto& audit : removed_chart->inbound_relationships) {
        if (audit.relationship_id == "rId2") {
            chart_inbound = &audit;
        } else if (audit.relationship_id == "rId4") {
            chart_fragment_inbound = &audit;
        }
    }
    check(chart_inbound != nullptr,
        "explicit chart removal should keep direct chart inbound audit");
    check(chart_inbound->owner_part == drawing_part.value(),
        "explicit chart removal should keep inbound owner part");
    check(chart_inbound->owner_entry == "xl/drawings/_rels/drawing1.xml.rels",
        "explicit chart removal should keep inbound owner relationship entry");
    check(chart_inbound->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart",
        "explicit chart removal should keep inbound relationship type");
    check(chart_inbound->relationship_target == "../charts/chart1.xml",
        "explicit chart removal should keep inbound raw target");
    check(chart_inbound->target_part == chart_part,
        "explicit chart removal should keep normalized target part");
    check(chart_fragment_inbound != nullptr,
        "explicit chart removal should keep URI-qualified chart inbound audit");
    check(chart_fragment_inbound->relationship_target == "../charts/chart1.xml#plotArea",
        "explicit chart removal should keep URI-qualified raw target");
    check(chart_fragment_inbound->target_part == chart_part,
        "explicit chart removal should keep URI-qualified normalized target part");
    const std::vector<fastxlsx::detail::PackageEditorOutputEntryPlan> output_plan =
        editor.planned_output_entries();
    check_output_entry_plan(output_plan, "xl/charts/chart1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "explicit chart removal output plan should omit chart source entry");
    check_output_entry_part_context(output_plan, "xl/charts/chart1.xml", true,
        chart_part.value(), "explicit chart removal output plan should keep part context");
    const auto* chart_output_plan =
        find_output_entry_plan(output_plan, "xl/charts/chart1.xml");
    check(chart_output_plan != nullptr
            && chart_output_plan->inbound_relationships.size() == 2,
        "explicit chart removal output plan should expose inbound relationship audit");
    check_output_entry_has_inbound_relationship(output_plan, "xl/charts/chart1.xml",
        drawing_part.value(), "xl/drawings/_rels/drawing1.xml.rels", "rId2",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart",
        "../charts/chart1.xml", chart_part,
        "explicit chart removal output plan should keep direct inbound relationship");
    check_output_entry_has_inbound_relationship(output_plan, "xl/charts/chart1.xml",
        drawing_part.value(), "xl/drawings/_rels/drawing1.xml.rels", "rId4",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart",
        "../charts/chart1.xml#plotArea", chart_part,
        "explicit chart removal output plan should keep URI-qualified inbound relationship");
    check(editor.manifest().find_part(chart_part) == nullptr,
        "explicit chart removal should remove the part from the manifest");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "explicit chart removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "explicit chart removal content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "explicit chart removal content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry("xl/charts/_rels/chart1.xml.rels")
            == nullptr,
        "explicit chart removal should not invent missing chart owner relationships omission");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/charts/chart1.xml") == entries.end(),
        "explicit chart removal output should omit chart part");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(chart_part) == nullptr,
        "explicit chart removal output should remove chart content type override");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "explicit chart removal should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "explicit chart removal should not prune inbound drawing relationships");
    const auto* drawing_relationships = output_reader.relationships_for(drawing_part);
    check(drawing_relationships != nullptr,
        "explicit chart removal should keep drawing relationships readable");
    const auto* chart_link = drawing_relationships->find_by_id("rId2");
    check(chart_link != nullptr,
        "explicit chart removal should keep inbound chart relationship id");
    check(chart_link->target == "../charts/chart1.xml",
        "explicit chart removal should not rewrite inbound chart relationship target");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "explicit chart removal should preserve media bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "explicit chart removal should preserve unknown extension bytes");
}

void test_package_editor_removes_media_and_preserves_drawing_links()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-media-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-media-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");

    editor.remove_part(image_part, "explicit media part removal");

    check(editor.edit_plan().find_part(image_part) == nullptr,
        "explicit media removal should clear the active edit-plan part");
    const auto* removed_image = editor.edit_plan().find_removed_part(image_part);
    check(removed_image != nullptr,
        "explicit media removal should record removed-part audit");
    check(removed_image->reason.find("media part") != std::string::npos,
        "explicit media removal should retain the removal reason");
    check(removed_image->reason.find("inbound relationship preserved")
            != std::string::npos,
        "explicit media removal should audit preserved inbound relationships");
    check(removed_image->reason.find("/xl/drawings/drawing1.xml")
            != std::string::npos,
        "explicit media removal inbound audit should include owner part");
    check(removed_image->reason.find("rId1") != std::string::npos,
        "explicit media removal inbound audit should include relationship id");
    check(removed_image->reason.find("../media/image1.png") != std::string::npos,
        "explicit media removal inbound audit should include original target");
    check(removed_image->inbound_relationships.size() == 1,
        "explicit media removal should keep structured inbound audit");
    const auto& image_inbound = removed_image->inbound_relationships.front();
    check(image_inbound.owner_part == drawing_part.value(),
        "explicit media removal should keep inbound owner part");
    check(image_inbound.owner_entry == "xl/drawings/_rels/drawing1.xml.rels",
        "explicit media removal should keep inbound owner relationship entry");
    check(image_inbound.relationship_id == "rId1",
        "explicit media removal should keep inbound relationship id");
    check(image_inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image",
        "explicit media removal should keep inbound relationship type");
    check(image_inbound.relationship_target == "../media/image1.png",
        "explicit media removal should keep inbound raw target");
    check(image_inbound.target_part == image_part,
        "explicit media removal should keep normalized target part");
    check(editor.manifest().find_part(image_part) == nullptr,
        "explicit media removal should remove the part from the manifest");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "explicit media removal should not rewrite content types when only default applies");
    check(editor.edit_plan().find_removed_package_entry("xl/media/_rels/image1.png.rels")
            == nullptr,
        "explicit media removal should not invent missing media owner relationships omission");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/media/image1.png") == entries.end(),
        "explicit media removal output should omit media part");
    check(entries.find("xl/media/_rels/image1.png.rels") == entries.end(),
        "explicit media removal output should not invent media owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "explicit media removal should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "explicit media removal should preserve package relationships bytes");
    check(output_reader.content_types().default_for("png") != nullptr,
        "explicit media removal should preserve PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "explicit media removal should not promote PNG media to an override");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "explicit media removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "explicit media removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "explicit media removal should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "explicit media removal should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "explicit media removal should not prune inbound drawing relationships");
    check(output_reader.relationships_for(image_part) == nullptr,
        "explicit media removal should not keep owner relationships for absent media");
    const auto* drawing_relationships = output_reader.relationships_for(drawing_part);
    check(drawing_relationships != nullptr,
        "explicit media removal should keep drawing relationships readable");
    const auto* image_link = drawing_relationships->find_by_id("rId1");
    check(image_link != nullptr,
        "explicit media removal should keep inbound image relationship id");
    check(image_link->target == "../media/image1.png",
        "explicit media removal should not rewrite inbound image relationship target");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "explicit media removal should preserve chart bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "explicit media removal should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "explicit media removal should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "explicit media removal should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "explicit media removal should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "explicit media removal should preserve VBA bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "explicit media removal should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "explicit media removal should preserve unknown extension relationships bytes");
}

void test_package_editor_removes_part_with_malformed_unrelated_relationship_target()
{
    SourcePackage source =
        write_source_package("fastxlsx-package-editor-remove-malformed-rel-source.xlsx");
    source.workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(<Relationship Id="rIdMalformedPercent" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing" Target="drawings/bad%ZZ.xml"/>)"
        R"(</Relationships>)";
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
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-malformed-rel-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");
    fastxlsx::detail::ReferencePolicy fail_policy;
    fail_policy.unsupported_linked_part_action =
        fastxlsx::detail::ReferencePolicyAction::Fail;

    editor.remove_part(
        unknown_part, "explicit unknown removal with malformed relationship target", fail_policy);

    const auto* removed_unknown = editor.edit_plan().find_removed_part(unknown_part);
    check(removed_unknown != nullptr,
        "malformed unrelated targets should not block explicit part removal");
    check(removed_unknown->inbound_relationships.empty(),
        "malformed unrelated targets should not be recorded as inbound removal links");
    check(has_note_containing(editor.edit_plan().notes(),
              {"invalid relationship target skipped during removed-part inbound audit",
                  "/xl/workbook.xml", "rIdMalformedPercent", "drawings/bad%ZZ.xml",
                  "percent escape is invalid"}),
        "PackageEditor should surface malformed relationship target audit notes");
    check(editor.edit_plan().relationship_target_audits().empty(),
        "malformed unrelated targets should not create structured target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().empty(),
        "malformed unrelated targets should not create worksheet reference audits");
    check(editor.edit_plan().package_entries().empty(),
        "malformed unrelated targets should not rewrite metadata package entries");
    check(editor.edit_plan().removed_package_entries().empty(),
        "malformed unrelated targets should not omit metadata package entries");
    check(!editor.edit_plan().full_calculation_on_load(),
        "malformed unrelated targets should not request full calculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "malformed unrelated targets should preserve calcChain policy");
    check(editor.manifest().find_part(unknown_part) == nullptr,
        "malformed unrelated targets should still allow removing the target from manifest");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "malformed target removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "malformed target removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "malformed target removal output plan should not add structured target audits");
    check(output_plan.worksheet_relationship_reference_audits.empty(),
        "malformed target removal output plan should not add worksheet reference audits");
    check(output_plan.removed_parts.size() == 1,
        "malformed target removal output plan should expose one removed part");
    check(output_plan.removed_parts.front().part_name == unknown_part,
        "malformed target removal output plan should expose the removed unknown part");
    check(output_plan.removed_parts.front().inbound_relationships.empty(),
        "malformed target removal output plan should not invent inbound links");
    check(has_note_containing(output_plan.notes,
              {"invalid relationship target skipped during removed-part inbound audit",
                  "/xl/workbook.xml", "rIdMalformedPercent", "drawings/bad%ZZ.xml",
                  "percent escape is invalid"}),
        "malformed target removal output plan should snapshot audit notes");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "malformed target removal output plan should omit removed unknown part");
    check_output_entry_part_context(output_plan.entries, "custom/opaque.bin",
        true, unknown_part.value(),
        "malformed target removal output plan should classify removed unknown part");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "malformed target removal output plan should preserve owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "malformed target removal output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "malformed target removal output plan should preserve package relationships");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("custom/opaque.bin") == entries.end(),
        "malformed unrelated target removal output should omit removed unknown part");
    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.find_entry("custom/opaque.bin") == nullptr,
        "malformed unrelated target removal reader should omit removed unknown part");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "malformed relationship target removal should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "malformed relationship target removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "malformed relationship target removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels") == source.workbook_relationships,
        "malformed relationship target should be byte-preserved in owner relationships");
    check(output_reader.read_entry("docProps/core.xml") == source.core_properties,
        "malformed relationship target removal should preserve core properties bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "malformed relationship target removal should preserve worksheet bytes");
}

void test_package_editor_reference_policy_fail_blocks_inbound_part_removal_without_state_changes()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package(
            "fastxlsx-package-editor-policy-fail-remove-drawing-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-policy-fail-remove-drawing-output.xlsx");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
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

    fastxlsx::detail::ReferencePolicy fail_policy;
    fail_policy.unsupported_linked_part_action =
        fastxlsx::detail::ReferencePolicyAction::Fail;

    bool failed = false;
    try {
        editor.remove_part(
            drawing_part, "strict drawing removal should fail", fail_policy);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "inbound relationships blocked by reference policy",
            "linked removal policy failure should report inbound relationship policy");
    }
    check(failed,
        "ReferencePolicyAction::Fail should reject removal of an inbound-linked drawing part");

    check(editor.edit_plan().size() == initial_plan_size,
        "linked removal policy failure should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "linked removal policy failure should not add edit plan notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "linked removal policy failure should not add relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "linked removal policy failure should not add worksheet relationship reference audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == initial_worksheet_payload_dependency_audit_count,
        "linked removal policy failure should not add worksheet payload audits");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == initial_workbook_payload_dependency_audit_count,
        "linked removal policy failure should not add workbook payload audits");
    check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
        "linked removal policy failure should not add package-entry audit");
    check(editor.edit_plan().removed_parts().size() == initial_removed_part_count,
        "linked removal policy failure should not record removed parts");
    check(editor.edit_plan().removed_package_entries().size()
            == initial_removed_package_entry_count,
        "linked removal policy failure should not record removed package entries");
    check(editor.edit_plan().full_calculation_on_load()
            == initial_full_calculation_on_load,
        "linked removal policy failure should not request full calculation");
    check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
        "linked removal policy failure should not change calcChain action");
    check(editor.edit_plan().find_removed_part(drawing_part) == nullptr,
        "linked removal policy failure should not leave stale removed-part audit");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "linked removal policy failure should not audit content types rewrite");
    check(editor.edit_plan().find_removed_package_entry("xl/drawings/_rels/drawing1.xml.rels")
            == nullptr,
        "linked removal policy failure should not omit drawing owner relationships");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked removal policy failure should leave drawing copy-original");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked removal policy failure should leave workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked removal policy failure should leave worksheet copy-original");
    check_manifest_write_mode(editor, drawing_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked removal policy failure should leave drawing manifest copy-original");
    check(editor.manifest().content_types().override_for(drawing_part) != nullptr,
        "linked removal policy failure should preserve drawing content type override");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load == initial_full_calculation_on_load,
        "linked removal policy failure output plan should not request full calculation");
    check(output_plan.calc_chain_action == initial_calc_chain_action,
        "linked removal policy failure output plan should preserve calcChain policy");
    check(output_plan.notes.size() == initial_note_count,
        "linked removal policy failure output plan should not add audit notes");
    check(output_plan.relationship_target_audits.size()
            == initial_relationship_target_audit_count,
        "linked removal policy failure output plan should not add relationship target audits");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == initial_worksheet_relationship_reference_audit_count,
        "linked removal policy failure output plan should not add worksheet reference audits");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == initial_worksheet_payload_dependency_audit_count,
        "linked removal policy failure output plan should not add worksheet payload audits");
    check(output_plan.workbook_payload_dependency_audits.size()
            == initial_workbook_payload_dependency_audit_count,
        "linked removal policy failure output plan should not add workbook payload audits");
    check(output_plan.removed_parts.size() == initial_removed_part_count,
        "linked removal policy failure output plan should not record removed parts");
    check(output_plan.removed_package_entries.size()
            == initial_removed_package_entry_count,
        "linked removal policy failure output plan should not record omitted metadata entries");
    check_output_plan_preserves_source_copy_original(editor, output_plan,
        "linked removal policy failure output plan should keep every source entry copy-original");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "linked removal policy failure output plan should classify content types as metadata entry");
    check_output_entry_part_context(output_plan.entries, "xl/drawings/drawing1.xml",
        true, drawing_part.value(),
        "linked removal policy failure output plan should classify drawing as a package part");
    check_output_entry_part_context(output_plan.entries,
        "xl/drawings/_rels/drawing1.xml.rels", false, "",
        "linked removal policy failure output plan should classify drawing relationships as metadata entry");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(source_reader, output_reader);
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "linked removal policy failure output should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "linked removal policy failure output should preserve drawing relationships bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "linked removal policy failure output should preserve unknown extension bytes");
}

void test_package_editor_reference_policy_fail_preserves_prior_replacement_for_inbound_removal()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package(
            "fastxlsx-package-editor-policy-fail-remove-drawing-prior-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-policy-fail-remove-drawing-prior-output.xlsx");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");

    const std::string replacement_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Queued" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, replacement_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "queued workbook replacement before strict drawing removal");

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
    const std::size_t queued_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t queued_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const bool queued_full_calculation_on_load =
        editor.edit_plan().full_calculation_on_load();
    const fastxlsx::detail::CalcChainAction queued_calc_chain_action =
        editor.edit_plan().calc_chain_action();

    const auto* queued_workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(queued_workbook_plan != nullptr
            && queued_workbook_plan->write_mode
                == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "queued inbound-removal fixture should record prior workbook replacement");
    check(queued_workbook_plan->reason.find("queued workbook replacement")
            != std::string::npos,
        "queued inbound-removal fixture should keep prior workbook replacement reason");
    const auto* queued_workbook_relationships_plan =
        editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels");
    check(queued_workbook_relationships_plan != nullptr
            && queued_workbook_relationships_plan->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "queued inbound-removal fixture should audit preserved workbook relationships");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "queued inbound-removal fixture should leave drawing copy-original before failure");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "queued inbound-removal fixture should update workbook manifest write mode");

    fastxlsx::detail::ReferencePolicy fail_policy;
    fail_policy.unsupported_linked_part_action =
        fastxlsx::detail::ReferencePolicyAction::Fail;

    bool failed = false;
    try {
        editor.remove_part(
            drawing_part, "strict drawing removal after queued workbook replacement", fail_policy);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "inbound relationships blocked by reference policy",
            "queued inbound removal policy failure should report inbound relationship policy");
    }
    check(failed,
        "PackageEditor should fail inbound drawing removal after a queued workbook replacement");

    check(editor.edit_plan().size() == queued_plan_size,
        "queued inbound removal failure should preserve prior edit-plan size");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "queued inbound removal failure should not append edit-plan notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == queued_relationship_target_audit_count,
        "queued inbound removal failure should not append relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == queued_worksheet_relationship_reference_audit_count,
        "queued inbound removal failure should not append worksheet reference audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == queued_worksheet_payload_dependency_audit_count,
        "queued inbound removal failure should not append worksheet payload audits");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == queued_workbook_payload_dependency_audit_count,
        "queued inbound removal failure should not append workbook payload audits");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "queued inbound removal failure should preserve package-entry audits");
    check(editor.edit_plan().removed_parts().size() == queued_removed_part_count,
        "queued inbound removal failure should not record removed parts");
    check(editor.edit_plan().removed_package_entries().size()
            == queued_removed_package_entry_count,
        "queued inbound removal failure should not record removed package entries");
    check(editor.edit_plan().full_calculation_on_load()
            == queued_full_calculation_on_load,
        "queued inbound removal failure should not change fullCalcOnLoad intent");
    check(editor.edit_plan().calc_chain_action() == queued_calc_chain_action,
        "queued inbound removal failure should preserve calcChain policy");
    const auto* final_workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(final_workbook_plan != nullptr
            && final_workbook_plan->write_mode
                == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "queued inbound removal failure should keep prior workbook replacement active");
    check(final_workbook_plan->reason.find("queued workbook replacement")
            != std::string::npos,
        "queued inbound removal failure should keep prior workbook replacement reason");
    check(editor.edit_plan().find_removed_part(drawing_part) == nullptr,
        "queued inbound removal failure should not leave stale removed drawing audit");
    check(editor.edit_plan().find_removed_package_entry("xl/drawings/_rels/drawing1.xml.rels")
            == nullptr,
        "queued inbound removal failure should not omit drawing owner relationships");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "queued inbound removal failure should leave drawing copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "queued inbound removal failure should leave worksheet copy-original");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "queued inbound removal failure should keep prior workbook manifest write mode");
    check_manifest_write_mode(editor, drawing_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "queued inbound removal failure should leave drawing manifest copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(editor.planned_output_entries().size() == output_plan.entries.size(),
        "queued inbound removal failure should keep planned output snapshot consistent");
    check(output_plan.full_calculation_on_load == queued_full_calculation_on_load,
        "queued inbound removal failure output plan should preserve fullCalcOnLoad intent");
    check(output_plan.calc_chain_action == queued_calc_chain_action,
        "queued inbound removal failure output plan should preserve calcChain policy");
    check(output_plan.notes.size() == queued_note_count,
        "queued inbound removal failure output plan should not append notes");
    check(output_plan.relationship_target_audits.size()
            == queued_relationship_target_audit_count,
        "queued inbound removal failure output plan should not append relationship target audits");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == queued_worksheet_relationship_reference_audit_count,
        "queued inbound removal failure output plan should not append worksheet reference audits");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == queued_worksheet_payload_dependency_audit_count,
        "queued inbound removal failure output plan should not append worksheet payload audits");
    check(output_plan.workbook_payload_dependency_audits.size()
            == queued_workbook_payload_dependency_audit_count,
        "queued inbound removal failure output plan should not append workbook payload audits");
    check(output_plan.removed_parts.size() == queued_removed_part_count,
        "queued inbound removal failure output plan should not record removed parts");
    check(output_plan.removed_package_entries.size()
            == queued_removed_package_entry_count,
        "queued inbound removal failure output plan should not record omitted metadata entries");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "queued inbound removal failure output plan should keep workbook rewrite");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "queued inbound removal failure output plan should preserve drawing copy-original");
    check_output_entry_part_context(output_plan.entries, "xl/drawings/drawing1.xml",
        true, drawing_part.value(),
        "queued inbound removal failure output plan should classify drawing as a package part");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "queued inbound removal failure output plan should preserve workbook relationships");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(source_reader, output_reader, workbook_part.zip_path());
    check(output_reader.read_entry("xl/workbook.xml") == replacement_workbook,
        "queued inbound removal failure output should keep prior workbook replacement bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "queued inbound removal failure output should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "queued inbound removal failure output should preserve worksheet bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "queued inbound removal failure output should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "queued inbound removal failure output should preserve drawing relationships bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "queued inbound removal failure output should preserve unknown extension bytes");
}

void test_package_editor_removes_table_and_preserves_worksheet_links()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-table-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-table-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    editor.remove_part(table_part, "explicit table part removal");

    check(editor.edit_plan().find_part(table_part) == nullptr,
        "explicit table removal should clear the active edit-plan part");
    const auto* removed_table = editor.edit_plan().find_removed_part(table_part);
    check(removed_table != nullptr,
        "explicit table removal should record removed-part audit");
    check(removed_table->reason.find("table part") != std::string::npos,
        "explicit table removal should retain the removal reason");
    check(removed_table->reason.find("inbound relationship preserved")
            != std::string::npos,
        "explicit table removal should audit preserved inbound relationships");
    check(removed_table->reason.find("/xl/worksheets/sheet1.xml")
            != std::string::npos,
        "explicit table removal inbound audit should include owner part");
    check(removed_table->reason.find("rId3") != std::string::npos,
        "explicit table removal inbound audit should include relationship id");
    check(removed_table->reason.find("../tables/table1.xml") != std::string::npos,
        "explicit table removal inbound audit should include original target");
    check(removed_table->inbound_relationships.size() == 1,
        "explicit table removal should keep structured inbound audit");
    const auto& table_inbound = removed_table->inbound_relationships.front();
    check(table_inbound.owner_part == worksheet_part.value(),
        "explicit table removal should keep inbound owner part");
    check(table_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "explicit table removal should keep inbound owner relationship entry");
    check(table_inbound.relationship_id == "rId3",
        "explicit table removal should keep inbound relationship id");
    check(table_inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/table",
        "explicit table removal should keep inbound relationship type");
    check(table_inbound.relationship_target == "../tables/table1.xml",
        "explicit table removal should keep inbound raw target");
    check(table_inbound.target_part == table_part,
        "explicit table removal should keep normalized target part");
    check(editor.manifest().find_part(table_part) == nullptr,
        "explicit table removal should remove the part from the manifest");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "explicit table removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "explicit table removal content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "explicit table removal content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry("xl/tables/_rels/table1.xml.rels")
            == nullptr,
        "explicit table removal should not invent missing table owner relationships omission");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/tables/table1.xml") == entries.end(),
        "explicit table removal output should omit table part");
    check(entries.find("xl/tables/_rels/table1.xml.rels") == entries.end(),
        "explicit table removal output should not invent table owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(table_part) == nullptr,
        "explicit table removal output should remove table content type override");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "explicit table removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "explicit table removal should not prune inbound worksheet relationships");
    check(output_reader.relationships_for(table_part) == nullptr,
        "explicit table removal should not keep owner relationships for absent table");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "explicit table removal should keep worksheet relationships readable");
    const auto* table_link = worksheet_relationships->find_by_id("rId3");
    check(table_link != nullptr,
        "explicit table removal should keep inbound table relationship id");
    check(table_link->target == "../tables/table1.xml",
        "explicit table removal should not rewrite inbound table relationship target");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "explicit table removal should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "explicit table removal should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "explicit table removal should preserve media bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "explicit table removal should preserve chart bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "explicit table removal should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "explicit table removal should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "explicit table removal should preserve VBA bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "explicit table removal should preserve unknown extension bytes");
}

void test_package_editor_removes_shared_strings_and_preserves_workbook_links()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-sharedstrings-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-sharedstrings-output.xlsx");

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

    editor.remove_part(shared_strings_part, "explicit sharedStrings part removal");

    check(editor.edit_plan().find_part(shared_strings_part) == nullptr,
        "explicit sharedStrings removal should clear the active edit-plan part");
    const auto* removed_shared_strings =
        editor.edit_plan().find_removed_part(shared_strings_part);
    check(removed_shared_strings != nullptr,
        "explicit sharedStrings removal should record removed-part audit");
    check(removed_shared_strings->reason.find("sharedStrings") != std::string::npos,
        "explicit sharedStrings removal should retain the removal reason");
    check(removed_shared_strings->reason.find("inbound relationship preserved")
            != std::string::npos,
        "explicit sharedStrings removal should audit preserved inbound relationships");
    check(removed_shared_strings->reason.find("/xl/workbook.xml") != std::string::npos,
        "explicit sharedStrings removal inbound audit should include owner part");
    check(removed_shared_strings->reason.find("rId3") != std::string::npos,
        "explicit sharedStrings removal inbound audit should include relationship id");
    check(removed_shared_strings->reason.find("sharedStrings.xml") != std::string::npos,
        "explicit sharedStrings removal inbound audit should include original target");
    check(removed_shared_strings->inbound_relationships.size() == 1,
        "explicit sharedStrings removal should keep structured inbound audit");
    const auto& shared_strings_inbound =
        removed_shared_strings->inbound_relationships.front();
    check(shared_strings_inbound.owner_part == workbook_part.value(),
        "explicit sharedStrings removal should keep inbound owner part");
    check(shared_strings_inbound.owner_entry == "xl/_rels/workbook.xml.rels",
        "explicit sharedStrings removal should keep inbound owner relationship entry");
    check(shared_strings_inbound.relationship_id == "rId3",
        "explicit sharedStrings removal should keep inbound relationship id");
    check(shared_strings_inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings",
        "explicit sharedStrings removal should keep inbound relationship type");
    check(shared_strings_inbound.relationship_target == "sharedStrings.xml",
        "explicit sharedStrings removal should keep inbound raw target");
    check(shared_strings_inbound.target_part == shared_strings_part,
        "explicit sharedStrings removal should keep normalized target part");
    check(editor.manifest().find_part(shared_strings_part) == nullptr,
        "explicit sharedStrings removal should remove the part from the manifest");
    check(editor.manifest().content_types().override_for(shared_strings_part) == nullptr,
        "explicit sharedStrings removal should remove the manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "explicit sharedStrings removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "explicit sharedStrings removal content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "explicit sharedStrings removal content types audit should keep structured role");
    const auto* removed_shared_strings_relationships =
        editor.edit_plan().find_removed_package_entry("xl/_rels/sharedStrings.xml.rels");
    check(removed_shared_strings_relationships != nullptr,
        "explicit sharedStrings removal should audit omitted owner relationships");
    check(removed_shared_strings_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "explicit sharedStrings owner relationships omission should keep source relationship role");
    check(removed_shared_strings_relationships->owner_part == shared_strings_part.value(),
        "explicit sharedStrings owner relationships omission should keep owner part");
    check(editor.edit_plan().find_package_entry("xl/_rels/sharedStrings.xml.rels")
            == nullptr,
        "explicit sharedStrings removal should not keep active owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit sharedStrings removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit sharedStrings removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit sharedStrings removal should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit sharedStrings removal should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit sharedStrings removal should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit sharedStrings removal should keep table copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit sharedStrings removal should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit sharedStrings removal should keep VBA copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit sharedStrings removal should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit sharedStrings removal should keep unknown extension copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/sharedStrings.xml") == entries.end(),
        "explicit sharedStrings removal output should omit sharedStrings part");
    check(entries.find("xl/_rels/sharedStrings.xml.rels") == entries.end(),
        "explicit sharedStrings removal output should omit owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(shared_strings_part) == nullptr,
        "explicit sharedStrings removal output should remove content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/sharedStrings.xml",
        "explicit sharedStrings removal content types XML should omit sharedStrings override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "explicit sharedStrings removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "explicit sharedStrings removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "explicit sharedStrings removal should not prune inbound workbook relationships");
    check(output_reader.relationships_for(shared_strings_part) == nullptr,
        "explicit sharedStrings removal should not keep owner relationships for absent sharedStrings");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "explicit sharedStrings removal should keep workbook relationships readable");
    const auto* shared_strings_link = workbook_relationships->find_by_id("rId3");
    check(shared_strings_link != nullptr,
        "explicit sharedStrings removal should keep inbound sharedStrings relationship id");
    check(shared_strings_link->target == "sharedStrings.xml",
        "explicit sharedStrings removal should not rewrite inbound sharedStrings target");
    check(shared_strings_link->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "explicit sharedStrings removal should keep inbound sharedStrings target mode");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "explicit sharedStrings removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "explicit sharedStrings removal should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "explicit sharedStrings removal should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "explicit sharedStrings removal should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "explicit sharedStrings removal should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "explicit sharedStrings removal should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "explicit sharedStrings removal should preserve table bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "explicit sharedStrings removal should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "explicit sharedStrings removal should preserve VBA bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "explicit sharedStrings removal should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "explicit sharedStrings removal should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "explicit sharedStrings removal should preserve unknown extension relationships bytes");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "explicit sharedStrings removal should keep styles content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "explicit sharedStrings removal should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "explicit sharedStrings removal should not promote PNG media to an override");
}

void test_package_editor_removes_styles_and_preserves_workbook_links()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-styles-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-styles-output.xlsx");

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

    editor.remove_part(styles_part, "explicit styles part removal");

    check(editor.edit_plan().find_part(styles_part) == nullptr,
        "explicit styles removal should clear the active edit-plan part");
    const auto* removed_styles = editor.edit_plan().find_removed_part(styles_part);
    check(removed_styles != nullptr,
        "explicit styles removal should record removed-part audit");
    check(removed_styles->reason.find("styles part") != std::string::npos,
        "explicit styles removal should retain the removal reason");
    check(removed_styles->reason.find("inbound relationship preserved")
            != std::string::npos,
        "explicit styles removal should audit preserved inbound relationships");
    check(removed_styles->reason.find("/xl/workbook.xml") != std::string::npos,
        "explicit styles removal inbound audit should include owner part");
    check(removed_styles->reason.find("rId4") != std::string::npos,
        "explicit styles removal inbound audit should include relationship id");
    check(removed_styles->reason.find("styles.xml") != std::string::npos,
        "explicit styles removal inbound audit should include original target");
    check(removed_styles->inbound_relationships.size() == 1,
        "explicit styles removal should keep structured inbound audit");
    const auto& styles_inbound = removed_styles->inbound_relationships.front();
    check(styles_inbound.owner_part == workbook_part.value(),
        "explicit styles removal should keep inbound owner part");
    check(styles_inbound.owner_entry == "xl/_rels/workbook.xml.rels",
        "explicit styles removal should keep inbound owner relationship entry");
    check(styles_inbound.relationship_id == "rId4",
        "explicit styles removal should keep inbound relationship id");
    check(styles_inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles",
        "explicit styles removal should keep inbound relationship type");
    check(styles_inbound.relationship_target == "styles.xml",
        "explicit styles removal should keep inbound raw target");
    check(styles_inbound.target_part == styles_part,
        "explicit styles removal should keep normalized target part");
    check(editor.manifest().find_part(styles_part) == nullptr,
        "explicit styles removal should remove the part from the manifest");
    check(editor.manifest().content_types().override_for(styles_part) == nullptr,
        "explicit styles removal should remove the manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "explicit styles removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "explicit styles removal content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "explicit styles removal content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/styles.xml.rels")
            == nullptr,
        "explicit styles removal should not invent missing owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/_rels/styles.xml.rels") == nullptr,
        "explicit styles removal should not keep active owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit styles removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit styles removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit styles removal should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit styles removal should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit styles removal should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit styles removal should keep table copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit styles removal should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit styles removal should keep VBA copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit styles removal should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit styles removal should keep unknown extension copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/styles.xml") == entries.end(),
        "explicit styles removal output should omit styles part");
    check(entries.find("xl/_rels/styles.xml.rels") == entries.end(),
        "explicit styles removal output should not invent styles owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(styles_part) == nullptr,
        "explicit styles removal output should remove styles content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/styles.xml",
        "explicit styles removal content types XML should omit styles override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "explicit styles removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "explicit styles removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "explicit styles removal should not prune inbound workbook relationships");
    check(output_reader.relationships_for(styles_part) == nullptr,
        "explicit styles removal should not create owner relationships for absent styles");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "explicit styles removal should keep workbook relationships readable");
    const auto* styles_link = workbook_relationships->find_by_id("rId4");
    check(styles_link != nullptr,
        "explicit styles removal should keep inbound styles relationship id");
    check(styles_link->target == "styles.xml",
        "explicit styles removal should not rewrite inbound styles target");
    check(styles_link->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "explicit styles removal should keep inbound styles target mode");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "explicit styles removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "explicit styles removal should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "explicit styles removal should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "explicit styles removal should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "explicit styles removal should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "explicit styles removal should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "explicit styles removal should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "explicit styles removal should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "explicit styles removal should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "explicit styles removal should preserve VBA bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "explicit styles removal should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "explicit styles removal should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "explicit styles removal should preserve unknown extension relationships bytes");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "explicit styles removal should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "explicit styles removal should keep table content type override");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "explicit styles removal should keep chart content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "explicit styles removal should keep VBA content type override");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "explicit styles removal should keep calcChain content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "explicit styles removal should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "explicit styles removal should not promote PNG media to an override");
}

void test_package_editor_removes_vba_project_and_preserves_workbook_links()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-vba-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-vba-output.xlsx");

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

    editor.remove_part(vba_part, "explicit VBA project part removal");

    check(editor.edit_plan().find_part(vba_part) == nullptr,
        "explicit VBA removal should clear the active edit-plan part");
    const auto* removed_vba = editor.edit_plan().find_removed_part(vba_part);
    check(removed_vba != nullptr,
        "explicit VBA removal should record removed-part audit");
    check(removed_vba->reason.find("VBA project") != std::string::npos,
        "explicit VBA removal should retain the removal reason");
    check(removed_vba->reason.find("inbound relationship preserved")
            != std::string::npos,
        "explicit VBA removal should audit preserved inbound relationships");
    check(removed_vba->reason.find("/xl/workbook.xml") != std::string::npos,
        "explicit VBA removal inbound audit should include owner part");
    check(removed_vba->reason.find("rId2") != std::string::npos,
        "explicit VBA removal inbound audit should include relationship id");
    check(removed_vba->reason.find("vbaProject.bin") != std::string::npos,
        "explicit VBA removal inbound audit should include original target");
    check(removed_vba->inbound_relationships.size() == 1,
        "explicit VBA removal should keep structured inbound audit");
    const auto& vba_inbound = removed_vba->inbound_relationships.front();
    check(vba_inbound.owner_part == workbook_part.value(),
        "explicit VBA removal should keep inbound owner part");
    check(vba_inbound.owner_entry == "xl/_rels/workbook.xml.rels",
        "explicit VBA removal should keep inbound owner relationship entry");
    check(vba_inbound.relationship_id == "rId2",
        "explicit VBA removal should keep inbound relationship id");
    check(vba_inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vbaProject",
        "explicit VBA removal should keep inbound relationship type");
    check(vba_inbound.relationship_target == "vbaProject.bin",
        "explicit VBA removal should keep inbound raw target");
    check(vba_inbound.target_part == vba_part,
        "explicit VBA removal should keep normalized target part");
    check(editor.manifest().find_part(vba_part) == nullptr,
        "explicit VBA removal should remove the part from the manifest");
    check(editor.manifest().content_types().override_for(vba_part) == nullptr,
        "explicit VBA removal should remove the manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "explicit VBA removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "explicit VBA removal content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "explicit VBA removal content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/vbaProject.bin.rels")
            == nullptr,
        "explicit VBA removal should not invent missing owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/_rels/vbaProject.bin.rels") == nullptr,
        "explicit VBA removal should not keep active owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VBA removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VBA removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VBA removal should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VBA removal should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VBA removal should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VBA removal should keep table copy-original");
    check(editor.edit_plan().find_part(vml_drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VBA removal should keep VML drawing copy-original");
    check(editor.edit_plan().find_part(percent_encoded_drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VBA removal should keep percent-decoded drawing copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VBA removal should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VBA removal should keep styles copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VBA removal should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VBA removal should keep unknown extension copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/vbaProject.bin") == entries.end(),
        "explicit VBA removal output should omit VBA project part");
    check(entries.find("xl/_rels/vbaProject.bin.rels") == entries.end(),
        "explicit VBA removal output should not invent VBA owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(vba_part) == nullptr,
        "explicit VBA removal output should remove VBA content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/vbaProject.bin",
        "explicit VBA removal content types XML should omit VBA override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "explicit VBA removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "explicit VBA removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "explicit VBA removal should not prune inbound workbook relationships");
    check(output_reader.relationships_for(vba_part) == nullptr,
        "explicit VBA removal should not create owner relationships for absent VBA project");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "explicit VBA removal should keep workbook relationships readable");
    const auto* vba_link = workbook_relationships->find_by_id("rId2");
    check(vba_link != nullptr,
        "explicit VBA removal should keep inbound VBA relationship id");
    check(vba_link->target == "vbaProject.bin",
        "explicit VBA removal should not rewrite inbound VBA target");
    check(vba_link->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "explicit VBA removal should keep inbound VBA target mode");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "explicit VBA removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "explicit VBA removal should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "explicit VBA removal should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "explicit VBA removal should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "explicit VBA removal should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "explicit VBA removal should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "explicit VBA removal should preserve table bytes");
    check(output_reader.read_entry("xl/drawings/vmlDrawing1.vml") == source.vml_drawing,
        "explicit VBA removal should preserve VML drawing bytes");
    check(output_reader.read_entry("xl/drawings/drawing space.xml")
            == source.percent_encoded_drawing,
        "explicit VBA removal should preserve percent-decoded drawing bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "explicit VBA removal should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "explicit VBA removal should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "explicit VBA removal should preserve styles bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "explicit VBA removal should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "explicit VBA removal should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "explicit VBA removal should preserve unknown extension relationships bytes");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "explicit VBA removal should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "explicit VBA removal should keep styles content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "explicit VBA removal should keep table content type override");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "explicit VBA removal should keep chart content type override");
    check(output_reader.content_types().override_for(vml_drawing_part) != nullptr,
        "explicit VBA removal should keep VML content type override");
    check(output_reader.content_types().override_for(percent_encoded_drawing_part) != nullptr,
        "explicit VBA removal should keep percent-decoded drawing content type override");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "explicit VBA removal should keep calcChain content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "explicit VBA removal should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "explicit VBA removal should not promote PNG media to an override");
}

void test_package_editor_removes_vml_drawing_and_preserves_worksheet_links()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-vml-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-vml-output.xlsx");

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

    editor.remove_part(vml_drawing_part, "explicit VML drawing part removal");

    check(editor.edit_plan().find_part(vml_drawing_part) == nullptr,
        "explicit VML removal should clear the active edit-plan part");
    const auto* removed_vml = editor.edit_plan().find_removed_part(vml_drawing_part);
    check(removed_vml != nullptr,
        "explicit VML removal should record removed-part audit");
    check(removed_vml->reason.find("VML drawing") != std::string::npos,
        "explicit VML removal should retain the removal reason");
    check(removed_vml->reason.find("inbound relationship preserved")
            != std::string::npos,
        "explicit VML removal should audit preserved inbound relationships");
    check(removed_vml->reason.find("/xl/worksheets/sheet1.xml")
            != std::string::npos,
        "explicit VML removal inbound audit should include owner part");
    check(removed_vml->reason.find("rId7") != std::string::npos,
        "explicit VML removal inbound audit should include relationship id");
    check(removed_vml->reason.find("../drawings/vmlDrawing1.vml#shape1")
            != std::string::npos,
        "explicit VML removal inbound audit should include original target");
    check(removed_vml->inbound_relationships.size() == 1,
        "explicit VML removal should keep structured inbound audit");
    const auto& vml_inbound = removed_vml->inbound_relationships.front();
    check(vml_inbound.owner_part == worksheet_part.value(),
        "explicit VML removal should keep inbound owner part");
    check(vml_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "explicit VML removal should keep inbound owner relationship entry");
    check(vml_inbound.relationship_id == "rId7",
        "explicit VML removal should keep inbound relationship id");
    check(vml_inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing",
        "explicit VML removal should keep inbound relationship type");
    check(vml_inbound.relationship_target == "../drawings/vmlDrawing1.vml#shape1",
        "explicit VML removal should keep inbound raw target");
    check(vml_inbound.target_part == vml_drawing_part,
        "explicit VML removal should keep normalized target part");
    check(editor.manifest().find_part(vml_drawing_part) == nullptr,
        "explicit VML removal should remove the part from the manifest");
    check(editor.manifest().content_types().override_for(vml_drawing_part) == nullptr,
        "explicit VML removal should remove the manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "explicit VML removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "explicit VML removal content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "explicit VML removal content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry("xl/drawings/_rels/vmlDrawing1.vml.rels")
            == nullptr,
        "explicit VML removal should not invent missing owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/drawings/_rels/vmlDrawing1.vml.rels")
            == nullptr,
        "explicit VML removal should not keep active owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VML removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VML removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VML removal should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VML removal should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VML removal should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VML removal should keep table copy-original");
    check(editor.edit_plan().find_part(percent_encoded_drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VML removal should keep percent-decoded drawing copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VML removal should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VML removal should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VML removal should keep VBA copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VML removal should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VML removal should keep unknown extension copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/drawings/vmlDrawing1.vml") == entries.end(),
        "explicit VML removal output should omit VML drawing part");
    check(entries.find("xl/drawings/_rels/vmlDrawing1.vml.rels") == entries.end(),
        "explicit VML removal output should not invent VML owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(vml_drawing_part) == nullptr,
        "explicit VML removal output should remove VML content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/drawings/vmlDrawing1.vml",
        "explicit VML removal content types XML should omit VML override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "explicit VML removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "explicit VML removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "explicit VML removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "explicit VML removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "explicit VML removal should not prune inbound worksheet relationships");
    check(output_reader.relationships_for(vml_drawing_part) == nullptr,
        "explicit VML removal should not create owner relationships for absent VML drawing");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "explicit VML removal should keep worksheet relationships readable");
    const auto* vml_link = worksheet_relationships->find_by_id("rId7");
    check(vml_link != nullptr,
        "explicit VML removal should keep inbound VML relationship id");
    check(vml_link->target == "../drawings/vmlDrawing1.vml#shape1",
        "explicit VML removal should not rewrite inbound VML relationship target");
    check(vml_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "explicit VML removal should keep inbound VML relationship target mode");
    check(worksheet_relationships->find_by_id("rId1") != nullptr,
        "explicit VML removal should keep worksheet drawing relationship");
    check(worksheet_relationships->find_by_id("rId3") != nullptr,
        "explicit VML removal should keep worksheet table relationship");
    check(worksheet_relationships->find_by_id("rId8") != nullptr,
        "explicit VML removal should keep percent-encoded drawing relationship");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "explicit VML removal should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "explicit VML removal should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "explicit VML removal should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "explicit VML removal should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "explicit VML removal should preserve table bytes");
    check(output_reader.read_entry("xl/drawings/drawing space.xml")
            == source.percent_encoded_drawing,
        "explicit VML removal should preserve percent-decoded drawing bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "explicit VML removal should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "explicit VML removal should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "explicit VML removal should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "explicit VML removal should preserve VBA bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "explicit VML removal should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "explicit VML removal should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "explicit VML removal should preserve unknown extension relationships bytes");
    check(output_reader.content_types().override_for(percent_encoded_drawing_part) != nullptr,
        "explicit VML removal should keep percent-decoded drawing content type override");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "explicit VML removal should keep chart content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "explicit VML removal should keep table content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "explicit VML removal should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "explicit VML removal should keep styles content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "explicit VML removal should keep VBA content type override");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "explicit VML removal should keep calcChain content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "explicit VML removal should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "explicit VML removal should not promote PNG media to an override");
}

void test_package_editor_removes_percent_decoded_drawing_and_preserves_encoded_link()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package(
            "fastxlsx-package-editor-remove-percent-drawing-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-percent-drawing-output.xlsx");

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
        "explicit percent-decoded drawing part removal");

    check(editor.edit_plan().find_part(percent_encoded_drawing_part) == nullptr,
        "explicit percent-decoded drawing removal should clear the active edit-plan part");
    const auto* removed_drawing =
        editor.edit_plan().find_removed_part(percent_encoded_drawing_part);
    check(removed_drawing != nullptr,
        "explicit percent-decoded drawing removal should record removed-part audit");
    check(removed_drawing->reason.find("percent-decoded drawing") != std::string::npos,
        "explicit percent-decoded drawing removal should retain the removal reason");
    check(removed_drawing->reason.find("inbound relationship preserved")
            != std::string::npos,
        "explicit percent-decoded drawing removal should audit preserved inbound relationships");
    check(removed_drawing->reason.find("/xl/worksheets/sheet1.xml")
            != std::string::npos,
        "explicit percent-decoded drawing removal inbound audit should include owner part");
    check(removed_drawing->reason.find("rId8") != std::string::npos,
        "explicit percent-decoded drawing removal inbound audit should include relationship id");
    check(removed_drawing->reason.find("../drawings/drawing%20space.xml")
            != std::string::npos,
        "explicit percent-decoded drawing removal inbound audit should include raw target");
    check(removed_drawing->inbound_relationships.size() == 1,
        "explicit percent-decoded drawing removal should keep structured inbound audit");
    const auto& drawing_inbound = removed_drawing->inbound_relationships.front();
    check(drawing_inbound.owner_part == worksheet_part.value(),
        "explicit percent-decoded drawing removal should keep inbound owner part");
    check(drawing_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "explicit percent-decoded drawing removal should keep inbound owner relationship entry");
    check(drawing_inbound.relationship_id == "rId8",
        "explicit percent-decoded drawing removal should keep inbound relationship id");
    check(drawing_inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
        "explicit percent-decoded drawing removal should keep inbound relationship type");
    check(drawing_inbound.relationship_target == "../drawings/drawing%20space.xml",
        "explicit percent-decoded drawing removal should keep inbound raw target");
    check(drawing_inbound.target_part == percent_encoded_drawing_part,
        "explicit percent-decoded drawing removal should keep normalized target part");
    check(editor.manifest().find_part(percent_encoded_drawing_part) == nullptr,
        "explicit percent-decoded drawing removal should remove the part from the manifest");
    check(editor.manifest().content_types().override_for(percent_encoded_drawing_part)
            == nullptr,
        "explicit percent-decoded drawing removal should remove the manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "explicit percent-decoded drawing removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "explicit percent-decoded drawing removal content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "explicit percent-decoded drawing removal content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry(
              "xl/drawings/_rels/drawing space.xml.rels") == nullptr,
        "explicit percent-decoded drawing removal should not invent missing owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/drawings/_rels/drawing space.xml.rels")
            == nullptr,
        "explicit percent-decoded drawing removal should not keep active owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit percent-decoded drawing removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit percent-decoded drawing removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit percent-decoded drawing removal should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit percent-decoded drawing removal should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit percent-decoded drawing removal should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit percent-decoded drawing removal should keep table copy-original");
    check(editor.edit_plan().find_part(vml_drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit percent-decoded drawing removal should keep VML drawing copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit percent-decoded drawing removal should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit percent-decoded drawing removal should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit percent-decoded drawing removal should keep VBA copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit percent-decoded drawing removal should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit percent-decoded drawing removal should keep unknown extension copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/drawings/drawing space.xml") == entries.end(),
        "explicit percent-decoded drawing removal output should omit drawing part");
    check(entries.find("xl/drawings/_rels/drawing space.xml.rels") == entries.end(),
        "explicit percent-decoded drawing removal output should not invent owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(percent_encoded_drawing_part)
            == nullptr,
        "explicit percent-decoded drawing removal output should remove content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/drawings/drawing space.xml",
        "explicit percent-decoded drawing removal content types XML should omit override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "explicit percent-decoded drawing removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "explicit percent-decoded drawing removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "explicit percent-decoded drawing removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "explicit percent-decoded drawing removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "explicit percent-decoded drawing removal should not prune inbound worksheet relationships");
    check(output_reader.relationships_for(percent_encoded_drawing_part) == nullptr,
        "explicit percent-decoded drawing removal should not create owner relationships");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "explicit percent-decoded drawing removal should keep worksheet relationships readable");
    const auto* drawing_link = worksheet_relationships->find_by_id("rId8");
    check(drawing_link != nullptr,
        "explicit percent-decoded drawing removal should keep inbound relationship id");
    check(drawing_link->target == "../drawings/drawing%20space.xml",
        "explicit percent-decoded drawing removal should not rewrite encoded target");
    check(drawing_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "explicit percent-decoded drawing removal should keep target mode internal");
    check(worksheet_relationships->find_by_id("rId1") != nullptr,
        "explicit percent-decoded drawing removal should keep worksheet drawing relationship");
    check(worksheet_relationships->find_by_id("rId3") != nullptr,
        "explicit percent-decoded drawing removal should keep worksheet table relationship");
    check(worksheet_relationships->find_by_id("rId7") != nullptr,
        "explicit percent-decoded drawing removal should keep worksheet VML relationship");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "explicit percent-decoded drawing removal should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "explicit percent-decoded drawing removal should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "explicit percent-decoded drawing removal should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "explicit percent-decoded drawing removal should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "explicit percent-decoded drawing removal should preserve table bytes");
    check(output_reader.read_entry("xl/drawings/vmlDrawing1.vml") == source.vml_drawing,
        "explicit percent-decoded drawing removal should preserve VML bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "explicit percent-decoded drawing removal should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "explicit percent-decoded drawing removal should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "explicit percent-decoded drawing removal should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "explicit percent-decoded drawing removal should preserve VBA bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "explicit percent-decoded drawing removal should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "explicit percent-decoded drawing removal should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "explicit percent-decoded drawing removal should preserve unknown extension relationships bytes");
    check(output_reader.content_types().override_for(vml_drawing_part) != nullptr,
        "explicit percent-decoded drawing removal should keep VML content type override");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "explicit percent-decoded drawing removal should keep chart content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "explicit percent-decoded drawing removal should keep table content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "explicit percent-decoded drawing removal should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "explicit percent-decoded drawing removal should keep styles content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "explicit percent-decoded drawing removal should keep VBA content type override");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "explicit percent-decoded drawing removal should keep calcChain content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "explicit percent-decoded drawing removal should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "explicit percent-decoded drawing removal should not promote PNG media to an override");
}

void test_package_editor_chart_removal_overrides_prior_replacement()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-after-replace-chart-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-chart-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
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

    const std::string replacement_chart =
        R"(<c:chartSpace xmlns:c="http://schemas.openxmlformats.org/drawingml/2006/chart">)"
        R"(<c:chart><c:title><c:tx><c:v>stale replacement</c:v></c:tx></c:title></c:chart>)"
        R"(</c:chartSpace>)";
    replace_part_with_memory_chunks(editor, chart_part, replacement_chart,
        "prior chart replacement before removal");

    const auto* prior_chart_plan = editor.edit_plan().find_part(chart_part);
    check(prior_chart_plan != nullptr,
        "setup should record active chart replacement before removal override");
    check(prior_chart_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "setup chart replacement should be local-DOM-rewrite before removal override");
    check_manifest_write_mode(editor, chart_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "setup chart replacement should mirror write mode into manifest");
    check(editor.manifest().content_types().override_for(chart_part) != nullptr,
        "setup chart replacement should keep chart content type override");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "setup chart replacement should not rewrite content types audit");
    check(editor.edit_plan().find_package_entry("xl/charts/_rels/chart1.xml.rels") == nullptr,
        "setup chart replacement should not invent chart owner relationships audit");

    editor.remove_part(chart_part, "explicit chart removal after replacement");

    check(editor.edit_plan().find_part(chart_part) == nullptr,
        "chart removal after replacement should clear active replacement entry");
    const auto* removed_chart = editor.edit_plan().find_removed_part(chart_part);
    check(removed_chart != nullptr,
        "chart removal after replacement should record removed-part audit");
    check(removed_chart->reason.find("after replacement") != std::string::npos,
        "chart removal after replacement should keep final removal reason");
    check(removed_chart->reason.find("inbound relationship preserved")
            != std::string::npos,
        "chart removal after replacement should keep inbound relationship audit");
    check(removed_chart->inbound_relationships.size() == 2,
        "chart removal after replacement should keep structured inbound audits");
    const fastxlsx::detail::RemovedPartInboundRelationshipAudit* chart_inbound = nullptr;
    const fastxlsx::detail::RemovedPartInboundRelationshipAudit* chart_fragment_inbound = nullptr;
    for (const auto& audit : removed_chart->inbound_relationships) {
        if (audit.relationship_id == "rId2") {
            chart_inbound = &audit;
        } else if (audit.relationship_id == "rId4") {
            chart_fragment_inbound = &audit;
        }
    }
    check(chart_inbound != nullptr,
        "chart removal after replacement should keep direct chart inbound audit");
    check(chart_inbound->owner_part == drawing_part.value(),
        "chart removal after replacement should keep direct inbound owner part");
    check(chart_inbound->owner_entry == "xl/drawings/_rels/drawing1.xml.rels",
        "chart removal after replacement should keep direct inbound owner entry");
    check(chart_inbound->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart",
        "chart removal after replacement should keep direct inbound relationship type");
    check(chart_inbound->relationship_target == "../charts/chart1.xml",
        "chart removal after replacement should keep direct inbound raw target");
    check(chart_inbound->target_part == chart_part,
        "chart removal after replacement should keep direct normalized target");
    check(chart_fragment_inbound != nullptr,
        "chart removal after replacement should keep URI-qualified chart inbound audit");
    check(chart_fragment_inbound->owner_part == drawing_part.value(),
        "chart removal after replacement should keep URI-qualified inbound owner part");
    check(chart_fragment_inbound->owner_entry == "xl/drawings/_rels/drawing1.xml.rels",
        "chart removal after replacement should keep URI-qualified inbound owner entry");
    check(chart_fragment_inbound->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart",
        "chart removal after replacement should keep URI-qualified inbound relationship type");
    check(chart_fragment_inbound->relationship_target == "../charts/chart1.xml#plotArea",
        "chart removal after replacement should keep URI-qualified raw target");
    check(chart_fragment_inbound->target_part == chart_part,
        "chart removal after replacement should keep URI-qualified normalized target");
    check(editor.manifest().find_part(chart_part) == nullptr,
        "chart removal after replacement should remove manifest part");
    check(editor.manifest().content_types().override_for(chart_part) == nullptr,
        "chart removal after replacement should remove manifest chart override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "chart removal after replacement should still rewrite content types");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "chart removal after replacement content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "chart removal after replacement content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry("xl/charts/_rels/chart1.xml.rels")
            == nullptr,
        "chart removal after replacement should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/charts/_rels/chart1.xml.rels") == nullptr,
        "chart removal after replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart removal after replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart removal after replacement should keep drawing copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart removal after replacement should keep image copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart removal after replacement should keep table copy-original");
    check(editor.edit_plan().find_part(vml_drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart removal after replacement should keep VML drawing copy-original");
    check(editor.edit_plan().find_part(percent_encoded_drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart removal after replacement should keep percent-decoded drawing copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart removal after replacement should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart removal after replacement should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart removal after replacement should keep VBA project copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart removal after replacement should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart removal after replacement should keep unknown extension copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "chart removal after replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "chart removal after replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "chart removal after replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.size() == 1,
        "chart removal after replacement output plan should expose one removed part");
    check(output_plan.removed_parts.front().part_name == chart_part,
        "chart removal after replacement output plan should expose removed chart");
    check(output_plan.removed_parts.front().reason.find("after replacement")
            != std::string::npos,
        "chart removal after replacement output plan should keep removed-part reason");
    check(output_plan.removed_parts.front().inbound_relationships.size() == 2,
        "chart removal after replacement output plan should keep removed-part inbound audits");
    check(output_plan.removed_package_entries.empty(),
        "chart removal after replacement output plan should not omit metadata entries");
    check_output_entry_plan(output_plan.entries, "xl/charts/chart1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "chart removal after replacement output plan should omit chart part");
    check_output_entry_part_context(output_plan.entries, "xl/charts/chart1.xml",
        true, chart_part.value(),
        "chart removal after replacement output plan should classify omitted chart");
    const auto* output_chart_plan =
        find_output_entry_plan(output_plan.entries, "xl/charts/chart1.xml");
    check(output_chart_plan->reason.find("after replacement") != std::string::npos,
        "chart removal after replacement output plan should keep final removal reason");
    check(output_chart_plan->inbound_relationships.size() == 2,
        "chart removal after replacement output plan should expose inbound audits");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "xl/charts/chart1.xml", drawing_part.value(),
        "xl/drawings/_rels/drawing1.xml.rels", "rId2",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart",
        "../charts/chart1.xml", chart_part,
        "chart removal after replacement output plan should keep direct inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "xl/charts/chart1.xml", drawing_part.value(),
        "xl/drawings/_rels/drawing1.xml.rels", "rId4",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart",
        "../charts/chart1.xml#plotArea", chart_part,
        "chart removal after replacement output plan should keep URI-qualified inbound audit");
    check(find_output_entry_plan(output_plan.entries, "xl/charts/_rels/chart1.xml.rels")
            == nullptr,
        "chart removal after replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "chart removal after replacement output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "chart removal after replacement output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "chart removal after replacement output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "xl/drawings/_rels/drawing1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "chart removal after replacement output plan should preserve inbound drawing relationships");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/charts/chart1.xml") == entries.end(),
        "chart removal after replacement output should omit target part");
    check(entries.find("xl/charts/_rels/chart1.xml.rels") == entries.end(),
        "chart removal after replacement output should not invent owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_not_contains(output_reader.read_entry("[Content_Types].xml"),
        "/xl/charts/chart1.xml",
        "chart removal after replacement output should omit chart content type override");
    check(output_reader.content_types().override_for(chart_part) == nullptr,
        "chart removal after replacement should remove chart content type override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "chart removal after replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "chart removal after replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "chart removal after replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "chart removal after replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "chart removal after replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "chart removal after replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "chart removal after replacement should not prune inbound drawing relationships");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "chart removal after replacement should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "chart removal after replacement should preserve table bytes");
    check(output_reader.read_entry("xl/drawings/vmlDrawing1.vml") == source.vml_drawing,
        "chart removal after replacement should preserve VML drawing bytes");
    check(output_reader.read_entry("xl/drawings/drawing space.xml")
            == source.percent_encoded_drawing,
        "chart removal after replacement should preserve percent-decoded drawing bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "chart removal after replacement should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "chart removal after replacement should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "chart removal after replacement should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "chart removal after replacement should preserve VBA project bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "chart removal after replacement should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "chart removal after replacement should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "chart removal after replacement should preserve unknown extension relationships bytes");
    const auto* drawing_relationships = output_reader.relationships_for(drawing_part);
    check(drawing_relationships != nullptr,
        "chart removal after replacement should keep drawing relationships readable");
    check(drawing_relationships->find_by_id("rId2") != nullptr,
        "chart removal after replacement should keep inbound chart relationship id");
    const auto* chart_link = drawing_relationships->find_by_id("rId2");
    check(chart_link->target == "../charts/chart1.xml",
        "chart removal after replacement should not rewrite direct chart target");
    check(chart_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "chart removal after replacement should keep direct chart target mode");
    const auto* chart_fragment_link = drawing_relationships->find_by_id("rId4");
    check(chart_fragment_link != nullptr,
        "chart removal after replacement should keep URI-qualified chart relationship id");
    check(chart_fragment_link->target == "../charts/chart1.xml#plotArea",
        "chart removal after replacement should not rewrite URI-qualified chart target");
    check(chart_fragment_link->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "chart removal after replacement should keep URI-qualified chart target mode");
    check(output_reader.relationships_for(chart_part) == nullptr,
        "chart removal after replacement should not keep owner relationships for absent chart");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "chart removal after replacement should keep table content type override");
    check(output_reader.content_types().override_for(vml_drawing_part) != nullptr,
        "chart removal after replacement should keep VML drawing content type override");
    check(output_reader.content_types().override_for(percent_encoded_drawing_part) != nullptr,
        "chart removal after replacement should keep percent-decoded drawing content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "chart removal after replacement should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "chart removal after replacement should keep styles content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "chart removal after replacement should keep VBA content type override");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "chart removal after replacement should keep calcChain content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "chart removal after replacement should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "chart removal after replacement should not promote PNG media to an override");
}


} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor preservation shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "preservation-removal")) {
            test_package_editor_removes_unknown_extension_and_omits_owner_relationships();
            test_package_editor_removes_workbook_and_preserves_package_links();
            test_package_editor_removes_worksheet_and_preserves_workbook_links();
            test_package_editor_removes_drawing_and_omits_owner_relationships();
            test_package_editor_removes_chart_and_rewrites_content_types_without_pruning_links();
            test_package_editor_removes_media_and_preserves_drawing_links();
            test_package_editor_removes_part_with_malformed_unrelated_relationship_target();
            test_package_editor_reference_policy_fail_blocks_inbound_part_removal_without_state_changes();
            test_package_editor_reference_policy_fail_preserves_prior_replacement_for_inbound_removal();
            test_package_editor_removes_table_and_preserves_worksheet_links();
            test_package_editor_removes_shared_strings_and_preserves_workbook_links();
            test_package_editor_removes_styles_and_preserves_workbook_links();
            test_package_editor_removes_vba_project_and_preserves_workbook_links();
            test_package_editor_removes_vml_drawing_and_preserves_worksheet_links();
            test_package_editor_removes_percent_decoded_drawing_and_preserves_encoded_link();
            test_package_editor_chart_removal_overrides_prior_replacement();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
