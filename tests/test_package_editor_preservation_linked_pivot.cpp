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
    return shard == "all" || shard == "preservation-linked-pivot";
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

void test_package_editor_worksheet_rewrite_preserves_pivot_table_cache_chain()
{
    const PivotSourcePackage source =
        write_pivot_source_package("fastxlsx-package-editor-pivot-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-pivot-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName pivot_table_part("/xl/pivotTables/pivotTable1.xml");
    const fastxlsx::detail::PartName pivot_cache_definition_part(
        "/xl/pivotCache/pivotCacheDefinition1.xml");
    const fastxlsx::detail::PartName pivot_cache_records_part(
        "/xl/pivotCache/pivotCacheRecords1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string replacement_sheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>128</v></c></row></sheetData>)"
        R"(</worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_sheet);

    const auto* pivot_table_plan = editor.edit_plan().find_part(pivot_table_part);
    check(pivot_table_plan != nullptr,
        "pivot table part should remain visible in worksheet rewrite edit plan");
    check(pivot_table_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table part should remain copy-original during worksheet rewrite");
    check(pivot_table_plan->reason.find("worksheet relationship rIdPivotTable")
            != std::string::npos,
        "pivot table copy reason should come from worksheet relationship traversal");
    check(pivot_table_plan->reason.find("relationships/pivotTable") != std::string::npos,
        "pivot table copy reason should include relationship type");
    check(pivot_table_plan->relationship_owner_part == worksheet_part.value(),
        "pivot table copy audit should keep structured relationship owner");
    check(pivot_table_plan->relationship_id == "rIdPivotTable",
        "pivot table copy audit should keep structured relationship id");
    check(pivot_table_plan->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotTable",
        "pivot table copy audit should keep structured relationship type");
    check(pivot_table_plan->relationship_target == "../pivotTables/pivotTable1.xml",
        "pivot table copy audit should keep structured relationship target");

    const auto* pivot_cache_definition_plan =
        editor.edit_plan().find_part(pivot_cache_definition_part);
    check(pivot_cache_definition_plan != nullptr,
        "pivot cache definition should remain visible in worksheet rewrite edit plan");
    check(pivot_cache_definition_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition should remain copy-original during worksheet rewrite");
    check(pivot_cache_definition_plan->reason.find("/xl/pivotTables/pivotTable1.xml")
            != std::string::npos
            && pivot_cache_definition_plan->reason.find("rIdPivotCacheDef")
                != std::string::npos,
        "pivot cache definition copy reason should come from pivot table relationship traversal");
    check(pivot_cache_definition_plan->relationship_owner_part == pivot_table_part.value(),
        "pivot cache definition copy audit should keep pivot-table-owned relationship owner");
    check(pivot_cache_definition_plan->relationship_id == "rIdPivotCacheDef",
        "pivot cache definition copy audit should keep relationship id");
    check(pivot_cache_definition_plan->relationship_target
            == "../pivotCache/pivotCacheDefinition1.xml",
        "pivot cache definition copy audit should keep relationship target");

    const auto* pivot_cache_records_plan =
        editor.edit_plan().find_part(pivot_cache_records_part);
    check(pivot_cache_records_plan != nullptr,
        "pivot cache records should remain visible in worksheet rewrite edit plan");
    check(pivot_cache_records_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records should remain copy-original during worksheet rewrite");
    check(pivot_cache_records_plan->reason.find("/xl/pivotCache/pivotCacheDefinition1.xml")
            != std::string::npos
            && pivot_cache_records_plan->reason.find("rIdPivotRecords")
                != std::string::npos,
        "pivot cache records copy reason should come from cache-definition relationship traversal");
    check(pivot_cache_records_plan->relationship_owner_part
            == pivot_cache_definition_part.value(),
        "pivot cache records copy audit should keep cache-definition-owned relationship owner");
    check(pivot_cache_records_plan->relationship_id == "rIdPivotRecords",
        "pivot cache records copy audit should keep relationship id");
    check(pivot_cache_records_plan->relationship_target == "pivotCacheRecords1.xml",
        "pivot cache records copy audit should keep relationship target");

    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot worksheet rewrite should keep unrelated unknown part copy-original");
    const auto* worksheet_relationships_plan =
        editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels");
    check(worksheet_relationships_plan != nullptr,
        "pivot worksheet rewrite should audit preserved worksheet relationships");
    check(worksheet_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot worksheet relationships should be copy-original in package-entry audit");
    check(worksheet_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "pivot worksheet relationships audit should keep structured role");
    check(worksheet_relationships_plan->owner_part == worksheet_part.value(),
        "pivot worksheet relationships audit should keep owner part");
    check(editor.edit_plan().find_package_entry("xl/pivotTables/_rels/pivotTable1.xml.rels")
            != nullptr,
        "pivot worksheet rewrite should audit preserved pivot table relationships");
    check(editor.edit_plan().find_package_entry(
              "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            != nullptr,
        "pivot worksheet rewrite should audit preserved pivot cache definition relationships");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "pivot worksheet rewrite should not rewrite content types without calcChain");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load,
        "pivot worksheet rewrite output plan should request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Remove,
        "pivot worksheet rewrite output plan should expose calcChain removal policy");
    check(output_plan.relationship_target_audits.empty(),
        "pivot worksheet rewrite output plan should not invent dependency audits");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "pivot worksheet rewrite output plan should stream-rewrite worksheet");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/sheet1.xml",
        true, worksheet_part.value(),
        "pivot worksheet rewrite output plan should classify worksheet as a part");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "pivot worksheet rewrite output plan should update workbook metadata");
    check_output_entry_part_context(output_plan.entries, "xl/workbook.xml", true,
        workbook_part.value(),
        "pivot worksheet rewrite output plan should classify workbook as a part");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot worksheet rewrite output plan should preserve package relationships");
    check_output_entry_part_context(output_plan.entries, "_rels/.rels", false, "",
        "pivot worksheet rewrite output plan should classify package relationships");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot worksheet rewrite output plan should preserve workbook relationships");
    check_output_entry_part_context(output_plan.entries, "xl/_rels/workbook.xml.rels",
        false, "",
        "pivot worksheet rewrite output plan should classify workbook relationships");
    const auto* output_workbook_relationships_plan =
        find_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels");
    check(output_workbook_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "pivot worksheet rewrite output plan should keep workbook relationship role");
    check(output_workbook_relationships_plan->owner_part == workbook_part.value(),
        "pivot worksheet rewrite output plan should keep workbook owner context");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot worksheet rewrite output plan should preserve worksheet relationships");
    check_output_entry_part_context(output_plan.entries,
        "xl/worksheets/_rels/sheet1.xml.rels", false, "",
        "pivot worksheet rewrite output plan should classify worksheet relationships");
    const auto* output_worksheet_relationships_plan =
        find_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels");
    check(output_worksheet_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "pivot worksheet rewrite output plan should keep worksheet relationship role");
    check(output_worksheet_relationships_plan->owner_part == worksheet_part.value(),
        "pivot worksheet rewrite output plan should keep worksheet owner context");
    check_output_entry_plan(output_plan.entries, "xl/pivotTables/pivotTable1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot worksheet rewrite output plan should preserve pivot table");
    check_output_entry_part_context(output_plan.entries, "xl/pivotTables/pivotTable1.xml",
        true, pivot_table_part.value(),
        "pivot worksheet rewrite output plan should classify pivot table as a part");
    check_output_entry_relationship_context(output_plan.entries,
        "xl/pivotTables/pivotTable1.xml", worksheet_part.value(), "rIdPivotTable",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotTable",
        "../pivotTables/pivotTable1.xml",
        "pivot worksheet rewrite output plan should keep pivot table relationship context");
    check_output_entry_plan(output_plan.entries,
        "xl/pivotTables/_rels/pivotTable1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot worksheet rewrite output plan should preserve pivot table relationships");
    check_output_entry_part_context(output_plan.entries,
        "xl/pivotTables/_rels/pivotTable1.xml.rels", false, "",
        "pivot worksheet rewrite output plan should classify pivot table relationships");
    const auto* output_pivot_table_relationships_plan =
        find_output_entry_plan(output_plan.entries,
            "xl/pivotTables/_rels/pivotTable1.xml.rels");
    check(output_pivot_table_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "pivot worksheet rewrite output plan should keep pivot table relationship role");
    check(output_pivot_table_relationships_plan->owner_part == pivot_table_part.value(),
        "pivot worksheet rewrite output plan should keep pivot table owner context");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheDefinition1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot worksheet rewrite output plan should preserve pivot cache definition");
    check_output_entry_part_context(output_plan.entries,
        "xl/pivotCache/pivotCacheDefinition1.xml", true,
        pivot_cache_definition_part.value(),
        "pivot worksheet rewrite output plan should classify pivot cache definition");
    check_output_entry_relationship_context(output_plan.entries,
        "xl/pivotCache/pivotCacheDefinition1.xml", pivot_table_part.value(),
        "rIdPivotCacheDef",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotCacheDefinition",
        "../pivotCache/pivotCacheDefinition1.xml",
        "pivot worksheet rewrite output plan should keep cache definition relationship context");
    check_output_entry_plan(output_plan.entries,
        "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot worksheet rewrite output plan should preserve cache definition relationships");
    check_output_entry_part_context(output_plan.entries,
        "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels", false, "",
        "pivot worksheet rewrite output plan should classify cache definition relationships");
    const auto* output_cache_definition_relationships_plan =
        find_output_entry_plan(output_plan.entries,
            "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels");
    check(output_cache_definition_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "pivot worksheet rewrite output plan should keep cache definition relationship role");
    check(output_cache_definition_relationships_plan->owner_part
            == pivot_cache_definition_part.value(),
        "pivot worksheet rewrite output plan should keep cache definition owner context");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheRecords1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot worksheet rewrite output plan should preserve cache records");
    check_output_entry_part_context(output_plan.entries,
        "xl/pivotCache/pivotCacheRecords1.xml", true, pivot_cache_records_part.value(),
        "pivot worksheet rewrite output plan should classify cache records");
    check_output_entry_relationship_context(output_plan.entries,
        "xl/pivotCache/pivotCacheRecords1.xml", pivot_cache_definition_part.value(),
        "rIdPivotRecords",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotCacheRecords",
        "pivotCacheRecords1.xml",
        "pivot worksheet rewrite output plan should keep cache records relationship context");
    check(find_output_entry_plan(output_plan.entries,
              "xl/pivotCache/_rels/pivotCacheRecords1.xml.rels")
            == nullptr,
        "pivot worksheet rewrite output plan should not invent cache records relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot worksheet rewrite output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "pivot worksheet rewrite output plan should classify content types");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot worksheet rewrite output plan should preserve unknown entry");
    check_output_entry_part_context(output_plan.entries, "custom/opaque.bin", true,
        unknown_part.value(),
        "pivot worksheet rewrite output plan should classify unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/pivotTables/pivotTable1.xml") != entries.end(),
        "pivot worksheet rewrite output should keep pivot table part");
    check(entries.find("xl/pivotTables/_rels/pivotTable1.xml.rels") != entries.end(),
        "pivot worksheet rewrite output should keep pivot table relationships");
    check(entries.find("xl/pivotCache/pivotCacheDefinition1.xml") != entries.end(),
        "pivot worksheet rewrite output should keep pivot cache definition");
    check(entries.find("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels") != entries.end(),
        "pivot worksheet rewrite output should keep pivot cache definition relationships");
    check(entries.find("xl/pivotCache/pivotCacheRecords1.xml") != entries.end(),
        "pivot worksheet rewrite output should keep pivot cache records");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_sheet,
        "pivot worksheet rewrite should write replacement worksheet XML");
    const std::string output_workbook = output_reader.read_entry("xl/workbook.xml");
    check_contains(output_workbook, "<pivotCaches>",
        "pivot worksheet rewrite should preserve workbook pivot cache metadata");
    check_contains(output_workbook, "r:id=\"rIdPivotCache\"",
        "pivot worksheet rewrite should preserve workbook pivot cache relationship id");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "pivot worksheet rewrite should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "pivot worksheet rewrite should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/pivotTables/pivotTable1.xml") == source.pivot_table,
        "pivot worksheet rewrite should preserve pivot table bytes");
    check(output_reader.read_entry("xl/pivotTables/_rels/pivotTable1.xml.rels")
            == source.pivot_table_relationships,
        "pivot worksheet rewrite should preserve pivot table relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheDefinition1.xml")
            == source.pivot_cache_definition,
        "pivot worksheet rewrite should preserve pivot cache definition bytes");
    check(output_reader.read_entry("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            == source.pivot_cache_definition_relationships,
        "pivot worksheet rewrite should preserve pivot cache definition relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheRecords1.xml")
            == source.pivot_cache_records,
        "pivot worksheet rewrite should preserve pivot cache records bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "pivot worksheet rewrite should preserve content types bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "pivot worksheet rewrite should preserve unknown bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "pivot worksheet rewrite should keep worksheet relationships readable");
    const auto* pivot_table_relationship = worksheet_relationships->find_by_id("rIdPivotTable");
    check(pivot_table_relationship != nullptr,
        "pivot worksheet rewrite should keep pivot table worksheet relationship id");
    check(pivot_table_relationship->target == "../pivotTables/pivotTable1.xml",
        "pivot worksheet rewrite should not rewrite pivot table target");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "pivot worksheet rewrite should keep workbook relationships readable");
    check(workbook_relationships->find_by_id("rIdPivotCache") != nullptr,
        "pivot worksheet rewrite should keep workbook pivot cache relationship id");
    const auto* pivot_table_relationships =
        output_reader.relationships_for(pivot_table_part);
    check(pivot_table_relationships != nullptr,
        "pivot worksheet rewrite should keep pivot table owner relationships readable");
    check(pivot_table_relationships->find_by_id("rIdPivotCacheDef") != nullptr,
        "pivot worksheet rewrite should keep pivot table to cache definition relationship");
    const auto* pivot_cache_relationships =
        output_reader.relationships_for(pivot_cache_definition_part);
    check(pivot_cache_relationships != nullptr,
        "pivot worksheet rewrite should keep pivot cache definition relationships readable");
    check(pivot_cache_relationships->find_by_id("rIdPivotRecords") != nullptr,
        "pivot worksheet rewrite should keep pivot cache records relationship");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(worksheet_part)->find_by_id("rIdPivotTable")
            != nullptr,
        "relationship graph should keep worksheet pivot table relationship");
    check(output_graph.relationships_for(pivot_table_part)->find_by_id("rIdPivotCacheDef")
            != nullptr,
        "relationship graph should keep pivot table cache definition relationship");
    check(output_graph.relationships_for(pivot_cache_definition_part)
                ->find_by_id("rIdPivotRecords")
            != nullptr,
        "relationship graph should keep pivot cache records relationship");
    check(output_reader.relationships_for(pivot_cache_records_part) == nullptr,
        "pivot worksheet rewrite should not invent pivot cache records owner relationships");
    check(output_reader.content_types().override_for(pivot_table_part) != nullptr,
        "pivot worksheet rewrite should preserve pivot table content type override");
    check(output_reader.content_types().override_for(pivot_cache_definition_part) != nullptr,
        "pivot worksheet rewrite should preserve pivot cache definition content type override");
    check(output_reader.content_types().override_for(pivot_cache_records_part) != nullptr,
        "pivot worksheet rewrite should preserve pivot cache records content type override");
}

void test_package_editor_replaces_pivot_table_and_preserves_cache_chain()
{
    const PivotSourcePackage source =
        write_pivot_source_package("fastxlsx-package-editor-replace-pivot-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-pivot-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName pivot_table_part("/xl/pivotTables/pivotTable1.xml");
    const fastxlsx::detail::PartName pivot_cache_definition_part(
        "/xl/pivotCache/pivotCacheDefinition1.xml");
    const fastxlsx::detail::PartName pivot_cache_records_part(
        "/xl/pivotCache/pivotCacheRecords1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string replacement_pivot_table =
        R"(<pivotTableDefinition xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" name="PivotTable1Patched" cacheId="1" dataCaption="Patched Values">)"
        R"(<location ref="G3:H8" firstHeaderRow="1" firstDataRow="2" firstDataCol="1"/>)"
        R"(<pivotFields count="1"><pivotField axis="axisCol"/></pivotFields>)"
        R"(</pivotTableDefinition>)";
    replace_part_with_memory_chunks(editor, pivot_table_part, replacement_pivot_table,
        "pivot table local-DOM rewrite");

    const auto* pivot_table_plan = editor.edit_plan().find_part(pivot_table_part);
    check(pivot_table_plan != nullptr,
        "pivot table replacement should be present in the edit plan");
    check(pivot_table_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "pivot table replacement should be local-DOM-rewrite");
    check_manifest_write_mode(editor, pivot_table_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "pivot table replacement should mirror write mode into manifest");
    const auto* pivot_table_relationships_plan =
        editor.edit_plan().find_package_entry("xl/pivotTables/_rels/pivotTable1.xml.rels");
    check(pivot_table_relationships_plan != nullptr,
        "pivot table replacement should audit preserved owner relationships");
    check(pivot_table_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table owner relationships should remain copy-original");
    check(pivot_table_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "pivot table owner relationships audit should keep structured role");
    check(pivot_table_relationships_plan->owner_part == pivot_table_part.value(),
        "pivot table owner relationships audit should keep owner part");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_cache_definition_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table replacement should keep pivot cache definition copy-original");
    check(editor.edit_plan().find_part(pivot_cache_records_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table replacement should keep pivot cache records copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table replacement should keep unknown part copy-original");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, pivot_table_part.zip_path());
    check(output_reader.read_entry("xl/pivotTables/pivotTable1.xml")
            == replacement_pivot_table,
        "pivot table replacement should write replacement pivot table XML");
    check(output_reader.read_entry("xl/pivotTables/_rels/pivotTable1.xml.rels")
            == source.pivot_table_relationships,
        "pivot table replacement should preserve owner relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheDefinition1.xml")
            == source.pivot_cache_definition,
        "pivot table replacement should preserve pivot cache definition bytes");
    check(output_reader.read_entry("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            == source.pivot_cache_definition_relationships,
        "pivot table replacement should preserve pivot cache definition relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheRecords1.xml")
            == source.pivot_cache_records,
        "pivot table replacement should preserve pivot cache records bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "pivot table replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "pivot table replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "pivot table replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "pivot table replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "pivot table replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "pivot table replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "pivot table replacement should preserve unknown bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "pivot table replacement should keep worksheet relationships readable");
    check(worksheet_relationships->find_by_id("rIdPivotTable") != nullptr,
        "pivot table replacement should keep worksheet pivot table relationship");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "pivot table replacement should keep workbook relationships readable");
    check(workbook_relationships->find_by_id("rIdPivotCache") != nullptr,
        "pivot table replacement should keep workbook pivot cache relationship");
    const auto* pivot_table_relationships =
        output_reader.relationships_for(pivot_table_part);
    check(pivot_table_relationships != nullptr,
        "pivot table replacement should keep pivot table owner relationships readable");
    check(pivot_table_relationships->find_by_id("rIdPivotCacheDef") != nullptr,
        "pivot table replacement should keep pivot table cache definition relationship");
    const auto* pivot_cache_relationships =
        output_reader.relationships_for(pivot_cache_definition_part);
    check(pivot_cache_relationships != nullptr,
        "pivot table replacement should keep pivot cache definition relationships readable");
    check(pivot_cache_relationships->find_by_id("rIdPivotRecords") != nullptr,
        "pivot table replacement should keep pivot cache records relationship");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(worksheet_part)->find_by_id("rIdPivotTable")
            != nullptr,
        "relationship graph should keep worksheet pivot table relationship after replacement");
    check(output_graph.relationships_for(pivot_table_part)->find_by_id("rIdPivotCacheDef")
            != nullptr,
        "relationship graph should keep pivot table cache relationship after replacement");
    check(output_graph.relationships_for(pivot_cache_definition_part)
                ->find_by_id("rIdPivotRecords")
            != nullptr,
        "relationship graph should keep pivot cache records relationship after replacement");
    check(output_reader.content_types().override_for(pivot_table_part) != nullptr,
        "pivot table replacement should keep pivot table content type override");
    check(output_reader.content_types().override_for(pivot_cache_definition_part) != nullptr,
        "pivot table replacement should keep pivot cache definition content type override");
    check(output_reader.content_types().override_for(pivot_cache_records_part) != nullptr,
        "pivot table replacement should keep pivot cache records content type override");
}

void test_package_editor_repeated_pivot_table_replacement_updates_final_state()
{
    const PivotSourcePackage source =
        write_pivot_source_package("fastxlsx-package-editor-repeat-pivot-table-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-repeat-pivot-table-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName pivot_table_part("/xl/pivotTables/pivotTable1.xml");
    const fastxlsx::detail::PartName pivot_cache_definition_part(
        "/xl/pivotCache/pivotCacheDefinition1.xml");
    const fastxlsx::detail::PartName pivot_cache_records_part(
        "/xl/pivotCache/pivotCacheRecords1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");
    const std::string pivot_table_relationships_entry =
        "xl/pivotTables/_rels/pivotTable1.xml.rels";

    const std::string stale_pivot_table =
        R"(<pivotTableDefinition xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" name="PivotTable1Stale" cacheId="1" dataCaption="Stale Values">)"
        R"(<location ref="F3:G8" firstHeaderRow="1" firstDataRow="2" firstDataCol="1"/>)"
        R"(<pivotFields count="1"><pivotField axis="axisRow"/></pivotFields>)"
        R"(</pivotTableDefinition>)";
    const std::string final_pivot_table =
        R"(<pivotTableDefinition xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" name="PivotTable1Final" cacheId="1" dataCaption="Final Values">)"
        R"(<location ref="H4:I9" firstHeaderRow="1" firstDataRow="2" firstDataCol="1"/>)"
        R"(<pivotFields count="1"><pivotField axis="axisCol"/></pivotFields>)"
        R"(</pivotTableDefinition>)";

    replace_part_with_memory_chunks(editor, pivot_table_part, stale_pivot_table,
        "stale repeated pivot table local-DOM rewrite");
    replace_part_with_memory_chunks(editor, pivot_table_part, final_pivot_table,
        "final repeated pivot table local-DOM rewrite");

    const auto* pivot_table_plan = editor.edit_plan().find_part(pivot_table_part);
    check(pivot_table_plan != nullptr,
        "repeated pivot table replacement should keep an active edit-plan part");
    check(pivot_table_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated pivot table replacement should keep final local-DOM-rewrite mode");
    check(pivot_table_plan->reason.find("final repeated") != std::string::npos,
        "repeated pivot table replacement should keep final reason");
    check(pivot_table_plan->reason.find("stale repeated") == std::string::npos,
        "repeated pivot table replacement should drop stale reason");
    check_manifest_write_mode(editor, pivot_table_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated pivot table replacement should mirror final write mode into manifest");
    check(editor.manifest().content_types().override_for(pivot_table_part) != nullptr,
        "repeated pivot table replacement should keep pivot table content type override");
    check(editor.edit_plan().find_removed_part(pivot_table_part) == nullptr,
        "repeated pivot table replacement should not leave removed-part audit");
    check(editor.edit_plan().find_removed_package_entry(pivot_table_relationships_entry)
            == nullptr,
        "repeated pivot table replacement should not leave owner relationships omission");
    const auto* pivot_table_relationships_audit =
        editor.edit_plan().find_package_entry(pivot_table_relationships_entry);
    check(pivot_table_relationships_audit != nullptr,
        "repeated pivot table replacement should preserve owner relationships audit");
    check(pivot_table_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot table replacement should preserve owner relationships bytes");
    check(pivot_table_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "repeated pivot table replacement should keep owner relationship audit role");
    check(pivot_table_relationships_audit->owner_part == pivot_table_part.value(),
        "repeated pivot table replacement should keep owner relationship context");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "repeated pivot table replacement should not rewrite content types audit");
    check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels") == nullptr,
        "repeated pivot table replacement should not rewrite workbook relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot table replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot table replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_cache_definition_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot table replacement should keep pivot cache definition copy-original");
    check(editor.edit_plan().find_part(pivot_cache_records_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot table replacement should keep pivot cache records copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot table replacement should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "repeated pivot table replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "repeated pivot table replacement output plan should preserve calcChain policy");
    check(output_plan.relationship_target_audits.empty(),
        "repeated pivot table replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "repeated pivot table replacement output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "repeated pivot table replacement output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "xl/pivotTables/pivotTable1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "repeated pivot table replacement output plan should rewrite pivot table");
    const auto* output_pivot_table_plan =
        find_output_entry_plan(output_plan.entries, "xl/pivotTables/pivotTable1.xml");
    check(output_pivot_table_plan->reason.find("final repeated") != std::string::npos,
        "repeated pivot table replacement output plan should keep final reason");
    check(output_pivot_table_plan->reason.find("stale repeated") == std::string::npos,
        "repeated pivot table replacement output plan should drop stale reason");
    check_output_entry_plan(output_plan.entries, pivot_table_relationships_entry,
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot table replacement output plan should preserve owner relationships");
    const auto* output_pivot_table_relationships_plan =
        find_output_entry_plan(output_plan.entries, pivot_table_relationships_entry);
    check(output_pivot_table_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "repeated pivot table replacement output plan should keep owner relationship audit role");
    check(output_pivot_table_relationships_plan->owner_part == pivot_table_part.value(),
        "repeated pivot table replacement output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot table replacement output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot table replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot table replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot table replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot table replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot table replacement output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheDefinition1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot table replacement output plan should preserve pivot cache definition");
    check_output_entry_plan(output_plan.entries,
        "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot table replacement output plan should preserve pivot cache definition relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheRecords1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot table replacement output plan should preserve pivot cache records");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot table replacement output plan should preserve unknown entry");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, pivot_table_part.zip_path());
    check(output_reader.read_entry("xl/pivotTables/pivotTable1.xml") == final_pivot_table,
        "repeated pivot table replacement should write final pivot table payload");
    check(output_reader.read_entry("xl/pivotTables/_rels/pivotTable1.xml.rels")
            == source.pivot_table_relationships,
        "repeated pivot table replacement should preserve owner relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheDefinition1.xml")
            == source.pivot_cache_definition,
        "repeated pivot table replacement should preserve pivot cache definition bytes");
    check(output_reader.read_entry("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            == source.pivot_cache_definition_relationships,
        "repeated pivot table replacement should preserve pivot cache definition relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheRecords1.xml")
            == source.pivot_cache_records,
        "repeated pivot table replacement should preserve pivot cache records bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "repeated pivot table replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "repeated pivot table replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "repeated pivot table replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "repeated pivot table replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "repeated pivot table replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "repeated pivot table replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "repeated pivot table replacement should preserve unknown bytes");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(worksheet_part)->find_by_id("rIdPivotTable")
            != nullptr,
        "relationship graph should keep worksheet pivot table relationship after repeated replacement");
    check(output_graph.relationships_for(pivot_table_part)->find_by_id("rIdPivotCacheDef")
            != nullptr,
        "relationship graph should keep pivot table cache relationship after repeated replacement");
    check(output_graph.relationships_for(pivot_cache_definition_part)
                ->find_by_id("rIdPivotRecords")
            != nullptr,
        "relationship graph should keep pivot cache records relationship after repeated replacement");
    check(output_reader.content_types().override_for(pivot_table_part) != nullptr,
        "repeated pivot table replacement should keep pivot table content type override");
}

void test_package_editor_removes_pivot_table_and_preserves_cache_chain()
{
    const PivotSourcePackage source =
        write_pivot_source_package("fastxlsx-package-editor-remove-pivot-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-pivot-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName pivot_table_part("/xl/pivotTables/pivotTable1.xml");
    const fastxlsx::detail::PartName pivot_cache_definition_part(
        "/xl/pivotCache/pivotCacheDefinition1.xml");
    const fastxlsx::detail::PartName pivot_cache_records_part(
        "/xl/pivotCache/pivotCacheRecords1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    editor.remove_part(pivot_table_part, "explicit pivot table part removal");

    check(editor.edit_plan().find_part(pivot_table_part) == nullptr,
        "pivot table removal should clear the active edit-plan part");
    const auto* removed_pivot_table = editor.edit_plan().find_removed_part(pivot_table_part);
    check(removed_pivot_table != nullptr,
        "pivot table removal should record removed-part audit");
    check(removed_pivot_table->reason.find("pivot table part") != std::string::npos,
        "pivot table removal should retain the removal reason");
    check(removed_pivot_table->reason.find("inbound relationship preserved")
            != std::string::npos,
        "pivot table removal should audit preserved inbound relationships");
    check(removed_pivot_table->inbound_relationships.size() == 1,
        "pivot table removal should keep structured inbound audit");
    const auto& pivot_table_inbound =
        removed_pivot_table->inbound_relationships.front();
    check(pivot_table_inbound.owner_part == worksheet_part.value(),
        "pivot table removal should keep inbound owner part");
    check(pivot_table_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "pivot table removal should keep inbound owner relationship entry");
    check(pivot_table_inbound.relationship_id == "rIdPivotTable",
        "pivot table removal should keep inbound relationship id");
    check(pivot_table_inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotTable",
        "pivot table removal should keep inbound relationship type");
    check(pivot_table_inbound.relationship_target == "../pivotTables/pivotTable1.xml",
        "pivot table removal should keep inbound raw target");
    check(pivot_table_inbound.target_part == pivot_table_part,
        "pivot table removal should keep normalized target part");
    check(editor.manifest().find_part(pivot_table_part) == nullptr,
        "pivot table removal should remove the part from the manifest");
    check(editor.manifest().content_types().override_for(pivot_table_part) == nullptr,
        "pivot table removal should remove the manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "pivot table removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "pivot table removal content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "pivot table removal content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry(
              "xl/pivotTables/_rels/pivotTable1.xml.rels")
            != nullptr,
        "pivot table removal should omit owner relationships entry");
    check(editor.edit_plan().find_package_entry("xl/pivotTables/_rels/pivotTable1.xml.rels")
            == nullptr,
        "pivot table removal should not keep active owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_cache_definition_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table removal should keep pivot cache definition copy-original");
    check(editor.edit_plan().find_part(pivot_cache_records_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table removal should keep pivot cache records copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table removal should keep unknown part copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/pivotTables/pivotTable1.xml") == entries.end(),
        "pivot table removal output should omit pivot table part");
    check(entries.find("xl/pivotTables/_rels/pivotTable1.xml.rels") == entries.end(),
        "pivot table removal output should omit pivot table owner relationships");
    check(entries.find("xl/pivotCache/pivotCacheDefinition1.xml") != entries.end(),
        "pivot table removal output should keep pivot cache definition");
    check(entries.find("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels") != entries.end(),
        "pivot table removal output should keep pivot cache definition relationships");
    check(entries.find("xl/pivotCache/pivotCacheRecords1.xml") != entries.end(),
        "pivot table removal output should keep pivot cache records");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(pivot_table_part) == nullptr,
        "pivot table removal output should remove pivot table content type override");
    check(output_reader.content_types().override_for(pivot_cache_definition_part) != nullptr,
        "pivot table removal output should keep pivot cache definition content type override");
    check(output_reader.content_types().override_for(pivot_cache_records_part) != nullptr,
        "pivot table removal output should keep pivot cache records content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/pivotTables/pivotTable1.xml",
        "pivot table removal content types XML should omit pivot table override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "pivot table removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "pivot table removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "pivot table removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "pivot table removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "pivot table removal should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheDefinition1.xml")
            == source.pivot_cache_definition,
        "pivot table removal should preserve pivot cache definition bytes");
    check(output_reader.read_entry("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            == source.pivot_cache_definition_relationships,
        "pivot table removal should preserve pivot cache definition relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheRecords1.xml")
            == source.pivot_cache_records,
        "pivot table removal should preserve pivot cache records bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "pivot table removal should preserve unknown bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "pivot table removal should keep worksheet relationships readable");
    const auto* pivot_table_relationship = worksheet_relationships->find_by_id("rIdPivotTable");
    check(pivot_table_relationship != nullptr,
        "pivot table removal should keep inbound worksheet pivot table relationship id");
    check(pivot_table_relationship->target == "../pivotTables/pivotTable1.xml",
        "pivot table removal should not rewrite inbound pivot table target");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "pivot table removal should keep workbook relationships readable");
    check(workbook_relationships->find_by_id("rIdPivotCache") != nullptr,
        "pivot table removal should keep workbook pivot cache relationship");
    const auto* pivot_cache_relationships =
        output_reader.relationships_for(pivot_cache_definition_part);
    check(pivot_cache_relationships != nullptr,
        "pivot table removal should keep pivot cache definition relationships readable");
    check(pivot_cache_relationships->find_by_id("rIdPivotRecords") != nullptr,
        "pivot table removal should keep pivot cache records relationship");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(worksheet_part)->find_by_id("rIdPivotTable")
            != nullptr,
        "relationship graph should keep inbound pivot table relationship after removal");
    check(output_graph.relationships_for(workbook_part)->find_by_id("rIdPivotCache")
            != nullptr,
        "relationship graph should keep workbook pivot cache relationship after removal");
    check(output_graph.relationships_for(pivot_cache_definition_part)
                ->find_by_id("rIdPivotRecords")
            != nullptr,
        "relationship graph should keep pivot cache records relationship after removal");
    check(output_reader.relationships_for(pivot_table_part) == nullptr,
        "pivot table removal should not keep owner relationships for absent part");
}

void test_package_editor_pivot_table_replacement_restores_prior_removal()
{
    const PivotSourcePackage source =
        write_pivot_source_package(
            "fastxlsx-package-editor-replace-after-remove-pivot-table-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-pivot-table-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName pivot_table_part("/xl/pivotTables/pivotTable1.xml");
    const fastxlsx::detail::PartName pivot_cache_definition_part(
        "/xl/pivotCache/pivotCacheDefinition1.xml");
    const fastxlsx::detail::PartName pivot_cache_records_part(
        "/xl/pivotCache/pivotCacheRecords1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");
    const std::string pivot_table_relationships_entry =
        "xl/pivotTables/_rels/pivotTable1.xml.rels";

    editor.remove_part(pivot_table_part, "temporary pivot table removal");
    check(editor.edit_plan().find_removed_part(pivot_table_part) != nullptr,
        "setup should record removed pivot table before replacement restore");
    check(editor.edit_plan().find_removed_package_entry(pivot_table_relationships_entry)
            != nullptr,
        "setup should omit pivot table owner relationships before restore");
    check(editor.manifest().find_part(pivot_table_part) == nullptr,
        "setup should remove pivot table from manifest before restore");

    const std::string restored_pivot_table =
        R"(<pivotTableDefinition xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" name="PivotTable1Restored" cacheId="1" dataCaption="Restored Values">)"
        R"(<location ref="F3:G8" firstHeaderRow="1" firstDataRow="2" firstDataCol="1"/>)"
        R"(<pivotFields count="1"><pivotField axis="axisRow"/></pivotFields>)"
        R"(</pivotTableDefinition>)";
    replace_part_with_memory_chunks(editor, pivot_table_part, restored_pivot_table,
        "restored pivot table after removal");

    check(editor.edit_plan().find_removed_part(pivot_table_part) == nullptr,
        "pivot table replacement after removal should clear stale removed-part audit");
    check(editor.edit_plan().find_removed_package_entry(pivot_table_relationships_entry)
            == nullptr,
        "pivot table replacement after removal should clear stale owner relationships omission");
    const auto* pivot_table_plan = editor.edit_plan().find_part(pivot_table_part);
    check(pivot_table_plan != nullptr,
        "pivot table replacement after removal should restore active edit-plan part");
    check(pivot_table_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "pivot table replacement after removal should keep final write mode");
    check(pivot_table_plan->reason.find("after removal") != std::string::npos,
        "pivot table replacement after removal should keep final replacement reason");
    check_manifest_write_mode(editor, pivot_table_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "pivot table replacement after removal should restore manifest write mode");

    const auto* pivot_table_relationships_audit =
        editor.edit_plan().find_package_entry(pivot_table_relationships_entry);
    check(pivot_table_relationships_audit != nullptr,
        "pivot table replacement after removal should restore owner relationships audit");
    check(pivot_table_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table replacement after removal owner relationships should be copy-original");
    check(pivot_table_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "pivot table replacement after removal owner relationships should keep source role");
    check(pivot_table_relationships_audit->owner_part == pivot_table_part.value(),
        "pivot table replacement after removal owner relationships should keep owner part");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "pivot table replacement after removal should keep content types audit");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table replacement after removal should restore source content types audit");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "pivot table replacement after removal content types audit should keep structured role");
    check(editor.manifest().content_types().override_for(pivot_table_part) != nullptr,
        "pivot table replacement after removal should restore manifest content type override");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table replacement after removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table replacement after removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_cache_definition_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table replacement after removal should keep pivot cache definition copy-original");
    check(editor.edit_plan().find_part(pivot_cache_records_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table replacement after removal should keep pivot cache records copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table replacement after removal should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "pivot table replacement after removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "pivot table replacement after removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "pivot table replacement after removal output plan should not invent dependency audits");
    check_output_entry_plan(output_plan.entries, "xl/pivotTables/pivotTable1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "pivot table replacement after removal output plan should rewrite pivot table part");
    check_output_entry_part_context(output_plan.entries, "xl/pivotTables/pivotTable1.xml",
        true, pivot_table_part.value(),
        "pivot table replacement after removal output plan should classify rewritten pivot table");
    const auto* output_pivot_table_plan =
        find_output_entry_plan(output_plan.entries, "xl/pivotTables/pivotTable1.xml");
    check(output_pivot_table_plan->reason.find("after removal") != std::string::npos,
        "pivot table replacement after removal output plan should keep replacement reason");
    check_output_entry_plan(output_plan.entries, pivot_table_relationships_entry,
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table replacement after removal output plan should preserve owner relationships");
    check_output_entry_part_context(output_plan.entries, pivot_table_relationships_entry,
        false, "",
        "pivot table replacement after removal output plan should classify owner relationships as metadata");
    const auto* output_pivot_table_relationships_plan =
        find_output_entry_plan(output_plan.entries, pivot_table_relationships_entry);
    check(output_pivot_table_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "pivot table replacement after removal output plan should classify owner relationships metadata");
    check(output_pivot_table_relationships_plan->owner_part == pivot_table_part.value(),
        "pivot table replacement after removal output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table replacement after removal output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "pivot table replacement after removal output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "pivot table replacement after removal output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table replacement after removal output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table replacement after removal output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table replacement after removal output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table replacement after removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table replacement after removal output plan should preserve inbound worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheDefinition1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table replacement after removal output plan should preserve cache definition");
    check_output_entry_plan(output_plan.entries,
        "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table replacement after removal output plan should preserve cache definition relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheRecords1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table replacement after removal output plan should preserve cache records");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table replacement after removal output plan should preserve unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/pivotTables/pivotTable1.xml") != entries.end(),
        "pivot table replacement after removal output should restore pivot table entry");
    check(entries.find(pivot_table_relationships_entry) != entries.end(),
        "pivot table replacement after removal output should restore owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, pivot_table_part.zip_path());
    check(output_reader.read_entry("xl/pivotTables/pivotTable1.xml")
            == restored_pivot_table,
        "pivot table replacement after removal should write restored payload");
    check(output_reader.read_entry(pivot_table_relationships_entry)
            == source.pivot_table_relationships,
        "pivot table replacement after removal should preserve owner relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "pivot table replacement after removal should preserve content types bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "pivot table replacement after removal should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheDefinition1.xml")
            == source.pivot_cache_definition,
        "pivot table replacement after removal should preserve cache definition bytes");
    check(output_reader.read_entry("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            == source.pivot_cache_definition_relationships,
        "pivot table replacement after removal should preserve cache definition relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheRecords1.xml")
            == source.pivot_cache_records,
        "pivot table replacement after removal should preserve cache records bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "pivot table replacement after removal should preserve unknown bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "pivot table replacement after removal should keep worksheet relationships readable");
    check(worksheet_relationships->find_by_id("rIdPivotTable") != nullptr,
        "pivot table replacement after removal should keep inbound worksheet relationship");
    const auto* pivot_table_relationships =
        output_reader.relationships_for(pivot_table_part);
    check(pivot_table_relationships != nullptr,
        "pivot table replacement after removal should keep owner relationships readable");
    check(pivot_table_relationships->find_by_id("rIdPivotCacheDef") != nullptr,
        "pivot table replacement after removal should keep cache definition relationship");
    const auto* cache_definition_relationships =
        output_reader.relationships_for(pivot_cache_definition_part);
    check(cache_definition_relationships != nullptr,
        "pivot table replacement after removal should keep cache definition relationships readable");
    check(cache_definition_relationships->find_by_id("rIdPivotRecords") != nullptr,
        "pivot table replacement after removal should keep cache records relationship");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(worksheet_part)->find_by_id("rIdPivotTable")
            != nullptr,
        "relationship graph should keep worksheet pivot table relationship after restore");
    check(output_graph.relationships_for(pivot_table_part)->find_by_id("rIdPivotCacheDef")
            != nullptr,
        "relationship graph should keep pivot table cache relationship after restore");
    check(output_graph.relationships_for(pivot_cache_definition_part)
                ->find_by_id("rIdPivotRecords")
            != nullptr,
        "relationship graph should keep cache records relationship after pivot table restore");
    check(output_reader.content_types().override_for(pivot_table_part) != nullptr,
        "pivot table replacement after removal should keep pivot table content type override");
    check(output_reader.content_types().override_for(pivot_cache_definition_part) != nullptr,
        "pivot table replacement after removal should keep cache definition content type override");
    check(output_reader.content_types().override_for(pivot_cache_records_part) != nullptr,
        "pivot table replacement after removal should keep cache records content type override");
}

void test_package_editor_pivot_table_removal_overrides_prior_replacement()
{
    const PivotSourcePackage source =
        write_pivot_source_package(
            "fastxlsx-package-editor-remove-after-replace-pivot-table-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-pivot-table-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName pivot_table_part("/xl/pivotTables/pivotTable1.xml");
    const fastxlsx::detail::PartName pivot_cache_definition_part(
        "/xl/pivotCache/pivotCacheDefinition1.xml");
    const fastxlsx::detail::PartName pivot_cache_records_part(
        "/xl/pivotCache/pivotCacheRecords1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");
    const std::string pivot_table_relationships_entry =
        "xl/pivotTables/_rels/pivotTable1.xml.rels";

    const std::string stale_pivot_table =
        R"(<pivotTableDefinition xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" name="PivotTable1Stale" cacheId="1" dataCaption="Stale Values">)"
        R"(<location ref="H3:I8" firstHeaderRow="1" firstDataRow="2" firstDataCol="1"/>)"
        R"(<pivotFields count="1"><pivotField axis="axisCol"/></pivotFields>)"
        R"(</pivotTableDefinition>)";
    replace_part_with_memory_chunks(editor, pivot_table_part, stale_pivot_table,
        "stale pivot table replacement before removal");
    check(editor.edit_plan().find_part(pivot_table_part) != nullptr,
        "setup should record active pivot table replacement before removal");
    check(editor.edit_plan().find_package_entry(pivot_table_relationships_entry)
            != nullptr,
        "setup should preserve owner relationships before final pivot table removal");

    editor.remove_part(pivot_table_part, "final pivot table removal after replacement");

    check(editor.edit_plan().find_part(pivot_table_part) == nullptr,
        "pivot table removal after replacement should clear active replacement");
    const auto* removed_pivot_table = editor.edit_plan().find_removed_part(pivot_table_part);
    check(removed_pivot_table != nullptr,
        "pivot table removal after replacement should record removed-part audit");
    check(removed_pivot_table->reason.find("after replacement") != std::string::npos,
        "pivot table removal after replacement should keep final removal reason");
    check(removed_pivot_table->inbound_relationships.size() == 1,
        "pivot table removal after replacement should keep worksheet inbound audit");
    const auto& inbound = removed_pivot_table->inbound_relationships.front();
    check(inbound.owner_part == worksheet_part.value(),
        "pivot table removal after replacement should audit worksheet owner part");
    check(inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "pivot table removal after replacement should audit worksheet relationships entry");
    check(inbound.relationship_id == "rIdPivotTable",
        "pivot table removal after replacement should audit pivot table relationship id");
    check(inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotTable",
        "pivot table removal after replacement should audit pivot table relationship type");
    check(inbound.relationship_target == "../pivotTables/pivotTable1.xml",
        "pivot table removal after replacement should audit raw pivot table target");
    check(inbound.target_part == pivot_table_part,
        "pivot table removal after replacement should audit normalized pivot table target");
    check(editor.edit_plan().find_removed_package_entry(pivot_table_relationships_entry)
            != nullptr,
        "pivot table removal after replacement should omit owner relationships entry");
    check(editor.edit_plan().find_package_entry(pivot_table_relationships_entry) == nullptr,
        "pivot table removal after replacement should clear active owner relationships audit");
    check(editor.manifest().find_part(pivot_table_part) == nullptr,
        "pivot table removal after replacement should remove manifest part");
    check(editor.manifest().content_types().override_for(pivot_table_part) == nullptr,
        "pivot table removal after replacement should remove manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "pivot table removal after replacement should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "pivot table removal after replacement content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "pivot table removal after replacement content types audit should keep structured role");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table removal after replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_cache_definition_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table removal after replacement should keep cache definition copy-original");
    check(editor.edit_plan().find_part(pivot_cache_records_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table removal after replacement should keep cache records copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table removal after replacement should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "pivot table removal after replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "pivot table removal after replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "pivot table removal after replacement output plan should not invent dependency audits");
    check_output_entry_plan(output_plan.entries, "xl/pivotTables/pivotTable1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "pivot table removal after replacement output plan should omit pivot table part");
    check_output_entry_part_context(output_plan.entries, "xl/pivotTables/pivotTable1.xml",
        true, pivot_table_part.value(),
        "pivot table removal after replacement output plan should classify omitted pivot table");
    const auto* output_pivot_table_plan =
        find_output_entry_plan(output_plan.entries, "xl/pivotTables/pivotTable1.xml");
    check(output_pivot_table_plan->reason.find("after replacement") != std::string::npos,
        "pivot table removal after replacement output plan should keep final removal reason");
    check(output_pivot_table_plan->inbound_relationships.size() == 1,
        "pivot table removal after replacement output plan should expose worksheet inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "xl/pivotTables/pivotTable1.xml", worksheet_part.value(),
        "xl/worksheets/_rels/sheet1.xml.rels", "rIdPivotTable",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotTable",
        "../pivotTables/pivotTable1.xml", pivot_table_part,
        "pivot table removal after replacement output plan should keep worksheet inbound audit");
    check_output_entry_plan(output_plan.entries,
        "xl/pivotTables/_rels/pivotTable1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "pivot table removal after replacement output plan should omit owner relationships");
    check_output_entry_part_context(output_plan.entries,
        "xl/pivotTables/_rels/pivotTable1.xml.rels", false, "",
        "pivot table removal after replacement output plan should classify owner relationships as metadata");
    const auto* output_pivot_table_relationships_plan =
        find_output_entry_plan(output_plan.entries, pivot_table_relationships_entry);
    check(output_pivot_table_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "pivot table removal after replacement output plan should classify owner relationships metadata");
    check(output_pivot_table_relationships_plan->owner_part == pivot_table_part.value(),
        "pivot table removal after replacement output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "pivot table removal after replacement output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "pivot table removal after replacement output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "pivot table removal after replacement output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table removal after replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table removal after replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table removal after replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table removal after replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table removal after replacement output plan should preserve inbound worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheDefinition1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table removal after replacement output plan should preserve cache definition");
    check_output_entry_plan(output_plan.entries,
        "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table removal after replacement output plan should preserve cache definition relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheRecords1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table removal after replacement output plan should preserve cache records");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table removal after replacement output plan should preserve unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/pivotTables/pivotTable1.xml") == entries.end(),
        "pivot table removal after replacement output should omit pivot table part");
    check(entries.find(pivot_table_relationships_entry) == entries.end(),
        "pivot table removal after replacement output should omit owner relationships");
    check(entries.find("xl/pivotCache/pivotCacheDefinition1.xml") != entries.end(),
        "pivot table removal after replacement output should keep cache definition");
    check(entries.find("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels") != entries.end(),
        "pivot table removal after replacement output should keep cache definition relationships");
    check(entries.find("xl/pivotCache/pivotCacheRecords1.xml") != entries.end(),
        "pivot table removal after replacement output should keep cache records");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(pivot_table_part) == nullptr,
        "pivot table removal after replacement output should remove pivot table content type override");
    check(output_reader.content_types().override_for(pivot_cache_definition_part) != nullptr,
        "pivot table removal after replacement output should keep cache definition content type override");
    check(output_reader.content_types().override_for(pivot_cache_records_part) != nullptr,
        "pivot table removal after replacement output should keep cache records content type override");
    check(output_reader.content_types().default_for("bin") != nullptr,
        "pivot table removal after replacement output should keep unknown extension default content type");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/pivotTables/pivotTable1.xml",
        "pivot table removal after replacement content types XML should omit pivot table override");
    check_contains(output_content_types, "/xl/pivotCache/pivotCacheDefinition1.xml",
        "pivot table removal after replacement content types XML should keep cache definition override");
    check_contains(output_content_types, "/xl/pivotCache/pivotCacheRecords1.xml",
        "pivot table removal after replacement content types XML should keep cache records override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "pivot table removal after replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "pivot table removal after replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "pivot table removal after replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "pivot table removal after replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "pivot table removal after replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheDefinition1.xml")
            == source.pivot_cache_definition,
        "pivot table removal after replacement should preserve cache definition bytes");
    check(output_reader.read_entry("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            == source.pivot_cache_definition_relationships,
        "pivot table removal after replacement should preserve cache definition relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheRecords1.xml")
            == source.pivot_cache_records,
        "pivot table removal after replacement should preserve cache records bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "pivot table removal after replacement should preserve unknown bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "pivot table removal after replacement should keep worksheet relationships readable");
    const auto* pivot_table_relationship = worksheet_relationships->find_by_id("rIdPivotTable");
    check(pivot_table_relationship != nullptr,
        "pivot table removal after replacement should keep inbound worksheet pivot table relationship");
    check(pivot_table_relationship->target == "../pivotTables/pivotTable1.xml",
        "pivot table removal after replacement should not rewrite inbound pivot table target");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "pivot table removal after replacement should keep workbook relationships readable");
    check(workbook_relationships->find_by_id("rIdPivotCache") != nullptr,
        "pivot table removal after replacement should keep workbook pivot cache relationship");
    const auto* cache_definition_relationships =
        output_reader.relationships_for(pivot_cache_definition_part);
    check(cache_definition_relationships != nullptr,
        "pivot table removal after replacement should keep cache definition relationships readable");
    check(cache_definition_relationships->find_by_id("rIdPivotRecords") != nullptr,
        "pivot table removal after replacement should keep cache records relationship");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(worksheet_part)->find_by_id("rIdPivotTable")
            != nullptr,
        "relationship graph should keep inbound pivot table relationship after final removal");
    check(output_graph.relationships_for(workbook_part)->find_by_id("rIdPivotCache")
            != nullptr,
        "relationship graph should keep workbook cache definition relationship after final removal");
    check(output_graph.relationships_for(pivot_cache_definition_part)
                ->find_by_id("rIdPivotRecords")
            != nullptr,
        "relationship graph should keep cache records relationship after final removal");
    check(output_reader.relationships_for(pivot_table_part) == nullptr,
        "pivot table removal after replacement should not keep owner relationships for absent part");
}

void test_package_editor_replaces_pivot_cache_definition_and_preserves_cache_records()
{
    const PivotSourcePackage source =
        write_pivot_source_package(
            "fastxlsx-package-editor-replace-pivot-cache-definition-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-pivot-cache-definition-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName pivot_table_part("/xl/pivotTables/pivotTable1.xml");
    const fastxlsx::detail::PartName pivot_cache_definition_part(
        "/xl/pivotCache/pivotCacheDefinition1.xml");
    const fastxlsx::detail::PartName pivot_cache_records_part(
        "/xl/pivotCache/pivotCacheRecords1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string replacement_cache_definition =
        R"(<pivotCacheDefinition xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" refreshOnLoad="1" refreshedVersion="9">)"
        R"(<cacheSource type="worksheet"><worksheetSource ref="A1:B4" sheet="Sheet1"/></cacheSource>)"
        R"(<cacheFields count="1"><cacheField name="PatchedValue" numFmtId="0"><sharedItems containsNumber="1"/></cacheField></cacheFields>)"
        R"(<cacheRecords r:id="rIdPivotRecords"/>)"
        R"(</pivotCacheDefinition>)";
    replace_part_with_memory_chunks(editor, pivot_cache_definition_part, replacement_cache_definition,
        "pivot cache definition local-DOM rewrite");

    const auto* cache_definition_plan =
        editor.edit_plan().find_part(pivot_cache_definition_part);
    check(cache_definition_plan != nullptr,
        "pivot cache definition replacement should be present in the edit plan");
    check(cache_definition_plan->write_mode
            == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "pivot cache definition replacement should be staged stream-rewrite");
    check_manifest_write_mode(editor, pivot_cache_definition_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "pivot cache definition replacement should mirror write mode into manifest");
    const auto* cache_definition_relationships_plan =
        editor.edit_plan().find_package_entry(
            "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels");
    check(cache_definition_relationships_plan != nullptr,
        "pivot cache definition replacement should audit preserved owner relationships");
    check(cache_definition_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition owner relationships should remain copy-original");
    check(cache_definition_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "pivot cache definition owner relationships audit should keep structured role");
    check(cache_definition_relationships_plan->owner_part
            == pivot_cache_definition_part.value(),
        "pivot cache definition owner relationships audit should keep owner part");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition replacement should keep pivot table copy-original");
    check(editor.edit_plan().find_part(pivot_cache_records_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition replacement should keep pivot cache records copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition replacement should keep unknown part copy-original");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(
        editor.reader(), output_reader, pivot_cache_definition_part.zip_path());
    check(output_reader.read_entry("xl/pivotCache/pivotCacheDefinition1.xml")
            == replacement_cache_definition,
        "pivot cache definition replacement should write replacement cache definition XML");
    check(output_reader.read_entry("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            == source.pivot_cache_definition_relationships,
        "pivot cache definition replacement should preserve owner relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheRecords1.xml")
            == source.pivot_cache_records,
        "pivot cache definition replacement should preserve pivot cache records bytes");
    check(output_reader.read_entry("xl/pivotTables/pivotTable1.xml") == source.pivot_table,
        "pivot cache definition replacement should preserve pivot table bytes");
    check(output_reader.read_entry("xl/pivotTables/_rels/pivotTable1.xml.rels")
            == source.pivot_table_relationships,
        "pivot cache definition replacement should preserve pivot table relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "pivot cache definition replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "pivot cache definition replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "pivot cache definition replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "pivot cache definition replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "pivot cache definition replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "pivot cache definition replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "pivot cache definition replacement should preserve unknown bytes");

    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "pivot cache definition replacement should keep workbook relationships readable");
    check(workbook_relationships->find_by_id("rIdPivotCache") != nullptr,
        "pivot cache definition replacement should keep workbook pivot cache relationship");
    const auto* pivot_table_relationships =
        output_reader.relationships_for(pivot_table_part);
    check(pivot_table_relationships != nullptr,
        "pivot cache definition replacement should keep pivot table relationships readable");
    check(pivot_table_relationships->find_by_id("rIdPivotCacheDef") != nullptr,
        "pivot cache definition replacement should keep pivot table cache definition relationship");
    const auto* cache_definition_relationships =
        output_reader.relationships_for(pivot_cache_definition_part);
    check(cache_definition_relationships != nullptr,
        "pivot cache definition replacement should keep owner relationships readable");
    check(cache_definition_relationships->find_by_id("rIdPivotRecords") != nullptr,
        "pivot cache definition replacement should keep pivot cache records relationship");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(workbook_part)->find_by_id("rIdPivotCache")
            != nullptr,
        "relationship graph should keep workbook pivot cache relationship after replacement");
    check(output_graph.relationships_for(pivot_table_part)->find_by_id("rIdPivotCacheDef")
            != nullptr,
        "relationship graph should keep pivot table cache relationship after cache replacement");
    check(output_graph.relationships_for(pivot_cache_definition_part)
                ->find_by_id("rIdPivotRecords")
            != nullptr,
        "relationship graph should keep pivot cache records relationship after replacement");
    check(output_reader.content_types().override_for(pivot_cache_definition_part) != nullptr,
        "pivot cache definition replacement should keep pivot cache definition content type override");
    check(output_reader.content_types().override_for(pivot_cache_records_part) != nullptr,
        "pivot cache definition replacement should keep pivot cache records content type override");
}

void test_package_editor_repeated_pivot_cache_definition_replacement_updates_final_state()
{
    const PivotSourcePackage source =
        write_pivot_source_package(
            "fastxlsx-package-editor-repeat-pivot-cache-definition-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-repeat-pivot-cache-definition-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName pivot_table_part("/xl/pivotTables/pivotTable1.xml");
    const fastxlsx::detail::PartName pivot_cache_definition_part(
        "/xl/pivotCache/pivotCacheDefinition1.xml");
    const fastxlsx::detail::PartName pivot_cache_records_part(
        "/xl/pivotCache/pivotCacheRecords1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");
    const std::string pivot_cache_relationships_entry =
        "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels";

    const std::string stale_cache_definition =
        R"(<pivotCacheDefinition xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" refreshOnLoad="1" refreshedVersion="11">)"
        R"(<cacheSource type="worksheet"><worksheetSource ref="A1:B6" sheet="Sheet1"/></cacheSource>)"
        R"(<cacheFields count="1"><cacheField name="StaleValue" numFmtId="0"><sharedItems containsNumber="1"/></cacheField></cacheFields>)"
        R"(<cacheRecords r:id="rIdPivotRecords"/>)"
        R"(</pivotCacheDefinition>)";
    const std::string final_cache_definition =
        R"(<pivotCacheDefinition xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" refreshOnLoad="1" refreshedVersion="12">)"
        R"(<cacheSource type="worksheet"><worksheetSource ref="B2:C7" sheet="Sheet1"/></cacheSource>)"
        R"(<cacheFields count="1"><cacheField name="FinalValue" numFmtId="0"><sharedItems containsNumber="1"/></cacheField></cacheFields>)"
        R"(<cacheRecords r:id="rIdPivotRecords"/>)"
        R"(</pivotCacheDefinition>)";

    replace_part_with_memory_chunks(editor, pivot_cache_definition_part, stale_cache_definition,
        "stale repeated pivot cache definition local-DOM rewrite");
    replace_part_with_memory_chunks(editor, pivot_cache_definition_part, final_cache_definition,
        "final repeated pivot cache definition local-DOM rewrite");

    const auto* cache_definition_plan =
        editor.edit_plan().find_part(pivot_cache_definition_part);
    check(cache_definition_plan != nullptr,
        "repeated pivot cache definition replacement should keep an active edit-plan part");
    check(cache_definition_plan->write_mode
            == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated pivot cache definition replacement should keep final stream-rewrite mode");
    check(cache_definition_plan->reason.find("final repeated") != std::string::npos,
        "repeated pivot cache definition replacement should keep final reason");
    check(cache_definition_plan->reason.find("stale repeated") == std::string::npos,
        "repeated pivot cache definition replacement should drop stale reason");
    check_manifest_write_mode(editor, pivot_cache_definition_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated pivot cache definition replacement should mirror final write mode into manifest");
    check(editor.manifest().content_types().override_for(pivot_cache_definition_part) != nullptr,
        "repeated pivot cache definition replacement should keep cache definition content type override");
    check(editor.edit_plan().find_removed_part(pivot_cache_definition_part) == nullptr,
        "repeated pivot cache definition replacement should not leave removed-part audit");
    check(editor.edit_plan().find_removed_package_entry(pivot_cache_relationships_entry)
            == nullptr,
        "repeated pivot cache definition replacement should not leave owner relationships omission");
    const auto* cache_relationships_audit =
        editor.edit_plan().find_package_entry(pivot_cache_relationships_entry);
    check(cache_relationships_audit != nullptr,
        "repeated pivot cache definition replacement should preserve owner relationships audit");
    check(cache_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot cache definition replacement should preserve owner relationships bytes");
    check(cache_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "repeated pivot cache definition replacement should keep owner relationship audit role");
    check(cache_relationships_audit->owner_part == pivot_cache_definition_part.value(),
        "repeated pivot cache definition replacement should keep owner relationship context");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "repeated pivot cache definition replacement should not rewrite content types audit");
    check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels") == nullptr,
        "repeated pivot cache definition replacement should not rewrite workbook relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot cache definition replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot cache definition replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot cache definition replacement should keep pivot table copy-original");
    check(editor.edit_plan().find_part(pivot_cache_records_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot cache definition replacement should keep pivot cache records copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot cache definition replacement should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "repeated pivot cache definition replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "repeated pivot cache definition replacement output plan should preserve calcChain policy");
    check(output_plan.relationship_target_audits.empty(),
        "repeated pivot cache definition replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "repeated pivot cache definition replacement output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "repeated pivot cache definition replacement output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheDefinition1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "repeated pivot cache definition replacement output plan should rewrite cache definition");
    const auto* output_cache_definition_plan =
        find_output_entry_plan(output_plan.entries,
            "xl/pivotCache/pivotCacheDefinition1.xml");
    check(output_cache_definition_plan->reason.find("final repeated") != std::string::npos,
        "repeated pivot cache definition replacement output plan should keep final reason");
    check(output_cache_definition_plan->reason.find("stale repeated") == std::string::npos,
        "repeated pivot cache definition replacement output plan should drop stale reason");
    check_output_entry_plan(output_plan.entries, pivot_cache_relationships_entry,
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache definition replacement output plan should preserve owner relationships");
    const auto* output_cache_relationships_plan =
        find_output_entry_plan(output_plan.entries, pivot_cache_relationships_entry);
    check(output_cache_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "repeated pivot cache definition replacement output plan should keep owner relationship audit role");
    check(output_cache_relationships_plan->owner_part == pivot_cache_definition_part.value(),
        "repeated pivot cache definition replacement output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache definition replacement output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache definition replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache definition replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache definition replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache definition replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache definition replacement output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotTables/pivotTable1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache definition replacement output plan should preserve pivot table");
    check_output_entry_plan(output_plan.entries,
        "xl/pivotTables/_rels/pivotTable1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache definition replacement output plan should preserve pivot table relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheRecords1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache definition replacement output plan should preserve pivot cache records");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache definition replacement output plan should preserve unknown entry");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader,
        pivot_cache_definition_part.zip_path());
    check(output_reader.read_entry("xl/pivotCache/pivotCacheDefinition1.xml")
            == final_cache_definition,
        "repeated pivot cache definition replacement should write final cache definition payload");
    check(output_reader.read_entry("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            == source.pivot_cache_definition_relationships,
        "repeated pivot cache definition replacement should preserve owner relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheRecords1.xml")
            == source.pivot_cache_records,
        "repeated pivot cache definition replacement should preserve pivot cache records bytes");
    check(output_reader.read_entry("xl/pivotTables/pivotTable1.xml") == source.pivot_table,
        "repeated pivot cache definition replacement should preserve pivot table bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "repeated pivot cache definition replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "repeated pivot cache definition replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "repeated pivot cache definition replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "repeated pivot cache definition replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "repeated pivot cache definition replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "repeated pivot cache definition replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "repeated pivot cache definition replacement should preserve unknown bytes");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(workbook_part)->find_by_id("rIdPivotCache")
            != nullptr,
        "relationship graph should keep workbook cache relationship after repeated replacement");
    check(output_graph.relationships_for(pivot_table_part)->find_by_id("rIdPivotCacheDef")
            != nullptr,
        "relationship graph should keep pivot table cache relationship after repeated replacement");
    check(output_graph.relationships_for(pivot_cache_definition_part)
                ->find_by_id("rIdPivotRecords")
            != nullptr,
        "relationship graph should keep pivot cache records relationship after repeated replacement");
    check(output_reader.content_types().override_for(pivot_cache_definition_part) != nullptr,
        "repeated pivot cache definition replacement should keep cache definition content type override");
}

void test_package_editor_removes_pivot_cache_definition_and_preserves_cache_records()
{
    const PivotSourcePackage source =
        write_pivot_source_package(
            "fastxlsx-package-editor-remove-pivot-cache-definition-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-pivot-cache-definition-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName pivot_table_part("/xl/pivotTables/pivotTable1.xml");
    const fastxlsx::detail::PartName pivot_cache_definition_part(
        "/xl/pivotCache/pivotCacheDefinition1.xml");
    const fastxlsx::detail::PartName pivot_cache_records_part(
        "/xl/pivotCache/pivotCacheRecords1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    editor.remove_part(pivot_cache_definition_part,
        "explicit pivot cache definition part removal");

    check(editor.edit_plan().find_part(pivot_cache_definition_part) == nullptr,
        "pivot cache definition removal should clear the active edit-plan part");
    const auto* removed_cache_definition =
        editor.edit_plan().find_removed_part(pivot_cache_definition_part);
    check(removed_cache_definition != nullptr,
        "pivot cache definition removal should record removed-part audit");
    check(removed_cache_definition->reason.find("pivot cache definition part")
            != std::string::npos,
        "pivot cache definition removal should retain the removal reason");
    check(removed_cache_definition->reason.find("inbound relationship preserved")
            != std::string::npos,
        "pivot cache definition removal should audit preserved inbound relationships");
    check(removed_cache_definition->inbound_relationships.size() == 2,
        "pivot cache definition removal should keep workbook and pivot table inbound audits");
    bool found_workbook_inbound = false;
    bool found_pivot_table_inbound = false;
    for (const auto& inbound : removed_cache_definition->inbound_relationships) {
        if (inbound.owner_part == workbook_part.value()
            && inbound.owner_entry == "xl/_rels/workbook.xml.rels"
            && inbound.relationship_id == "rIdPivotCache"
            && inbound.relationship_target == "pivotCache/pivotCacheDefinition1.xml"
            && inbound.target_part == pivot_cache_definition_part) {
            found_workbook_inbound = true;
        }
        if (inbound.owner_part == pivot_table_part.value()
            && inbound.owner_entry == "xl/pivotTables/_rels/pivotTable1.xml.rels"
            && inbound.relationship_id == "rIdPivotCacheDef"
            && inbound.relationship_target == "../pivotCache/pivotCacheDefinition1.xml"
            && inbound.target_part == pivot_cache_definition_part) {
            found_pivot_table_inbound = true;
        }
    }
    check(found_workbook_inbound,
        "pivot cache definition removal should audit workbook inbound relationship");
    check(found_pivot_table_inbound,
        "pivot cache definition removal should audit pivot table inbound relationship");
    check(editor.manifest().find_part(pivot_cache_definition_part) == nullptr,
        "pivot cache definition removal should remove the part from the manifest");
    check(editor.manifest().content_types().override_for(pivot_cache_definition_part)
            == nullptr,
        "pivot cache definition removal should remove the manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "pivot cache definition removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "pivot cache definition removal content types rewrite should be local-DOM-rewrite");
    check(editor.edit_plan().find_removed_package_entry(
              "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            != nullptr,
        "pivot cache definition removal should omit owner relationships entry");
    check(editor.edit_plan().find_package_entry(
              "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            == nullptr,
        "pivot cache definition removal should not keep active owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition removal should keep pivot table copy-original");
    check(editor.edit_plan().find_part(pivot_cache_records_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition removal should keep pivot cache records copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition removal should keep unknown part copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/pivotCache/pivotCacheDefinition1.xml") == entries.end(),
        "pivot cache definition removal output should omit pivot cache definition part");
    check(entries.find("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels") == entries.end(),
        "pivot cache definition removal output should omit owner relationships");
    check(entries.find("xl/pivotCache/pivotCacheRecords1.xml") != entries.end(),
        "pivot cache definition removal output should keep pivot cache records");
    check(entries.find("xl/pivotTables/pivotTable1.xml") != entries.end(),
        "pivot cache definition removal output should keep pivot table");
    check(entries.find("xl/pivotTables/_rels/pivotTable1.xml.rels") != entries.end(),
        "pivot cache definition removal output should keep pivot table relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(pivot_cache_definition_part) == nullptr,
        "pivot cache definition removal output should remove cache definition content type override");
    check(output_reader.content_types().override_for(pivot_cache_records_part) != nullptr,
        "pivot cache definition removal output should keep cache records content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/pivotCache/pivotCacheDefinition1.xml",
        "pivot cache definition removal content types XML should omit cache definition override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "pivot cache definition removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "pivot cache definition removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "pivot cache definition removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "pivot cache definition removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "pivot cache definition removal should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/pivotTables/pivotTable1.xml") == source.pivot_table,
        "pivot cache definition removal should preserve pivot table bytes");
    check(output_reader.read_entry("xl/pivotTables/_rels/pivotTable1.xml.rels")
            == source.pivot_table_relationships,
        "pivot cache definition removal should preserve pivot table relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheRecords1.xml")
            == source.pivot_cache_records,
        "pivot cache definition removal should preserve pivot cache records bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "pivot cache definition removal should preserve unknown bytes");

    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "pivot cache definition removal should keep workbook relationships readable");
    check(workbook_relationships->find_by_id("rIdPivotCache") != nullptr,
        "pivot cache definition removal should keep inbound workbook pivot cache relationship");
    const auto* pivot_table_relationships =
        output_reader.relationships_for(pivot_table_part);
    check(pivot_table_relationships != nullptr,
        "pivot cache definition removal should keep pivot table relationships readable");
    check(pivot_table_relationships->find_by_id("rIdPivotCacheDef") != nullptr,
        "pivot cache definition removal should keep inbound pivot table cache relationship");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(workbook_part)->find_by_id("rIdPivotCache")
            != nullptr,
        "relationship graph should keep workbook cache definition relationship after removal");
    check(output_graph.relationships_for(pivot_table_part)->find_by_id("rIdPivotCacheDef")
            != nullptr,
        "relationship graph should keep pivot table cache definition relationship after removal");
    check(output_reader.relationships_for(pivot_cache_definition_part) == nullptr,
        "pivot cache definition removal should not keep owner relationships for absent part");
}

void test_package_editor_pivot_cache_definition_replacement_restores_prior_removal()
{
    const PivotSourcePackage source =
        write_pivot_source_package(
            "fastxlsx-package-editor-replace-after-remove-pivot-cache-definition-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-pivot-cache-definition-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName pivot_table_part("/xl/pivotTables/pivotTable1.xml");
    const fastxlsx::detail::PartName pivot_cache_definition_part(
        "/xl/pivotCache/pivotCacheDefinition1.xml");
    const fastxlsx::detail::PartName pivot_cache_records_part(
        "/xl/pivotCache/pivotCacheRecords1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");
    const std::string pivot_cache_relationships_entry =
        "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels";

    editor.remove_part(pivot_cache_definition_part,
        "temporary pivot cache definition removal");
    check(editor.edit_plan().find_removed_part(pivot_cache_definition_part) != nullptr,
        "setup should record removed pivot cache definition before replacement restore");
    check(editor.edit_plan().find_removed_package_entry(pivot_cache_relationships_entry)
            != nullptr,
        "setup should omit pivot cache definition owner relationships before restore");
    check(editor.manifest().find_part(pivot_cache_definition_part) == nullptr,
        "setup should remove pivot cache definition from manifest before restore");

    const std::string restored_cache_definition =
        R"(<pivotCacheDefinition xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" refreshOnLoad="1" refreshedVersion="10">)"
        R"(<cacheSource type="worksheet"><worksheetSource ref="A1:B5" sheet="Sheet1"/></cacheSource>)"
        R"(<cacheFields count="1"><cacheField name="RestoredValue" numFmtId="0"><sharedItems containsNumber="1"/></cacheField></cacheFields>)"
        R"(<cacheRecords r:id="rIdPivotRecords"/>)"
        R"(</pivotCacheDefinition>)";
    replace_part_with_memory_chunks(editor, pivot_cache_definition_part, restored_cache_definition,
        "restored pivot cache definition after removal");

    check(editor.edit_plan().find_removed_part(pivot_cache_definition_part) == nullptr,
        "pivot cache definition replacement after removal should clear stale removed-part audit");
    check(editor.edit_plan().find_removed_package_entry(pivot_cache_relationships_entry)
            == nullptr,
        "pivot cache definition replacement after removal should clear stale owner relationships omission");
    const auto* cache_definition_plan =
        editor.edit_plan().find_part(pivot_cache_definition_part);
    check(cache_definition_plan != nullptr,
        "pivot cache definition replacement after removal should restore active edit-plan part");
    check(cache_definition_plan->write_mode
            == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "pivot cache definition replacement after removal should keep final staged write mode");
    check(cache_definition_plan->reason.find("after removal") != std::string::npos,
        "pivot cache definition replacement after removal should keep final replacement reason");
    check_manifest_write_mode(editor, pivot_cache_definition_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "pivot cache definition replacement after removal should restore manifest write mode");

    const auto* cache_relationships_audit =
        editor.edit_plan().find_package_entry(pivot_cache_relationships_entry);
    check(cache_relationships_audit != nullptr,
        "pivot cache definition replacement after removal should restore owner relationships audit");
    check(cache_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition replacement after removal owner relationships should be copy-original");
    check(cache_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "pivot cache definition replacement after removal owner relationships should keep source role");
    check(cache_relationships_audit->owner_part == pivot_cache_definition_part.value(),
        "pivot cache definition replacement after removal owner relationships should keep owner part");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "pivot cache definition replacement after removal should keep content types audit");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition replacement after removal should restore source content types audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition replacement after removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition replacement after removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition replacement after removal should keep pivot table copy-original");
    check(editor.edit_plan().find_part(pivot_cache_records_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition replacement after removal should keep pivot cache records copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition replacement after removal should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "pivot cache definition replacement after removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "pivot cache definition replacement after removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "pivot cache definition replacement after removal output plan should not invent dependency audits");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheDefinition1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "pivot cache definition replacement after removal output plan should rewrite cache definition");
    check_output_entry_part_context(output_plan.entries,
        "xl/pivotCache/pivotCacheDefinition1.xml", true,
        pivot_cache_definition_part.value(),
        "pivot cache definition replacement after removal output plan should classify rewritten cache definition");
    const auto* output_cache_definition_plan =
        find_output_entry_plan(output_plan.entries,
            "xl/pivotCache/pivotCacheDefinition1.xml");
    check(output_cache_definition_plan->reason.find("after removal") != std::string::npos,
        "pivot cache definition replacement after removal output plan should keep replacement reason");
    check_output_entry_plan(output_plan.entries, pivot_cache_relationships_entry,
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition replacement after removal output plan should preserve owner relationships");
    check_output_entry_part_context(output_plan.entries, pivot_cache_relationships_entry,
        false, "",
        "pivot cache definition replacement after removal output plan should classify owner relationships as metadata");
    const auto* output_cache_relationships_plan =
        find_output_entry_plan(output_plan.entries, pivot_cache_relationships_entry);
    check(output_cache_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "pivot cache definition replacement after removal output plan should classify owner relationships metadata");
    check(output_cache_relationships_plan->owner_part == pivot_cache_definition_part.value(),
        "pivot cache definition replacement after removal output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition replacement after removal output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "pivot cache definition replacement after removal output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "pivot cache definition replacement after removal output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition replacement after removal output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition replacement after removal output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition replacement after removal output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition replacement after removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition replacement after removal output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotTables/pivotTable1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition replacement after removal output plan should preserve pivot table");
    check_output_entry_plan(output_plan.entries, "xl/pivotTables/_rels/pivotTable1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition replacement after removal output plan should preserve pivot table relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheRecords1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition replacement after removal output plan should preserve cache records");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition replacement after removal output plan should preserve unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/pivotCache/pivotCacheDefinition1.xml") != entries.end(),
        "pivot cache definition replacement after removal output should restore cache definition entry");
    check(entries.find(pivot_cache_relationships_entry) != entries.end(),
        "pivot cache definition replacement after removal output should restore owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(
        editor.reader(), output_reader, pivot_cache_definition_part.zip_path());
    check(output_reader.read_entry("xl/pivotCache/pivotCacheDefinition1.xml")
            == restored_cache_definition,
        "pivot cache definition replacement after removal should write restored payload");
    check(output_reader.read_entry(pivot_cache_relationships_entry)
            == source.pivot_cache_definition_relationships,
        "pivot cache definition replacement after removal should preserve owner relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "pivot cache definition replacement after removal should preserve content types bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheRecords1.xml")
            == source.pivot_cache_records,
        "pivot cache definition replacement after removal should preserve cache records bytes");
    check(output_reader.read_entry("xl/pivotTables/pivotTable1.xml") == source.pivot_table,
        "pivot cache definition replacement after removal should preserve pivot table bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "pivot cache definition replacement after removal should preserve unknown bytes");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(workbook_part)->find_by_id("rIdPivotCache")
            != nullptr,
        "relationship graph should keep workbook pivot cache relationship after restore");
    check(output_graph.relationships_for(pivot_table_part)->find_by_id("rIdPivotCacheDef")
            != nullptr,
        "relationship graph should keep pivot table cache relationship after restore");
    check(output_graph.relationships_for(pivot_cache_definition_part)
                ->find_by_id("rIdPivotRecords")
            != nullptr,
        "relationship graph should keep cache records relationship after restore");
    check(output_reader.content_types().override_for(pivot_cache_definition_part) != nullptr,
        "pivot cache definition replacement after removal should keep content type override");
}

void test_package_editor_pivot_cache_definition_removal_overrides_prior_replacement()
{
    const PivotSourcePackage source =
        write_pivot_source_package(
            "fastxlsx-package-editor-remove-after-replace-pivot-cache-definition-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-pivot-cache-definition-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName pivot_table_part("/xl/pivotTables/pivotTable1.xml");
    const fastxlsx::detail::PartName pivot_cache_definition_part(
        "/xl/pivotCache/pivotCacheDefinition1.xml");
    const fastxlsx::detail::PartName pivot_cache_records_part(
        "/xl/pivotCache/pivotCacheRecords1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");
    const std::string pivot_cache_relationships_entry =
        "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels";

    const std::string stale_cache_definition =
        R"(<pivotCacheDefinition xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" refreshOnLoad="1" refreshedVersion="11">)"
        R"(<cacheSource type="worksheet"><worksheetSource ref="A1:B6" sheet="Sheet1"/></cacheSource>)"
        R"(<cacheFields count="1"><cacheField name="StaleValue" numFmtId="0"><sharedItems containsNumber="1"/></cacheField></cacheFields>)"
        R"(<cacheRecords r:id="rIdPivotRecords"/>)"
        R"(</pivotCacheDefinition>)";
    replace_part_with_memory_chunks(editor, pivot_cache_definition_part, stale_cache_definition,
        "stale pivot cache definition replacement before removal");
    check(editor.edit_plan().find_part(pivot_cache_definition_part) != nullptr,
        "setup should record active pivot cache definition replacement before removal");
    check(editor.edit_plan().find_package_entry(pivot_cache_relationships_entry)
            != nullptr,
        "setup should preserve owner relationships before final cache definition removal");

    editor.remove_part(pivot_cache_definition_part,
        "final pivot cache definition removal after replacement");

    check(editor.edit_plan().find_part(pivot_cache_definition_part) == nullptr,
        "pivot cache definition removal after replacement should clear active replacement");
    const auto* removed_cache_definition =
        editor.edit_plan().find_removed_part(pivot_cache_definition_part);
    check(removed_cache_definition != nullptr,
        "pivot cache definition removal after replacement should record removed-part audit");
    check(removed_cache_definition->reason.find("after replacement") != std::string::npos,
        "pivot cache definition removal after replacement should keep final removal reason");
    check(removed_cache_definition->inbound_relationships.size() == 2,
        "pivot cache definition removal after replacement should keep workbook and pivot table inbound audits");
    check(editor.edit_plan().find_removed_package_entry(pivot_cache_relationships_entry)
            != nullptr,
        "pivot cache definition removal after replacement should omit owner relationships entry");
    check(editor.edit_plan().find_package_entry(pivot_cache_relationships_entry) == nullptr,
        "pivot cache definition removal after replacement should clear active owner relationships audit");
    check(editor.manifest().find_part(pivot_cache_definition_part) == nullptr,
        "pivot cache definition removal after replacement should remove manifest part");
    check(editor.manifest().content_types().override_for(pivot_cache_definition_part)
            == nullptr,
        "pivot cache definition removal after replacement should remove manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "pivot cache definition removal after replacement should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "pivot cache definition removal after replacement content types rewrite should be local-DOM-rewrite");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition removal after replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition removal after replacement should keep pivot table copy-original");
    check(editor.edit_plan().find_part(pivot_cache_records_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition removal after replacement should keep cache records copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition removal after replacement should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "pivot cache definition removal after replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "pivot cache definition removal after replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "pivot cache definition removal after replacement output plan should not invent dependency audits");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheDefinition1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "pivot cache definition removal after replacement output plan should omit cache definition");
    check_output_entry_part_context(output_plan.entries,
        "xl/pivotCache/pivotCacheDefinition1.xml", true,
        pivot_cache_definition_part.value(),
        "pivot cache definition removal after replacement output plan should classify omitted cache definition");
    const auto* output_cache_definition_plan =
        find_output_entry_plan(output_plan.entries,
            "xl/pivotCache/pivotCacheDefinition1.xml");
    check(output_cache_definition_plan->reason.find("after replacement")
            != std::string::npos,
        "pivot cache definition removal after replacement output plan should keep final removal reason");
    check(output_cache_definition_plan->inbound_relationships.size() == 2,
        "pivot cache definition removal after replacement output plan should expose inbound audits");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "xl/pivotCache/pivotCacheDefinition1.xml", workbook_part.value(),
        "xl/_rels/workbook.xml.rels", "rIdPivotCache",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotCacheDefinition",
        "pivotCache/pivotCacheDefinition1.xml", pivot_cache_definition_part,
        "pivot cache definition removal after replacement output plan should keep workbook inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "xl/pivotCache/pivotCacheDefinition1.xml", pivot_table_part.value(),
        "xl/pivotTables/_rels/pivotTable1.xml.rels", "rIdPivotCacheDef",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotCacheDefinition",
        "../pivotCache/pivotCacheDefinition1.xml", pivot_cache_definition_part,
        "pivot cache definition removal after replacement output plan should keep pivot table inbound audit");
    check_output_entry_plan(output_plan.entries, pivot_cache_relationships_entry,
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "pivot cache definition removal after replacement output plan should omit owner relationships");
    check_output_entry_part_context(output_plan.entries, pivot_cache_relationships_entry,
        false, "",
        "pivot cache definition removal after replacement output plan should classify owner relationships as metadata");
    const auto* output_cache_relationships_plan =
        find_output_entry_plan(output_plan.entries, pivot_cache_relationships_entry);
    check(output_cache_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "pivot cache definition removal after replacement output plan should classify owner relationships metadata");
    check(output_cache_relationships_plan->owner_part
            == pivot_cache_definition_part.value(),
        "pivot cache definition removal after replacement output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "pivot cache definition removal after replacement output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false,
        "",
        "pivot cache definition removal after replacement output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "pivot cache definition removal after replacement output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition removal after replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition removal after replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition removal after replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition removal after replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition removal after replacement output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotTables/pivotTable1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition removal after replacement output plan should preserve pivot table");
    check_output_entry_plan(output_plan.entries, "xl/pivotTables/_rels/pivotTable1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition removal after replacement output plan should preserve pivot table relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheRecords1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition removal after replacement output plan should preserve cache records");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition removal after replacement output plan should preserve unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/pivotCache/pivotCacheDefinition1.xml") == entries.end(),
        "pivot cache definition removal after replacement output should omit cache definition part");
    check(entries.find(pivot_cache_relationships_entry) == entries.end(),
        "pivot cache definition removal after replacement output should omit owner relationships");
    check(entries.find("xl/pivotCache/pivotCacheRecords1.xml") != entries.end(),
        "pivot cache definition removal after replacement output should keep cache records");
    check(entries.find("xl/pivotTables/pivotTable1.xml") != entries.end(),
        "pivot cache definition removal after replacement output should keep pivot table");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(pivot_cache_definition_part) == nullptr,
        "pivot cache definition removal after replacement output should remove content type override");
    check(output_reader.content_types().override_for(pivot_cache_records_part) != nullptr,
        "pivot cache definition removal after replacement output should keep cache records content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/pivotCache/pivotCacheDefinition1.xml",
        "pivot cache definition removal after replacement content types should omit cache definition override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "pivot cache definition removal after replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "pivot cache definition removal after replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "pivot cache definition removal after replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "pivot cache definition removal after replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "pivot cache definition removal after replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/pivotTables/pivotTable1.xml") == source.pivot_table,
        "pivot cache definition removal after replacement should preserve pivot table bytes");
    check(output_reader.read_entry("xl/pivotTables/_rels/pivotTable1.xml.rels")
            == source.pivot_table_relationships,
        "pivot cache definition removal after replacement should preserve pivot table relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheRecords1.xml")
            == source.pivot_cache_records,
        "pivot cache definition removal after replacement should preserve cache records bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "pivot cache definition removal after replacement should preserve unknown bytes");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(workbook_part)->find_by_id("rIdPivotCache")
            != nullptr,
        "relationship graph should keep workbook cache definition relationship after final removal");
    check(output_graph.relationships_for(pivot_table_part)->find_by_id("rIdPivotCacheDef")
            != nullptr,
        "relationship graph should keep pivot table cache definition relationship after final removal");
    check(output_reader.relationships_for(pivot_cache_definition_part) == nullptr,
        "pivot cache definition removal after replacement should not keep owner relationships");
}

void test_package_editor_replaces_pivot_cache_records_and_preserves_cache_definition()
{
    const PivotSourcePackage source =
        write_pivot_source_package(
            "fastxlsx-package-editor-replace-pivot-cache-records-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-pivot-cache-records-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName pivot_table_part("/xl/pivotTables/pivotTable1.xml");
    const fastxlsx::detail::PartName pivot_cache_definition_part(
        "/xl/pivotCache/pivotCacheDefinition1.xml");
    const fastxlsx::detail::PartName pivot_cache_records_part(
        "/xl/pivotCache/pivotCacheRecords1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string replacement_cache_records =
        R"(<pivotCacheRecords xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="2">)"
        R"(<r><n v="64"/></r><r><n v="128"/></r>)"
        R"(</pivotCacheRecords>)";
    replace_part_with_memory_chunks(editor, pivot_cache_records_part,
        replacement_cache_records, "pivot cache records stream rewrite");

    const auto* cache_records_plan =
        editor.edit_plan().find_part(pivot_cache_records_part);
    check(cache_records_plan != nullptr,
        "pivot cache records replacement should be present in the edit plan");
    check(cache_records_plan->write_mode
            == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "pivot cache records replacement should be stream-rewrite");
    check_manifest_write_mode(editor, pivot_cache_records_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "pivot cache records replacement should mirror write mode into manifest");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records replacement should keep pivot table copy-original");
    check(editor.edit_plan().find_part(pivot_cache_definition_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records replacement should keep pivot cache definition copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records replacement should keep unknown part copy-original");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(
        editor.reader(), output_reader, pivot_cache_records_part.zip_path());
    check(output_reader.read_entry("xl/pivotCache/pivotCacheRecords1.xml")
            == replacement_cache_records,
        "pivot cache records replacement should write replacement records XML");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheDefinition1.xml")
            == source.pivot_cache_definition,
        "pivot cache records replacement should preserve pivot cache definition bytes");
    check(output_reader.read_entry("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            == source.pivot_cache_definition_relationships,
        "pivot cache records replacement should preserve cache definition relationships bytes");
    check(output_reader.read_entry("xl/pivotTables/pivotTable1.xml") == source.pivot_table,
        "pivot cache records replacement should preserve pivot table bytes");
    check(output_reader.read_entry("xl/pivotTables/_rels/pivotTable1.xml.rels")
            == source.pivot_table_relationships,
        "pivot cache records replacement should preserve pivot table relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "pivot cache records replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "pivot cache records replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "pivot cache records replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "pivot cache records replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "pivot cache records replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "pivot cache records replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "pivot cache records replacement should preserve unknown bytes");

    const auto* cache_definition_relationships =
        output_reader.relationships_for(pivot_cache_definition_part);
    check(cache_definition_relationships != nullptr,
        "pivot cache records replacement should keep cache definition relationships readable");
    check(cache_definition_relationships->find_by_id("rIdPivotRecords") != nullptr,
        "pivot cache records replacement should keep inbound cache records relationship");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "pivot cache records replacement should keep workbook relationships readable");
    check(workbook_relationships->find_by_id("rIdPivotCache") != nullptr,
        "pivot cache records replacement should keep workbook pivot cache relationship");
    const auto* pivot_table_relationships =
        output_reader.relationships_for(pivot_table_part);
    check(pivot_table_relationships != nullptr,
        "pivot cache records replacement should keep pivot table relationships readable");
    check(pivot_table_relationships->find_by_id("rIdPivotCacheDef") != nullptr,
        "pivot cache records replacement should keep pivot table cache definition relationship");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(pivot_cache_definition_part)
                ->find_by_id("rIdPivotRecords")
            != nullptr,
        "relationship graph should keep cache records relationship after replacement");
    check(output_reader.content_types().override_for(pivot_cache_records_part) != nullptr,
        "pivot cache records replacement should keep pivot cache records content type override");
    check(output_reader.content_types().override_for(pivot_cache_definition_part) != nullptr,
        "pivot cache records replacement should keep pivot cache definition content type override");
}

void test_package_editor_repeated_pivot_cache_records_replacement_updates_final_state()
{
    const PivotSourcePackage source =
        write_pivot_source_package(
            "fastxlsx-package-editor-repeat-pivot-cache-records-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-repeat-pivot-cache-records-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName pivot_table_part("/xl/pivotTables/pivotTable1.xml");
    const fastxlsx::detail::PartName pivot_cache_definition_part(
        "/xl/pivotCache/pivotCacheDefinition1.xml");
    const fastxlsx::detail::PartName pivot_cache_records_part(
        "/xl/pivotCache/pivotCacheRecords1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");
    const std::string pivot_cache_records_relationships_entry =
        "xl/pivotCache/_rels/pivotCacheRecords1.xml.rels";

    const std::string stale_cache_records =
        R"(<pivotCacheRecords xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="2">)"
        R"(<r><n v="128"/></r><r><n v="256"/></r>)"
        R"(</pivotCacheRecords>)";
    const std::string final_cache_records =
        R"(<pivotCacheRecords xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="4">)"
        R"(<r><n v="128"/></r><r><n v="256"/></r><r><n v="512"/></r><r><n v="1024"/></r>)"
        R"(</pivotCacheRecords>)";

    replace_part_with_memory_chunks(editor, pivot_cache_records_part,
        stale_cache_records, "stale repeated pivot cache records stream rewrite");
    replace_part_with_memory_chunks(editor, pivot_cache_records_part,
        final_cache_records, "final repeated pivot cache records stream rewrite");

    const auto* cache_records_plan = editor.edit_plan().find_part(pivot_cache_records_part);
    check(cache_records_plan != nullptr,
        "repeated pivot cache records replacement should keep an active edit-plan part");
    check(cache_records_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated pivot cache records replacement should keep final stream-rewrite mode");
    check(cache_records_plan->reason.find("final repeated") != std::string::npos,
        "repeated pivot cache records replacement should keep final reason");
    check(cache_records_plan->reason.find("stale repeated") == std::string::npos,
        "repeated pivot cache records replacement should drop stale reason");
    check_manifest_write_mode(editor, pivot_cache_records_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated pivot cache records replacement should mirror final write mode into manifest");
    check(editor.manifest().content_types().override_for(pivot_cache_records_part) != nullptr,
        "repeated pivot cache records replacement should keep cache records content type override");
    check(editor.edit_plan().find_removed_part(pivot_cache_records_part) == nullptr,
        "repeated pivot cache records replacement should not leave removed-part audit");
    check(editor.edit_plan().find_removed_package_entry(pivot_cache_records_relationships_entry)
            == nullptr,
        "repeated pivot cache records replacement should not leave owner relationships omission");
    check(editor.edit_plan().find_package_entry(pivot_cache_records_relationships_entry)
            == nullptr,
        "repeated pivot cache records replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "repeated pivot cache records replacement should not rewrite content types audit");
    check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels") == nullptr,
        "repeated pivot cache records replacement should not rewrite workbook relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot cache records replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot cache records replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot cache records replacement should keep pivot table copy-original");
    check(editor.edit_plan().find_part(pivot_cache_definition_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot cache records replacement should keep cache definition copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot cache records replacement should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "repeated pivot cache records replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "repeated pivot cache records replacement output plan should preserve calcChain policy");
    check(output_plan.relationship_target_audits.empty(),
        "repeated pivot cache records replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "repeated pivot cache records replacement output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "repeated pivot cache records replacement output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheRecords1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "repeated pivot cache records replacement output plan should rewrite cache records");
    const auto* output_cache_records_plan =
        find_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheRecords1.xml");
    check(output_cache_records_plan->reason.find("final repeated") != std::string::npos,
        "repeated pivot cache records replacement output plan should keep final reason");
    check(output_cache_records_plan->reason.find("stale repeated") == std::string::npos,
        "repeated pivot cache records replacement output plan should drop stale reason");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache records replacement output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache records replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache records replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache records replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache records replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache records replacement output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotTables/pivotTable1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache records replacement output plan should preserve pivot table");
    check_output_entry_plan(output_plan.entries,
        "xl/pivotTables/_rels/pivotTable1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache records replacement output plan should preserve pivot table relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheDefinition1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache records replacement output plan should preserve cache definition");
    check_output_entry_plan(output_plan.entries,
        "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache records replacement output plan should preserve cache definition relationships");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache records replacement output plan should preserve unknown entry");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, pivot_cache_records_part.zip_path());
    check(output_reader.read_entry("xl/pivotCache/pivotCacheRecords1.xml") == final_cache_records,
        "repeated pivot cache records replacement should write final cache records payload");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheDefinition1.xml")
            == source.pivot_cache_definition,
        "repeated pivot cache records replacement should preserve pivot cache definition bytes");
    check(output_reader.read_entry("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            == source.pivot_cache_definition_relationships,
        "repeated pivot cache records replacement should preserve pivot cache definition relationships bytes");
    check(output_reader.read_entry("xl/pivotTables/pivotTable1.xml") == source.pivot_table,
        "repeated pivot cache records replacement should preserve pivot table bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "repeated pivot cache records replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "repeated pivot cache records replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "repeated pivot cache records replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "repeated pivot cache records replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "repeated pivot cache records replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "repeated pivot cache records replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "repeated pivot cache records replacement should preserve unknown bytes");

    const auto* cache_definition_relationships =
        output_reader.relationships_for(pivot_cache_definition_part);
    check(cache_definition_relationships != nullptr,
        "repeated pivot cache records replacement should keep cache definition relationships readable");
    check(cache_definition_relationships->find_by_id("rIdPivotRecords") != nullptr,
        "repeated pivot cache records replacement should keep inbound cache records relationship");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(pivot_cache_definition_part)
                ->find_by_id("rIdPivotRecords")
            != nullptr,
        "relationship graph should keep cache records relationship after repeated replacement");
}

void test_package_editor_removes_pivot_cache_records_and_preserves_cache_definition()
{
    const PivotSourcePackage source =
        write_pivot_source_package(
            "fastxlsx-package-editor-remove-pivot-cache-records-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-pivot-cache-records-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName pivot_table_part("/xl/pivotTables/pivotTable1.xml");
    const fastxlsx::detail::PartName pivot_cache_definition_part(
        "/xl/pivotCache/pivotCacheDefinition1.xml");
    const fastxlsx::detail::PartName pivot_cache_records_part(
        "/xl/pivotCache/pivotCacheRecords1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    editor.remove_part(pivot_cache_records_part,
        "explicit pivot cache records part removal");

    check(editor.edit_plan().find_part(pivot_cache_records_part) == nullptr,
        "pivot cache records removal should clear the active edit-plan part");
    const auto* removed_cache_records =
        editor.edit_plan().find_removed_part(pivot_cache_records_part);
    check(removed_cache_records != nullptr,
        "pivot cache records removal should record removed-part audit");
    check(removed_cache_records->reason.find("pivot cache records part")
            != std::string::npos,
        "pivot cache records removal should retain the removal reason");
    check(removed_cache_records->reason.find("inbound relationship preserved")
            != std::string::npos,
        "pivot cache records removal should audit preserved inbound relationships");
    check(removed_cache_records->inbound_relationships.size() == 1,
        "pivot cache records removal should keep cache definition inbound audit");
    const auto& inbound = removed_cache_records->inbound_relationships.front();
    check(inbound.owner_part == pivot_cache_definition_part.value(),
        "pivot cache records removal should audit cache definition owner part");
    check(inbound.owner_entry == "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels",
        "pivot cache records removal should audit cache definition owner entry");
    check(inbound.relationship_id == "rIdPivotRecords",
        "pivot cache records removal should audit cache records relationship id");
    check(inbound.relationship_target == "pivotCacheRecords1.xml",
        "pivot cache records removal should audit raw cache records target");
    check(inbound.target_part == pivot_cache_records_part,
        "pivot cache records removal should audit normalized cache records target");
    check(editor.manifest().find_part(pivot_cache_records_part) == nullptr,
        "pivot cache records removal should remove the part from the manifest");
    check(editor.manifest().content_types().override_for(pivot_cache_records_part)
            == nullptr,
        "pivot cache records removal should remove the manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "pivot cache records removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "pivot cache records removal content types rewrite should be local-DOM-rewrite");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records removal should keep pivot table copy-original");
    check(editor.edit_plan().find_part(pivot_cache_definition_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records removal should keep pivot cache definition copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records removal should keep unknown part copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/pivotCache/pivotCacheRecords1.xml") == entries.end(),
        "pivot cache records removal output should omit pivot cache records part");
    check(entries.find("xl/pivotCache/pivotCacheDefinition1.xml") != entries.end(),
        "pivot cache records removal output should keep pivot cache definition");
    check(entries.find("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            != entries.end(),
        "pivot cache records removal output should keep cache definition relationships");
    check(entries.find("xl/pivotTables/pivotTable1.xml") != entries.end(),
        "pivot cache records removal output should keep pivot table");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(pivot_cache_records_part) == nullptr,
        "pivot cache records removal output should remove records content type override");
    check(output_reader.content_types().override_for(pivot_cache_definition_part) != nullptr,
        "pivot cache records removal output should keep cache definition content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/pivotCache/pivotCacheRecords1.xml",
        "pivot cache records removal content types XML should omit records override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "pivot cache records removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "pivot cache records removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "pivot cache records removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "pivot cache records removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "pivot cache records removal should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/pivotTables/pivotTable1.xml") == source.pivot_table,
        "pivot cache records removal should preserve pivot table bytes");
    check(output_reader.read_entry("xl/pivotTables/_rels/pivotTable1.xml.rels")
            == source.pivot_table_relationships,
        "pivot cache records removal should preserve pivot table relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheDefinition1.xml")
            == source.pivot_cache_definition,
        "pivot cache records removal should preserve pivot cache definition bytes");
    check(output_reader.read_entry("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            == source.pivot_cache_definition_relationships,
        "pivot cache records removal should preserve cache definition relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "pivot cache records removal should preserve unknown bytes");

    const auto* cache_definition_relationships =
        output_reader.relationships_for(pivot_cache_definition_part);
    check(cache_definition_relationships != nullptr,
        "pivot cache records removal should keep cache definition relationships readable");
    check(cache_definition_relationships->find_by_id("rIdPivotRecords") != nullptr,
        "pivot cache records removal should keep inbound cache records relationship");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(pivot_cache_definition_part)
                ->find_by_id("rIdPivotRecords")
            != nullptr,
        "relationship graph should keep cache records relationship after records removal");
}

void test_package_editor_pivot_cache_records_replacement_restores_prior_removal()
{
    const PivotSourcePackage source =
        write_pivot_source_package(
            "fastxlsx-package-editor-replace-after-remove-pivot-cache-records-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-pivot-cache-records-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName pivot_table_part("/xl/pivotTables/pivotTable1.xml");
    const fastxlsx::detail::PartName pivot_cache_definition_part(
        "/xl/pivotCache/pivotCacheDefinition1.xml");
    const fastxlsx::detail::PartName pivot_cache_records_part(
        "/xl/pivotCache/pivotCacheRecords1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");
    const std::string pivot_cache_records_relationships_entry =
        "xl/pivotCache/_rels/pivotCacheRecords1.xml.rels";

    editor.remove_part(pivot_cache_records_part, "temporary pivot cache records removal");
    check(editor.edit_plan().find_removed_part(pivot_cache_records_part) != nullptr,
        "setup should record removed pivot cache records before replacement restore");
    check(editor.edit_plan().find_removed_package_entry(
              pivot_cache_records_relationships_entry)
            == nullptr,
        "setup should not invent pivot cache records owner relationships omission");
    check(editor.manifest().find_part(pivot_cache_records_part) == nullptr,
        "setup should remove pivot cache records from manifest before restore");

    const std::string restored_cache_records =
        R"(<pivotCacheRecords xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="3">)"
        R"(<r><n v="256"/></r><r><n v="512"/></r><r><n v="1024"/></r>)"
        R"(</pivotCacheRecords>)";
    replace_part_with_memory_chunks(editor, pivot_cache_records_part,
        restored_cache_records, "restored pivot cache records after removal");

    check(editor.edit_plan().find_removed_part(pivot_cache_records_part) == nullptr,
        "pivot cache records replacement after removal should clear stale removed-part audit");
    check(editor.edit_plan().find_removed_package_entry(
              pivot_cache_records_relationships_entry)
            == nullptr,
        "pivot cache records replacement after removal should not keep owner relationships omission");
    const auto* cache_records_plan =
        editor.edit_plan().find_part(pivot_cache_records_part);
    check(cache_records_plan != nullptr,
        "pivot cache records replacement after removal should restore active edit-plan part");
    check(cache_records_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "pivot cache records replacement after removal should keep final write mode");
    check(cache_records_plan->reason.find("after removal") != std::string::npos,
        "pivot cache records replacement after removal should keep final replacement reason");
    check_manifest_write_mode(editor, pivot_cache_records_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "pivot cache records replacement after removal should restore manifest write mode");
    check(editor.edit_plan().find_package_entry(pivot_cache_records_relationships_entry)
            == nullptr,
        "pivot cache records replacement after removal should not invent owner relationships audit");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "pivot cache records replacement after removal should keep content types audit");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records replacement after removal should restore source content types audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records replacement after removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records replacement after removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records replacement after removal should keep pivot table copy-original");
    check(editor.edit_plan().find_part(pivot_cache_definition_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records replacement after removal should keep cache definition copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records replacement after removal should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "pivot cache records replacement after removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "pivot cache records replacement after removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "pivot cache records replacement after removal output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "pivot cache records replacement after removal output plan should clear stale removed parts");
    check(output_plan.removed_package_entries.empty(),
        "pivot cache records replacement after removal output plan should clear stale omitted entries");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheRecords1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "pivot cache records replacement after removal output plan should rewrite cache records");
    check_output_entry_part_context(output_plan.entries,
        "xl/pivotCache/pivotCacheRecords1.xml", true, pivot_cache_records_part.value(),
        "pivot cache records replacement after removal output plan should classify rewritten cache records");
    const auto* output_cache_records_plan =
        find_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheRecords1.xml");
    check(output_cache_records_plan->reason.find("after removal") != std::string::npos,
        "pivot cache records replacement after removal output plan should keep replacement reason");
    check(find_output_entry_plan(output_plan.entries, pivot_cache_records_relationships_entry)
            == nullptr,
        "pivot cache records replacement after removal output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records replacement after removal output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "pivot cache records replacement after removal output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "pivot cache records replacement after removal output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records replacement after removal output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records replacement after removal output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records replacement after removal output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records replacement after removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records replacement after removal output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotTables/pivotTable1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records replacement after removal output plan should preserve pivot table");
    check_output_entry_plan(output_plan.entries, "xl/pivotTables/_rels/pivotTable1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records replacement after removal output plan should preserve pivot table relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheDefinition1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records replacement after removal output plan should preserve cache definition");
    check_output_entry_plan(output_plan.entries,
        "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records replacement after removal output plan should preserve cache definition relationships");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records replacement after removal output plan should preserve unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/pivotCache/pivotCacheRecords1.xml") != entries.end(),
        "pivot cache records replacement after removal output should restore records entry");
    check(entries.find(pivot_cache_records_relationships_entry) == entries.end(),
        "pivot cache records replacement after removal output should not invent owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(
        editor.reader(), output_reader, pivot_cache_records_part.zip_path());
    check(output_reader.read_entry("xl/pivotCache/pivotCacheRecords1.xml")
            == restored_cache_records,
        "pivot cache records replacement after removal should write restored payload");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "pivot cache records replacement after removal should preserve content types bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheDefinition1.xml")
            == source.pivot_cache_definition,
        "pivot cache records replacement after removal should preserve cache definition bytes");
    check(output_reader.read_entry("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            == source.pivot_cache_definition_relationships,
        "pivot cache records replacement after removal should preserve cache definition relationships bytes");
    check(output_reader.read_entry("xl/pivotTables/pivotTable1.xml") == source.pivot_table,
        "pivot cache records replacement after removal should preserve pivot table bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "pivot cache records replacement after removal should preserve unknown bytes");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(pivot_cache_definition_part)
                ->find_by_id("rIdPivotRecords")
            != nullptr,
        "relationship graph should keep cache records relationship after records restore");
    check(output_reader.content_types().override_for(pivot_cache_records_part) != nullptr,
        "pivot cache records replacement after removal should keep records content type override");
}

void test_package_editor_pivot_cache_records_removal_overrides_prior_replacement()
{
    const PivotSourcePackage source =
        write_pivot_source_package(
            "fastxlsx-package-editor-remove-after-replace-pivot-cache-records-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-pivot-cache-records-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName pivot_table_part("/xl/pivotTables/pivotTable1.xml");
    const fastxlsx::detail::PartName pivot_cache_definition_part(
        "/xl/pivotCache/pivotCacheDefinition1.xml");
    const fastxlsx::detail::PartName pivot_cache_records_part(
        "/xl/pivotCache/pivotCacheRecords1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");
    const std::string pivot_cache_records_relationships_entry =
        "xl/pivotCache/_rels/pivotCacheRecords1.xml.rels";

    const std::string stale_cache_records =
        R"(<pivotCacheRecords xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="2">)"
        R"(<r><n v="2048"/></r><r><n v="4096"/></r>)"
        R"(</pivotCacheRecords>)";
    replace_part_with_memory_chunks(editor, pivot_cache_records_part,
        stale_cache_records, "stale pivot cache records replacement before removal");
    check(editor.edit_plan().find_part(pivot_cache_records_part) != nullptr,
        "setup should record active pivot cache records replacement before removal");
    check(editor.edit_plan().find_package_entry(pivot_cache_records_relationships_entry)
            == nullptr,
        "setup should not invent owner relationships before records removal");

    editor.remove_part(pivot_cache_records_part,
        "final pivot cache records removal after replacement");

    check(editor.edit_plan().find_part(pivot_cache_records_part) == nullptr,
        "pivot cache records removal after replacement should clear active replacement");
    const auto* removed_cache_records =
        editor.edit_plan().find_removed_part(pivot_cache_records_part);
    check(removed_cache_records != nullptr,
        "pivot cache records removal after replacement should record removed-part audit");
    check(removed_cache_records->reason.find("after replacement") != std::string::npos,
        "pivot cache records removal after replacement should keep final removal reason");
    check(removed_cache_records->inbound_relationships.size() == 1,
        "pivot cache records removal after replacement should keep cache definition inbound audit");
    const auto& inbound = removed_cache_records->inbound_relationships.front();
    check(inbound.owner_part == pivot_cache_definition_part.value(),
        "pivot cache records removal after replacement should audit cache definition owner part");
    check(inbound.owner_entry == "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels",
        "pivot cache records removal after replacement should audit cache definition relationships entry");
    check(inbound.relationship_id == "rIdPivotRecords",
        "pivot cache records removal after replacement should audit records relationship id");
    check(inbound.relationship_target == "pivotCacheRecords1.xml",
        "pivot cache records removal after replacement should audit raw records target");
    check(inbound.target_part == pivot_cache_records_part,
        "pivot cache records removal after replacement should audit normalized records target");
    check(editor.edit_plan().find_removed_package_entry(
              pivot_cache_records_relationships_entry)
            == nullptr,
        "pivot cache records removal after replacement should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry(pivot_cache_records_relationships_entry)
            == nullptr,
        "pivot cache records removal after replacement should not keep active owner relationships audit");
    check(editor.manifest().find_part(pivot_cache_records_part) == nullptr,
        "pivot cache records removal after replacement should remove manifest part");
    check(editor.manifest().content_types().override_for(pivot_cache_records_part)
            == nullptr,
        "pivot cache records removal after replacement should remove manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "pivot cache records removal after replacement should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "pivot cache records removal after replacement content types rewrite should be local-DOM-rewrite");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records removal after replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records removal after replacement should keep pivot table copy-original");
    check(editor.edit_plan().find_part(pivot_cache_definition_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records removal after replacement should keep cache definition copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records removal after replacement should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "pivot cache records removal after replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "pivot cache records removal after replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "pivot cache records removal after replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.size() == 1,
        "pivot cache records removal after replacement output plan should expose one removed part");
    check(output_plan.removed_parts.front().part_name == pivot_cache_records_part,
        "pivot cache records removal after replacement output plan should expose removed records");
    check(output_plan.removed_parts.front().reason.find("after replacement")
            != std::string::npos,
        "pivot cache records removal after replacement output plan should keep removed-part reason");
    check(output_plan.removed_parts.front().inbound_relationships.size() == 1,
        "pivot cache records removal after replacement output plan should expose removed-part inbound audit");
    check(output_plan.removed_package_entries.empty(),
        "pivot cache records removal after replacement output plan should not invent owner relationships omission");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheRecords1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "pivot cache records removal after replacement output plan should omit cache records");
    check_output_entry_part_context(output_plan.entries,
        "xl/pivotCache/pivotCacheRecords1.xml", true,
        pivot_cache_records_part.value(),
        "pivot cache records removal after replacement output plan should classify omitted cache records");
    const auto* output_cache_records_plan =
        find_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheRecords1.xml");
    check(output_cache_records_plan->reason.find("after replacement") != std::string::npos,
        "pivot cache records removal after replacement output plan should keep final removal reason");
    check(output_cache_records_plan->inbound_relationships.size() == 1,
        "pivot cache records removal after replacement output plan should expose inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "xl/pivotCache/pivotCacheRecords1.xml", pivot_cache_definition_part.value(),
        "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels", "rIdPivotRecords",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotCacheRecords",
        "pivotCacheRecords1.xml", pivot_cache_records_part,
        "pivot cache records removal after replacement output plan should keep cache definition inbound audit");
    check(find_output_entry_plan(output_plan.entries, pivot_cache_records_relationships_entry)
            == nullptr,
        "pivot cache records removal after replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "pivot cache records removal after replacement output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false,
        "",
        "pivot cache records removal after replacement output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "pivot cache records removal after replacement output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records removal after replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records removal after replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records removal after replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records removal after replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records removal after replacement output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotTables/pivotTable1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records removal after replacement output plan should preserve pivot table");
    check_output_entry_plan(output_plan.entries, "xl/pivotTables/_rels/pivotTable1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records removal after replacement output plan should preserve pivot table relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheDefinition1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records removal after replacement output plan should preserve cache definition");
    check_output_entry_plan(output_plan.entries,
        "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records removal after replacement output plan should preserve cache definition relationships");
    check_output_entry_part_context(output_plan.entries,
        "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels", false,
        "",
        "pivot cache records removal after replacement output plan should keep cache definition relationships as metadata entry");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records removal after replacement output plan should preserve unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/pivotCache/pivotCacheRecords1.xml") == entries.end(),
        "pivot cache records removal after replacement output should omit records part");
    check(entries.find(pivot_cache_records_relationships_entry) == entries.end(),
        "pivot cache records removal after replacement output should not invent owner relationships");
    check(entries.find("xl/pivotCache/pivotCacheDefinition1.xml") != entries.end(),
        "pivot cache records removal after replacement output should keep cache definition");
    check(entries.find("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            != entries.end(),
        "pivot cache records removal after replacement output should keep cache definition relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(pivot_cache_records_part) == nullptr,
        "pivot cache records removal after replacement output should remove records content type override");
    check(output_reader.content_types().override_for(pivot_cache_definition_part) != nullptr,
        "pivot cache records removal after replacement output should keep cache definition content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/pivotCache/pivotCacheRecords1.xml",
        "pivot cache records removal after replacement content types should omit records override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "pivot cache records removal after replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "pivot cache records removal after replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "pivot cache records removal after replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "pivot cache records removal after replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "pivot cache records removal after replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/pivotTables/pivotTable1.xml") == source.pivot_table,
        "pivot cache records removal after replacement should preserve pivot table bytes");
    check(output_reader.read_entry("xl/pivotTables/_rels/pivotTable1.xml.rels")
            == source.pivot_table_relationships,
        "pivot cache records removal after replacement should preserve pivot table relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheDefinition1.xml")
            == source.pivot_cache_definition,
        "pivot cache records removal after replacement should preserve cache definition bytes");
    check(output_reader.read_entry("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            == source.pivot_cache_definition_relationships,
        "pivot cache records removal after replacement should preserve cache definition relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "pivot cache records removal after replacement should preserve unknown bytes");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(pivot_cache_definition_part)
                ->find_by_id("rIdPivotRecords")
            != nullptr,
        "relationship graph should keep cache records relationship after final records removal");
}


} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor preservation shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "preservation-linked-pivot")) {
            test_package_editor_worksheet_rewrite_preserves_pivot_table_cache_chain();
            test_package_editor_replaces_pivot_table_and_preserves_cache_chain();
            test_package_editor_repeated_pivot_table_replacement_updates_final_state();
            test_package_editor_removes_pivot_table_and_preserves_cache_chain();
            test_package_editor_pivot_table_replacement_restores_prior_removal();
            test_package_editor_pivot_table_removal_overrides_prior_replacement();
            test_package_editor_replaces_pivot_cache_definition_and_preserves_cache_records();
            test_package_editor_repeated_pivot_cache_definition_replacement_updates_final_state();
            test_package_editor_removes_pivot_cache_definition_and_preserves_cache_records();
            test_package_editor_pivot_cache_definition_replacement_restores_prior_removal();
            test_package_editor_pivot_cache_definition_removal_overrides_prior_replacement();
            test_package_editor_replaces_pivot_cache_records_and_preserves_cache_definition();
            test_package_editor_repeated_pivot_cache_records_replacement_updates_final_state();
            test_package_editor_removes_pivot_cache_records_and_preserves_cache_definition();
            test_package_editor_pivot_cache_records_replacement_restores_prior_removal();
            test_package_editor_pivot_cache_records_removal_overrides_prior_replacement();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
