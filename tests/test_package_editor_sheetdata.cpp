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
    return shard == "all" || shard == "sheetdata" || shard == "sheetdata-catalog"
        || shard == "sheetdata-guards" || shard == "sheetdata-linked";
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

void test_package_editor_replaces_worksheet_sheet_data_and_preserves_metadata()
{
    const LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package("fastxlsx-package-editor-sheetdata-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-sheetdata-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName vml_drawing_part("/xl/drawings/vmlDrawing1.vml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName vba_part("/xl/vbaProject.bin");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="1"><c r="A1" t="s"><v>0</v></c><c r="B1" s="1"><f>SUM(A1:A1)</f><v>777</v></c></row><row r="2"/></sheetData>)";
    replace_worksheet_sheet_data_from_single_chunk_source(editor, worksheet_part, replacement_sheet_data);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "sheetData replacement should keep worksheet in the edit plan");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "sheetData replacement should plan worksheet as local-DOM-rewrite");
    check(worksheet_plan->reason.find("bounded local sheetData replacement")
            != std::string::npos,
        "sheetData replacement reason should disclose the bounded local rewrite helper");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "sheetData replacement should record workbook metadata rewrite");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "sheetData replacement should plan workbook calc metadata as local-DOM-rewrite");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "sheetData replacement should remove stale calcChain by default");
    check(editor.edit_plan().full_calculation_on_load(),
        "sheetData replacement should request full calculation on load");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Remove,
        "sheetData replacement should keep the default calcChain remove policy");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "sheet property metadata", "caller review"}),
        "sheetData replacement should audit preserved sheet property metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "sheet calculation metadata", "caller review"}),
        "sheetData replacement should audit preserved sheet calculation metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "dimension metadata", "caller review"}),
        "sheetData replacement should audit preserved dimension metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "view metadata", "caller review"}),
        "sheetData replacement should audit preserved sheet view metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "custom view metadata", "caller review"}),
        "sheetData replacement should audit preserved custom view metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "default format metadata", "caller review"}),
        "sheetData replacement should audit preserved default format metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "column metadata", "caller review"}),
        "sheetData replacement should audit preserved column metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "protection metadata", "caller review"}),
        "sheetData replacement should audit preserved sheet protection metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "protected-range metadata", "caller review"}),
        "sheetData replacement should audit preserved protected range metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "sort-state metadata", "caller review"}),
        "sheetData replacement should audit preserved sort-state metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "autoFilter metadata", "caller review"}),
        "sheetData replacement should audit preserved autoFilter metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "merged-cell metadata", "caller review"}),
        "sheetData replacement should audit preserved merged-cell metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "scenario metadata", "caller review"}),
        "sheetData replacement should audit preserved scenario metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "data consolidation metadata", "caller review"}),
        "sheetData replacement should audit preserved data consolidation metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "custom property metadata", "caller review"}),
        "sheetData replacement should audit preserved custom property metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "cell watch metadata", "caller review"}),
        "sheetData replacement should audit preserved cell watch metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "smart tag metadata", "caller review"}),
        "sheetData replacement should audit preserved smart tag metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "web publishing metadata", "caller review"}),
        "sheetData replacement should audit preserved web publishing metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "conditional formatting metadata", "caller review"}),
        "sheetData replacement should audit preserved conditional formatting metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "data validation metadata", "caller review"}),
        "sheetData replacement should audit preserved data validation metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "hyperlink metadata", "caller review"}),
        "sheetData replacement should audit preserved hyperlink metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "ignored-error metadata", "caller review"}),
        "sheetData replacement should audit preserved ignored-error metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "print options metadata", "caller review"}),
        "sheetData replacement should audit preserved print options metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "page margins metadata", "caller review"}),
        "sheetData replacement should audit preserved page margins metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "page setup metadata", "caller review"}),
        "sheetData replacement should audit preserved page setup metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "header/footer metadata", "caller review"}),
        "sheetData replacement should audit preserved header/footer metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "row break metadata", "caller review"}),
        "sheetData replacement should audit preserved row break metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "column break metadata", "caller review"}),
        "sheetData replacement should audit preserved column break metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "phonetic metadata", "caller review"}),
        "sheetData replacement should audit preserved phonetic metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "drawing reference metadata", "caller review"}),
        "sheetData replacement should audit preserved drawing metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "legacy drawing reference metadata", "caller review"}),
        "sheetData replacement should audit preserved legacy drawing metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "background picture reference metadata", "caller review"}),
        "sheetData replacement should audit preserved background picture metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "header/footer drawing reference metadata",
                  "caller review"}),
        "sheetData replacement should audit preserved header/footer drawing metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "OLE object reference metadata", "caller review"}),
        "sheetData replacement should audit preserved OLE object metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "control reference metadata", "caller review"}),
        "sheetData replacement should audit preserved control metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "table reference metadata", "caller review"}),
        "sheetData replacement should audit preserved table metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "extension metadata", "caller review"}),
        "sheetData replacement should audit preserved worksheet extension metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "shared string indexes", "xl/sharedStrings.xml"}),
        "sheetData replacement should audit replacement shared string references");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "style id references", "xl/styles.xml"}),
        "sheetData replacement should audit replacement style id references");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "contains formulas", "calcChain policy"}),
        "sheetData replacement should audit replacement formula references");
    check(has_note_containing(editor.edit_plan().notes(),
              {"bounded local worksheet XML rewrite", "not the large-file streaming"}),
        "sheetData replacement should audit that the current helper is bounded local rewrite");
    using PayloadAuditKind =
        fastxlsx::detail::WorksheetPayloadDependencyAuditKind;
    using PayloadAuditScope =
        fastxlsx::detail::WorksheetPayloadDependencyAuditScope;
    const auto& payload_audits =
        editor.edit_plan().worksheet_payload_dependency_audits();
    struct ExpectedPreservedPayloadAudit {
        PayloadAuditKind kind;
        std::string_view element;
        std::vector<std::string_view> note_needles;
    };
    const auto has_expected_preserved_payload_audit =
        [&](const auto& audits, const ExpectedPreservedPayloadAudit& expected) {
            for (const fastxlsx::detail::WorksheetPayloadDependencyAudit& audit : audits) {
                if (audit.worksheet_part != worksheet_part || audit.kind != expected.kind
                    || audit.scope != PayloadAuditScope::PreservedWorksheetMetadata
                    || audit.element != expected.element) {
                    continue;
                }

                bool matched = true;
                for (std::string_view needle : expected.note_needles) {
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
        };
    const std::vector<ExpectedPreservedPayloadAudit> preserved_metadata_audits = {
        {PayloadAuditKind::RangeMetadata, "sheetPr",
            {"sheet property metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "sheetCalcPr",
            {"sheet calculation metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "dimension",
            {"dimension metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "sheetViews",
            {"view metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "customSheetViews",
            {"custom view metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "sheetFormatPr",
            {"default format metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "cols",
            {"column metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "sheetProtection",
            {"protection metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "protectedRanges",
            {"protected-range metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "sortState",
            {"sort-state metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "autoFilter",
            {"autoFilter metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "mergeCells",
            {"merged-cell metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "scenarios",
            {"scenario metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "dataConsolidate",
            {"data consolidation metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "customProperties",
            {"custom property metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "cellWatches",
            {"cell watch metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "smartTags",
            {"smart tag metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "webPublishItems",
            {"web publishing metadata", "caller review"}},
        {PayloadAuditKind::RelationshipMetadata, "hyperlinks",
            {"hyperlink metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "dataValidations",
            {"data validation metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "conditionalFormatting",
            {"conditional formatting metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "ignoredErrors",
            {"ignored-error metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "printOptions",
            {"print options metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "pageMargins",
            {"page margins metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "pageSetup",
            {"page setup metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "headerFooter",
            {"header/footer metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "rowBreaks",
            {"row break metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "colBreaks",
            {"column break metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "phoneticPr",
            {"phonetic metadata", "caller review"}},
        {PayloadAuditKind::RelationshipMetadata, "drawing",
            {"drawing reference metadata", "caller review"}},
        {PayloadAuditKind::RelationshipMetadata, "legacyDrawing",
            {"legacy drawing reference metadata", "caller review"}},
        {PayloadAuditKind::RelationshipMetadata, "picture",
            {"background picture reference metadata", "caller review"}},
        {PayloadAuditKind::RelationshipMetadata, "legacyDrawingHF",
            {"header/footer drawing reference metadata", "caller review"}},
        {PayloadAuditKind::RelationshipMetadata, "oleObjects",
            {"OLE object reference metadata", "caller review"}},
        {PayloadAuditKind::RelationshipMetadata, "controls",
            {"control reference metadata", "caller review"}},
        {PayloadAuditKind::RelationshipMetadata, "tableParts",
            {"table reference metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "extLst",
            {"extension metadata", "caller review"}},
    };
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::SharedStrings,
              PayloadAuditScope::SheetDataReplacement, "c",
              {"shared string indexes", "xl/sharedStrings.xml"}),
        "sheetData replacement should record structured sharedStrings payload audit");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::Styles,
              PayloadAuditScope::SheetDataReplacement, "c",
              {"style id references", "xl/styles.xml"}),
        "sheetData replacement should record structured styles payload audit");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::Formula,
              PayloadAuditScope::SheetDataReplacement, "f",
              {"formulas", "calcChain policy"}),
        "sheetData replacement should record structured formula payload audit");
    for (const ExpectedPreservedPayloadAudit& expected : preserved_metadata_audits) {
        check(has_expected_preserved_payload_audit(payload_audits, expected),
            "sheetData replacement should record structured preserved worksheet metadata audits");
    }
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "sheetData replacement should mirror worksheet local-DOM-rewrite in the manifest");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "sheetData replacement should mirror workbook metadata rewrite in the manifest");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "sheetData replacement should remove calcChain from the output manifest");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load,
        "sheetData output plan should expose full calculation request");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Remove,
        "sheetData output plan should expose calcChain removal policy");
    check(output_plan.relationship_target_audits.size()
            == editor.edit_plan().relationship_target_audits().size(),
        "sheetData output plan should snapshot structured relationship audits");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == payload_audits.size(),
        "sheetData output plan should mirror structured payload dependency audits");
    check(has_payload_audit(output_plan.worksheet_payload_dependency_audits,
              worksheet_part, PayloadAuditKind::SharedStrings,
              PayloadAuditScope::SheetDataReplacement, "c",
              {"shared string indexes", "xl/sharedStrings.xml"}),
        "sheetData output plan should keep structured sharedStrings payload audit");
    for (const ExpectedPreservedPayloadAudit& expected : preserved_metadata_audits) {
        check(has_expected_preserved_payload_audit(
                  output_plan.worksheet_payload_dependency_audits, expected),
            "sheetData output plan should keep structured preserved worksheet metadata audits");
    }
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "dimension metadata", "caller review"}),
        "sheetData output plan should snapshot preserved metadata notes");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "sheet calculation metadata", "caller review"}),
        "sheetData output plan should snapshot preserved sheet calculation metadata notes");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "column metadata", "caller review"}),
        "sheetData output plan should snapshot preserved column metadata notes");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "sort-state metadata", "caller review"}),
        "sheetData output plan should snapshot preserved sort-state metadata notes");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "page setup metadata", "caller review"}),
        "sheetData output plan should snapshot preserved page setup metadata notes");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "header/footer metadata", "caller review"}),
        "sheetData output plan should snapshot preserved header/footer metadata notes");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "legacy drawing reference metadata",
                  "caller review"}),
        "sheetData output plan should snapshot preserved legacy drawing metadata notes");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "background picture reference metadata",
                  "caller review"}),
        "sheetData output plan should snapshot preserved picture metadata notes");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "shared string indexes", "xl/sharedStrings.xml"}),
        "sheetData output plan should snapshot sharedStrings review notes");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "style id references", "xl/styles.xml"}),
        "sheetData output plan should snapshot styles review notes");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "contains formulas", "calcChain policy"}),
        "sheetData output plan should snapshot formula review notes");
    check(has_note_containing(output_plan.notes,
              {"bounded local worksheet XML rewrite", "not the large-file streaming"}),
        "sheetData output plan should snapshot bounded local rewrite boundary notes");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "sheetData output plan should local-DOM-rewrite worksheet");
    const auto* output_worksheet_entry_plan =
        find_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml");
    check(output_worksheet_entry_plan->reason.find("bounded local sheetData replacement")
            != std::string::npos,
        "sheetData output plan should disclose bounded local worksheet rewrite in the reason");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/sheet1.xml", true,
        worksheet_part.value(),
        "sheetData output plan should classify worksheet as package part");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "sheetData output plan should local-DOM-rewrite workbook metadata");
    check_output_entry_part_context(output_plan.entries, "xl/workbook.xml", true,
        workbook_part.value(),
        "sheetData output plan should classify workbook as package part");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "sheetData output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "sheetData output plan should classify content types as metadata entry");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "sheetData output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "sheetData output plan should rewrite workbook relationships");
    check_output_entry_part_context(output_plan.entries, "xl/_rels/workbook.xml.rels", false,
        "",
        "sheetData output plan should classify workbook relationships as metadata entry");
    const auto* output_workbook_relationships_plan =
        find_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels");
    check(output_workbook_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "sheetData output plan should classify workbook source relationships");
    check(output_workbook_relationships_plan->owner_part == workbook_part.value(),
        "sheetData output plan should keep workbook owner context");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheetData output plan should preserve worksheet relationships");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        false, "",
        "sheetData output plan should classify worksheet relationships as metadata entry");
    const auto* output_worksheet_relationships_plan =
        find_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels");
    check(output_worksheet_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "sheetData output plan should classify worksheet source relationships");
    check(output_worksheet_relationships_plan->owner_part == worksheet_part.value(),
        "sheetData output plan should keep worksheet owner context");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "sheetData output plan should omit stale calcChain");
    check_output_entry_part_context(output_plan.entries, "xl/calcChain.xml", true,
        calc_chain_part.value(),
        "sheetData output plan should keep omitted calcChain as package part");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheetData output plan should preserve drawing");
    check_output_entry_relationship_context(output_plan.entries, "xl/drawings/drawing1.xml",
        worksheet_part.value(), "rId1",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
        "../drawings/drawing1.xml",
        "sheetData output plan should keep drawing relationship audit");
    check_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawing1.vml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheetData output plan should preserve legacy drawing VML");
    check_output_entry_part_context(output_plan.entries, "xl/drawings/vmlDrawing1.vml",
        true, vml_drawing_part.value(),
        "sheetData output plan should classify legacy drawing VML as package part");
    check_output_entry_relationship_context(output_plan.entries, "xl/drawings/vmlDrawing1.vml",
        worksheet_part.value(), "rId7",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing",
        "../drawings/vmlDrawing1.vml#shape1",
        "sheetData output plan should keep legacy drawing relationship audit");
    check_output_entry_plan(output_plan.entries, "xl/media/image1.png",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheetData output plan should preserve image media");
    check_output_entry_relationship_context(output_plan.entries, "xl/media/image1.png",
        drawing_part.value(), "rId1",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image",
        "../media/image1.png",
        "sheetData output plan should keep image relationship audit");
    check_output_entry_plan(output_plan.entries, "xl/tables/table1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheetData output plan should preserve table");
    check_output_entry_relationship_context(output_plan.entries, "xl/tables/table1.xml",
        worksheet_part.value(), "rId3",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/table",
        "../tables/table1.xml",
        "sheetData output plan should keep table relationship audit");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheetData output plan should preserve sharedStrings");
    check_output_entry_relationship_context(output_plan.entries, "xl/sharedStrings.xml", "",
        "", "", "",
        "sheetData output plan should not invent sharedStrings relationship audit");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheetData output plan should preserve styles");
    check_output_entry_relationship_context(output_plan.entries, "xl/styles.xml", "", "",
        "", "",
        "sheetData output plan should not invent styles relationship audit");
    check_output_entry_plan(output_plan.entries, "xl/vbaProject.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheetData output plan should preserve VBA");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheetData output plan should preserve unknown extension");
    check_output_entry_relationship_context(output_plan.entries, "custom/opaque-extension.bin",
        worksheet_part.value(), "rId9",
        "https://fastxlsx.invalid/relationships/opaque-extension",
        "../../custom/opaque-extension.bin",
        "sheetData output plan should keep unknown extension relationship audit");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "sheetData replacement output should omit stale calcChain.xml");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string expected_worksheet =
        std::string(
            R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
            R"(<sheetPr filterMode="1"/>)"
            R"(<sheetCalcPr fullCalcOnLoad="1"/>)"
            R"(<dimension ref="A1:B2"/>)"
            R"(<sheetViews><sheetView workbookViewId="0"/></sheetViews>)"
            R"(<customSheetViews><customSheetView guid="{11111111-2222-3333-4444-555555555555}"/></customSheetViews>)"
            R"(<sheetFormatPr defaultRowHeight="15"/>)"
            R"(<cols><col min="1" max="1" width="12" customWidth="1"/></cols>)")
        + replacement_sheet_data
        + R"(<sheetProtection sheet="1" objects="1" scenarios="1"/>)"
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
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == expected_worksheet,
        "sheetData replacement should preserve worksheet metadata around sheetData");
    check_not_contains(output_reader.read_entry("xl/worksheets/sheet1.xml"), R"(<v>1</v>)",
        "sheetData replacement should remove the old sheetData rows");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "sheetData replacement should preserve worksheet relationships bytes");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "sheetData replacement should keep worksheet relationships readable");
    check(worksheet_relationships->find_by_id("rId1") != nullptr,
        "sheetData replacement should keep drawing relationship readable");
    check(worksheet_relationships->find_by_id("rId2") != nullptr,
        "sheetData replacement should keep hyperlink relationship readable");
    check(worksheet_relationships->find_by_id("rId3") != nullptr,
        "sheetData replacement should keep table relationship readable");
    const auto* legacy_drawing_relationship = worksheet_relationships->find_by_id("rId7");
    check(legacy_drawing_relationship != nullptr,
        "sheetData replacement should keep legacy drawing relationship readable");
    check(legacy_drawing_relationship->target == "../drawings/vmlDrawing1.vml#shape1",
        "sheetData replacement should preserve legacy drawing relationship target");
    check(legacy_drawing_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "sheetData replacement should keep legacy drawing relationship internal");
    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    const auto* graph_worksheet_relationships =
        output_graph.relationships_for(worksheet_part);
    check(graph_worksheet_relationships != nullptr,
        "sheetData replacement should keep worksheet relationships in graph");
    const auto* graph_legacy_drawing_relationship =
        graph_worksheet_relationships->find_by_id("rId7");
    check(graph_legacy_drawing_relationship != nullptr,
        "sheetData replacement graph should keep legacy drawing relationship");
    check(graph_legacy_drawing_relationship->target
            == "../drawings/vmlDrawing1.vml#shape1",
        "sheetData replacement graph should preserve legacy drawing target");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "sheetData replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "sheetData replacement should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/drawings/vmlDrawing1.vml") == source.vml_drawing,
        "sheetData replacement should preserve legacy drawing bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "sheetData replacement should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "sheetData replacement should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "sheetData replacement should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "sheetData replacement should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "sheetData replacement should preserve VBA bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "sheetData replacement should preserve unknown extension bytes");
    check(output_reader.content_types().override_for(calc_chain_part) == nullptr,
        "sheetData replacement should remove calcChain content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "sheetData replacement should preserve table content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "sheetData replacement should preserve sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "sheetData replacement should preserve styles content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "sheetData replacement should preserve VBA content type override");
    check(output_reader.content_types().override_for(opaque_extension_part) == nullptr,
        "sheetData replacement should keep unknown extension on default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "sheetData replacement should not promote media defaults to overrides");

    const std::string workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_not_contains(workbook_relationships, "relationships/calcChain",
        "sheetData replacement should remove calcChain workbook relationship");
    check_contains(workbook_relationships, "relationships/sharedStrings",
        "sheetData replacement should preserve sharedStrings workbook relationship");
    check_contains(workbook_relationships, "relationships/styles",
        "sheetData replacement should preserve styles workbook relationship");
    check_contains(workbook_relationships, "relationships/vbaProject",
        "sheetData replacement should preserve VBA workbook relationship");

    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml,
        R"(<definedNames><definedName name="ReportRange">Sheet1!$A$1:$B$2</definedName></definedNames>)",
        "sheetData replacement should preserve workbook defined names");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "sheetData replacement should request full calculation in workbook XML");
}

void test_package_editor_replaces_worksheet_sheet_data_from_chunk_source()
{
    const LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-sheetdata-chunk-source-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-sheetdata-chunk-source-output.xlsx");
    const std::filesystem::path by_name_output =
        output_path("fastxlsx-package-editor-sheetdata-by-name-chunk-source-output.xlsx");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    const std::string sheet_data_prefix = "<sheetData>";
    const std::string sheet_data_body =
        R"(<row r="6"><c r="A6" t="s"><v>0</v></c><c r="B6" s="1"><f>A6</f></c></row>)";
    const std::string sheet_data_suffix = "</sheetData>";
    const std::string replacement_sheet_data =
        sheet_data_prefix + sheet_data_body + sheet_data_suffix;

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    editor.replace_worksheet_sheet_data_from_chunk_source(worksheet_part,
        make_test_chunk_source({sheet_data_prefix, sheet_data_body, sheet_data_suffix}));

    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "chunk-source replacement/output",
                  "consumed directly", "file-backed staged chunk"}),
        "chunk-source sheetData replacement should expose file-backed handoff");
    const auto& payload_audits = editor.edit_plan().worksheet_payload_dependency_audits();
    using PayloadAuditKind = fastxlsx::detail::WorksheetPayloadDependencyAuditKind;
    using PayloadAuditScope = fastxlsx::detail::WorksheetPayloadDependencyAuditScope;
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::SharedStrings,
              PayloadAuditScope::SheetDataReplacement, "c",
              {"shared string indexes", "xl/sharedStrings.xml"}),
        "chunk-source sheetData replacement should audit shared string references");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::Styles,
              PayloadAuditScope::SheetDataReplacement, "c",
              {"style id references", "xl/styles.xml"}),
        "chunk-source sheetData replacement should audit style references");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::Formula,
              PayloadAuditScope::SheetDataReplacement, "f",
              {"formulas", "calcChain policy"}),
        "chunk-source sheetData replacement should audit formulas");
    const fastxlsx::detail::PackageEditorOutputPlan sheet_data_output_plan =
        editor.planned_output();
    check_output_entry_staged_replacement_chunks(sheet_data_output_plan.entries,
        worksheet_part.zip_path(), true,
        "chunk-source sheetData replacement output plan should expose rewritten worksheet staged chunks");
    check(has_note_containing(sheet_data_output_plan.notes,
              {"validates replacement sheetData root",
                  "replacement payload dependency audit",
                  "without staging or replaying a separate replacement sheetData chunk"}),
        "chunk-source sheetData replacement should expose direct payload insert validation/audit");
    check(has_note_containing(sheet_data_output_plan.notes,
              {"sheetData replacement output writer", "relationship-id audit",
                  "without a separate post-output worksheet validation or audit reread"}),
        "chunk-source sheetData replacement should expose fused output relationship audit");
    check(has_note_containing(sheet_data_output_plan.notes,
              {"sheetData replacement output writer", "preserved worksheet metadata audit",
                  "without a separate preservation-only worksheet reread"}),
        "chunk-source sheetData replacement should expose fused preservation audit");

    editor.save_as(output);
    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_contains(output_reader.read_entry(worksheet_part.zip_path()),
        replacement_sheet_data,
        "chunk-source sheetData replacement output should contain replacement payload");

    fastxlsx::detail::PackageEditor by_name_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    by_name_editor.replace_worksheet_sheet_data_from_chunk_source_by_name("Sheet1",
        make_test_chunk_source({sheet_data_prefix, sheet_data_body, sheet_data_suffix}));
    check(has_note_containing(by_name_editor.edit_plan().notes(),
              {"by-name sheetData chunk-source replacement",
                  "planned/source workbook catalog"}),
        "by-name chunk-source sheetData replacement should expose catalog handoff");
    by_name_editor.save_as(by_name_output);
    const fastxlsx::detail::PackageReader by_name_output_reader =
        fastxlsx::detail::PackageReader::open(by_name_output);
    check_contains(by_name_output_reader.read_entry(worksheet_part.zip_path()),
        replacement_sheet_data,
        "by-name chunk-source sheetData replacement output should contain replacement payload");

    const std::vector<std::string> cdata_sheet_data_chunks {
        R"(<sheetData><row r="7"><c r="A7" t="inlineStr"><is><t><![CDATA[literal > )",
        R"(<c r="Z99" t="s"><f>not-a-formula</f></c>)",
        R"(]]></t></is></c></row>)",
        R"(</sheetData>)",
    };
    fastxlsx::detail::PackageEditor cdata_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    cdata_editor.replace_worksheet_sheet_data_from_chunk_source(
        worksheet_part, make_test_chunk_source(cdata_sheet_data_chunks));
    const auto& cdata_payload_audits =
        cdata_editor.edit_plan().worksheet_payload_dependency_audits();
    check(!has_payload_audit(cdata_payload_audits, worksheet_part,
              PayloadAuditKind::SharedStrings,
              PayloadAuditScope::SheetDataReplacement, "c"),
        "sheetData CDATA text should not be scanned as shared string cell markup");
    check(!has_payload_audit(cdata_payload_audits, worksheet_part,
              PayloadAuditKind::Formula,
              PayloadAuditScope::SheetDataReplacement, "f"),
        "sheetData CDATA text should not be scanned as formula markup");

    std::vector<std::string> long_cdata_sheet_data_chunks {
        R"(<sheetData><row r="8"><c r="A8" t="inlineStr"><is><t><![CDATA[)",
    };
    const std::string cdata_padding_chunk(1024, 'y');
    const std::size_t cdata_padding_chunk_count =
        fastxlsx::detail::package_editor_sheet_data_replacement_payload_byte_limit
        / cdata_padding_chunk.size() - 8U;
    for (std::size_t index = 0; index < cdata_padding_chunk_count; ++index) {
        long_cdata_sheet_data_chunks.push_back(cdata_padding_chunk);
    }
    long_cdata_sheet_data_chunks.push_back(
        R"(<drawing r:id="rIdLongCdataText"/><f>not-a-formula</f>]]>ok</t></is></c></row></sheetData>)");
    fastxlsx::detail::PackageEditor long_cdata_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    long_cdata_editor.replace_worksheet_sheet_data_from_chunk_source(
        worksheet_part, make_test_chunk_source(long_cdata_sheet_data_chunks));
    check(!has_note_containing(long_cdata_editor.edit_plan().notes(),
              {"relationship-id audit", "could not parse"}),
        "long sheetData CDATA text should not trip the relationship scanner retained window");
    check(!has_note_containing(long_cdata_editor.edit_plan().notes(),
              {"rIdLongCdataText"}),
        "long sheetData CDATA text should not be scanned as a relationship reference");

    std::vector<std::string> pi_sheet_data_chunks {
        R"(<sheetData><row r="9"><c r="A9" t="inlineStr"><is><?fastxlsx )",
    };
    const std::string pi_padding_chunk(1024, 'x');
    const std::size_t pi_padding_chunk_count =
        fastxlsx::detail::package_editor_sheet_data_replacement_payload_byte_limit
        / pi_padding_chunk.size() - 8U;
    for (std::size_t index = 0; index < pi_padding_chunk_count; ++index) {
        pi_sheet_data_chunks.push_back(pi_padding_chunk);
    }
    pi_sheet_data_chunks.push_back(
        R"(<drawing r:id="rIdLongPiText"/><c r="Z99" t="s"><f>not-a-formula</f></c> ?><t>ok</t></is></c></row></sheetData>)");
    fastxlsx::detail::PackageEditor pi_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    pi_editor.replace_worksheet_sheet_data_from_chunk_source(
        worksheet_part, make_test_chunk_source(pi_sheet_data_chunks));
    const auto& pi_payload_audits =
        pi_editor.edit_plan().worksheet_payload_dependency_audits();
    check(!has_payload_audit(pi_payload_audits, worksheet_part,
              PayloadAuditKind::SharedStrings,
              PayloadAuditScope::SheetDataReplacement, "c"),
        "sheetData processing instruction text should not be scanned as shared string cell markup");
    check(!has_payload_audit(pi_payload_audits, worksheet_part,
              PayloadAuditKind::Formula,
              PayloadAuditScope::SheetDataReplacement, "f"),
        "sheetData processing instruction text should not be scanned as formula markup");
    check(!has_note_containing(pi_editor.edit_plan().notes(),
              {"relationship-id audit", "could not parse"}),
        "long sheetData processing instruction should not trip the relationship scanner retained window");
    check(!has_note_containing(pi_editor.edit_plan().notes(),
              {"rIdLongPiText"}),
        "long sheetData processing instruction text should not be scanned as a relationship reference");

    fastxlsx::detail::PackageEditor invalid_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const std::size_t initial_plan_size = invalid_editor.edit_plan().size();
    const std::size_t initial_note_count = invalid_editor.edit_plan().notes().size();
    bool invalid_failed = false;
    try {
        invalid_editor.replace_worksheet_sheet_data_from_chunk_source(worksheet_part,
            make_test_chunk_source({"<row/>"}));
    } catch (const std::exception& error) {
        invalid_failed = true;
        check_contains(error.what(), "sheetData",
            "invalid chunk-source sheetData replacement should name sheetData");
        check_not_contains(error.what(), "current worksheet input",
            "invalid replacement sheetData should not be mislabeled as source worksheet input");
    }
    check(invalid_failed,
        "invalid chunk-source sheetData replacement should fail");
    check(invalid_editor.edit_plan().size() == initial_plan_size,
        "invalid chunk-source sheetData replacement should not change edit-plan size");
    check(invalid_editor.edit_plan().notes().size() == initial_note_count,
        "invalid chunk-source sheetData replacement should not add notes");
    check(!invalid_editor.edit_plan().full_calculation_on_load(),
        "invalid chunk-source sheetData replacement should not request recalculation");

    fastxlsx::detail::PackageEditor throwing_sheet_data_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const std::size_t throwing_sheet_data_initial_plan_size =
        throwing_sheet_data_editor.edit_plan().size();
    const std::size_t throwing_sheet_data_initial_note_count =
        throwing_sheet_data_editor.edit_plan().notes().size();
    const std::vector<std::filesystem::path> throwing_sheet_data_temp_files_before =
        package_editor_temp_files();
    int throwing_sheet_data_reads = 0;
    bool throwing_sheet_data_failed = false;
    try {
        throwing_sheet_data_editor.replace_worksheet_sheet_data_from_chunk_source(
            worksheet_part,
            [&](std::string& chunk) {
                ++throwing_sheet_data_reads;
                if (throwing_sheet_data_reads == 1) {
                    chunk = "<sheetData>";
                    return true;
                }
                throw std::runtime_error("caller sheetData stream stopped");
            });
    } catch (const std::exception& error) {
        throwing_sheet_data_failed = true;
        check_contains(error.what(), "failed while reading sheetData replacement XML",
            "throwing sheetData chunk source should name the replacement read boundary");
        check_contains(error.what(), "caller sheetData stream stopped",
            "throwing sheetData chunk source should preserve the caller failure");
        check_not_contains(error.what(), "current worksheet input",
            "throwing replacement sheetData source should not be mislabeled as current worksheet input");
    }
    check(throwing_sheet_data_failed,
        "throwing chunk-source sheetData replacement should fail");
    check(throwing_sheet_data_reads == 2,
        "throwing chunk-source sheetData replacement should stop at the throwing read");
    check(throwing_sheet_data_editor.edit_plan().size()
            == throwing_sheet_data_initial_plan_size,
        "throwing chunk-source sheetData replacement should not change edit-plan size");
    check(throwing_sheet_data_editor.edit_plan().notes().size()
            == throwing_sheet_data_initial_note_count,
        "throwing chunk-source sheetData replacement should not add notes");
    check(!throwing_sheet_data_editor.edit_plan().full_calculation_on_load(),
        "throwing chunk-source sheetData replacement should not request recalculation");
    check_manifest_write_mode(throwing_sheet_data_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "throwing chunk-source sheetData replacement should not change worksheet manifest state");
    check_no_new_package_editor_temp_files(throwing_sheet_data_temp_files_before,
        "throwing chunk-source sheetData replacement should not leak staged temp files");

    const SourcePackage missing_entry_source =
        write_missing_worksheet_entry_source_package(
            "fastxlsx-package-editor-sheetdata-missing-source-entry-source.xlsx");
    fastxlsx::detail::PackageEditor missing_entry_editor =
        fastxlsx::detail::PackageEditor::open(missing_entry_source.path);
    const std::size_t missing_entry_initial_plan_size =
        missing_entry_editor.edit_plan().size();
    const std::size_t missing_entry_initial_note_count =
        missing_entry_editor.edit_plan().notes().size();
    const std::vector<std::filesystem::path> temp_files_before =
        package_editor_temp_files();
    int missing_entry_reads = 0;
    bool missing_entry_failed = false;
    try {
        missing_entry_editor.replace_worksheet_sheet_data_from_chunk_source(worksheet_part,
            [&](std::string& chunk) {
                ++missing_entry_reads;
                chunk = "<sheetData/>";
                return true;
            });
    } catch (const std::exception& error) {
        missing_entry_failed = true;
        check_contains(error.what(), "worksheet sheetData replacement target",
            "missing worksheet-entry sheetData failure should explain the target preflight");
        check_contains(error.what(), "worksheet part '/xl/worksheets/sheet1.xml'",
            "missing worksheet-entry sheetData failure should include the worksheet part");
        check_contains(error.what(), "ZIP entry 'xl/worksheets/sheet1.xml'",
            "missing worksheet-entry sheetData failure should include the worksheet entry");
    }
    check(missing_entry_failed,
        "missing worksheet-entry sheetData replacement should fail");
    check(missing_entry_reads == 0,
        "missing worksheet-entry sheetData replacement should fail before consuming input");
    check(missing_entry_editor.edit_plan().size() == missing_entry_initial_plan_size,
        "missing worksheet-entry sheetData replacement should not change edit-plan size");
    check(missing_entry_editor.edit_plan().notes().size() == missing_entry_initial_note_count,
        "missing worksheet-entry sheetData replacement should not add notes");
    check(!missing_entry_editor.edit_plan().full_calculation_on_load(),
        "missing worksheet-entry sheetData replacement should not request recalculation");
    const auto* missing_entry_manifest_part =
        missing_entry_editor.manifest().find_part(worksheet_part);
    check(missing_entry_manifest_part == nullptr
            || missing_entry_manifest_part->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "missing worksheet-entry sheetData replacement should not change worksheet manifest state");
    check_no_new_package_editor_temp_files(temp_files_before,
        "missing worksheet-entry sheetData replacement should not create staged temp files");
}

void test_package_editor_replaces_worksheet_sheet_data_by_sheet_name()
{
    LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-sheetdata-by-name-source.xlsx");
    source.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<extLst><ext><sheets><sheet name="Decoy Sheet" sheetId="777" r:id="rId1"/></sheets></ext></extLst>)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<definedNames><definedName name="ReportRange">Sheet1!$A$1:$B$2</definedName></definedNames>)"
        R"(</workbook>)";
    rewrite_linked_object_source_package(source);

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-sheetdata-by-name-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="4"><c r="A4"><v>44</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Sheet1", replacement_sheet_data);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "sheet-name sheetData replacement should resolve the worksheet part");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "sheet-name sheetData replacement should plan worksheet local-DOM rewrite");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "sheet-name sheetData replacement should remove stale calcChain by default");
    check(editor.edit_plan().full_calculation_on_load(),
        "sheet-name sheetData replacement should request full calculation");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "sheet-name sheetData output plan should local-DOM-rewrite worksheet");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheet-name sheetData output plan should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "sheet-name sheetData replacement output should omit stale calcChain");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("Sheet1") == worksheet_part,
        "sheet-name sheetData replacement should ignore decoy workbook sheets catalogs");
    bool decoy_lookup_failed = false;
    try {
        (void)output_reader.worksheet_part_by_sheet_name("Decoy Sheet");
    } catch (const std::exception&) {
        decoy_lookup_failed = true;
    }
    check(decoy_lookup_failed,
        "sheet-name sheetData replacement should not expose decoy workbook sheets catalogs");
    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, replacement_sheet_data,
        "sheet-name sheetData replacement should write replacement sheetData");
    check_not_contains(worksheet_xml, R"(<v>1</v>)",
        "sheet-name sheetData replacement should remove old sheetData rows");
    check_contains(output_reader.read_entry("xl/workbook.xml"),
        R"(fullCalcOnLoad="1")",
        "sheet-name sheetData replacement should update workbook calc metadata");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "sheet-name sheetData replacement should preserve unknown extension bytes");
    check(output_reader.content_types().override_for(opaque_extension_part) == nullptr,
        "sheet-name sheetData replacement should keep unknown extension default content type");
}

void test_package_editor_sheet_data_patch_without_calc_chain_keeps_relationship_metadata_copy_original()
{
    SourcePackage source;
    source.path = output_path("fastxlsx-package-editor-sheetdata-no-calcchain-source.xlsx");
    source.content_types = R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(</Types>)";
    source.package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdWorkbook" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    source.workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdSheet" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(</Relationships>)";
    source.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rIdSheet"/></sheets>)"
        R"(</workbook>)";
    source.worksheet =
        R"(<worksheet><dimension ref="A1:A1"/><sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData></worksheet>)";
    source.unknown = std::string("opaque\0bytes", 12);
    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"custom/opaque.bin", source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-sheetdata-no-calcchain-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="2"><c r="A2"><v>2</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Sheet1", replacement_sheet_data);

    check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
        "no-calcChain sheetData patch should not record calcChain removal");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "no-calcChain sheetData patch should not rewrite content types");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "no-calcChain sheetData patch should not rewrite package relationships");
    check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels") != nullptr,
        "no-calcChain sheetData patch should audit preserved workbook relationships");
    check(editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == nullptr,
        "no-calcChain sheetData patch should not invent worksheet relationships audit");
    check(editor.edit_plan().full_calculation_on_load(),
        "no-calcChain sheetData patch should still request workbook recalculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Remove,
        "no-calcChain sheetData patch should keep default calcChain action");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "no-calcChain sheetData patch should local-DOM-rewrite worksheet");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "no-calcChain sheetData patch should local-DOM-rewrite workbook calc metadata");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "no-calcChain sheetData patch should not add calcChain to manifest");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "no-calcChain sheetData output plan should rewrite worksheet");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "no-calcChain sheetData output plan should rewrite workbook calc metadata");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "no-calcChain sheetData output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "no-calcChain sheetData output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "no-calcChain sheetData output plan should preserve workbook relationships");
    check(find_output_entry_plan(
              output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels") == nullptr,
        "no-calcChain sheetData output plan should not create worksheet relationships");
    check(find_output_entry_plan(output_plan.entries, "xl/calcChain.xml") == nullptr,
        "no-calcChain sheetData output plan should not create calcChain output");
    check(output_plan.removed_parts.empty(),
        "no-calcChain sheetData output plan should not omit parts");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "no-calcChain sheetData output should not create calcChain");
    check(entries.find("xl/worksheets/_rels/sheet1.xml.rels") == entries.end(),
        "no-calcChain sheetData output should not create worksheet relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "no-calcChain sheetData output should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "no-calcChain sheetData output should preserve package relationships bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "no-calcChain sheetData output should preserve workbook relationships bytes");
    check(output_reader.relationships_for(worksheet_part) == nullptr,
        "no-calcChain sheetData output should keep worksheet relationships absent");
    check(output_reader.content_types().override_for(calc_chain_part) == nullptr,
        "no-calcChain sheetData output should keep calcChain content type absent");
    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, replacement_sheet_data,
        "no-calcChain sheetData output should write replacement sheetData");
    check_not_contains(worksheet_xml, R"(<v>1</v>)",
        "no-calcChain sheetData output should remove old sheetData rows");
    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "no-calcChain sheetData output should request full calculation");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "no-calcChain sheetData output should preserve unknown bytes");
}

void test_package_editor_replaces_worksheet_sheet_data_by_sheet_name_with_absolute_targets()
{
    LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-sheetdata-by-name-absolute-source.xlsx");
    source.package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="/xl/workbook.xml"/>)"
        R"(</Relationships>)";
    source.workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="/xl/worksheets/sheet1.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/vbaProject" Target="vbaProject.bin"/>)"
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>)"
        R"(<Relationship Id="rId4" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>)"
        R"(<Relationship Id="rId5" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="calcChain.xml"/>)"
        R"(</Relationships>)";
    rewrite_linked_object_source_package(source);

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-sheetdata-by-name-absolute-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="6"><c r="A6"><v>66</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Sheet1", replacement_sheet_data);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "absolute-target sheetData replacement should resolve the worksheet part by name");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "absolute-target sheetData replacement should plan worksheet local-DOM rewrite");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "absolute-target sheetData replacement should remove stale calcChain by default");
    check(editor.edit_plan().full_calculation_on_load(),
        "absolute-target sheetData replacement should request full calculation");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "absolute-target sheetData output plan should local-DOM-rewrite worksheet");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "absolute-target sheetData output plan should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "absolute-target sheetData replacement output should omit stale calcChain");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("Sheet1") == worksheet_part,
        "absolute-target sheetData replacement output should keep sheet-name lookup readable");
    const auto* package_workbook_relationship =
        output_reader.package_relationships().find_by_id("rId1");
    check(package_workbook_relationship != nullptr
            && package_workbook_relationship->target == "/xl/workbook.xml",
        "absolute-target sheetData replacement should preserve absolute package workbook target");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "absolute-target sheetData replacement should keep workbook relationships readable");
    check(workbook_relationships->find_by_id("rId1") != nullptr
            && workbook_relationships->find_by_id("rId1")->target
                == "/xl/worksheets/sheet1.xml",
        "absolute-target sheetData replacement should preserve absolute worksheet target");

    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, replacement_sheet_data,
        "absolute-target sheetData replacement should write replacement sheetData");
    check_not_contains(worksheet_xml, R"(<v>1</v>)",
        "absolute-target sheetData replacement should remove old sheetData rows");
    check_contains(output_reader.read_entry("xl/workbook.xml"),
        R"(fullCalcOnLoad="1")",
        "absolute-target sheetData replacement should update workbook calc metadata");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "absolute-target sheetData replacement should preserve unknown extension bytes");
    check(output_reader.content_types().override_for(opaque_extension_part) == nullptr,
        "absolute-target sheetData replacement should keep unknown extension default content type");
}

void test_package_editor_replaces_worksheet_sheet_data_by_sheet_name_with_dot_segment_targets()
{
    LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-sheetdata-by-name-dot-segments-source.xlsx");
    source.package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/./workbook.xml"/>)"
        R"(</Relationships>)";
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
        output_path("fastxlsx-package-editor-sheetdata-by-name-dot-segments-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    check(editor.reader().worksheet_part_by_sheet_name("Sheet1") == worksheet_part,
        "dot-segment source should resolve the sheet catalog before patching");

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="7"><c r="A7"><v>77</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Sheet1", replacement_sheet_data);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "dot-segment sheetData replacement should resolve the worksheet part by name");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "dot-segment sheetData replacement should plan worksheet local-DOM rewrite");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "dot-segment sheetData replacement should remove stale calcChain by default");
    check(editor.edit_plan().full_calculation_on_load(),
        "dot-segment sheetData replacement should request full calculation");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "dot-segment sheetData output plan should local-DOM-rewrite worksheet");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "dot-segment sheetData output plan should rewrite workbook relationships for calcChain removal");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "dot-segment sheetData output plan should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "dot-segment sheetData replacement output should omit stale calcChain");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("Sheet1") == worksheet_part,
        "dot-segment sheetData replacement output should keep sheet-name lookup readable");
    const auto* package_workbook_relationship =
        output_reader.package_relationships().find_by_id("rId1");
    check(package_workbook_relationship != nullptr
            && package_workbook_relationship->target == "xl/./workbook.xml",
        "dot-segment sheetData replacement should preserve package workbook target bytes");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "dot-segment sheetData replacement should keep workbook relationships readable");
    check(workbook_relationships->find_by_id("rId1") != nullptr
            && workbook_relationships->find_by_id("rId1")->target
                == "./worksheets/../worksheets/sheet1.xml",
        "dot-segment sheetData replacement should preserve worksheet target bytes");
    const std::string output_workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_contains(output_workbook_relationships,
        R"(Target="./worksheets/../worksheets/sheet1.xml")",
        "dot-segment sheetData replacement should preserve worksheet target text");
    check_not_contains(output_workbook_relationships, "relationships/calcChain",
        "dot-segment sheetData replacement should remove stale calcChain relationship");

    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, replacement_sheet_data,
        "dot-segment sheetData replacement should write replacement sheetData");
    check_not_contains(worksheet_xml, R"(<v>1</v>)",
        "dot-segment sheetData replacement should remove old sheetData rows");
    check_contains(output_reader.read_entry("xl/workbook.xml"),
        R"(fullCalcOnLoad="1")",
        "dot-segment sheetData replacement should update workbook calc metadata");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "dot-segment sheetData replacement should preserve unknown extension bytes");
    check(output_reader.content_types().override_for(opaque_extension_part) == nullptr,
        "dot-segment sheetData replacement should keep unknown extension default content type");
}

void test_package_editor_patches_fastxlsx_writer_sheet_data_roundtrip()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-editor-writer-roundtrip-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-writer-roundtrip-output.xlsx");

    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        auto workbook = fastxlsx::WorkbookWriter::create(source_path, options);
        const auto text_style = workbook.add_style(fastxlsx::CellStyle {"@"});
        auto editable = workbook.add_worksheet("Patch Source");
        auto untouched = workbook.add_worksheet("Untouched");

        editable.append_row({
            fastxlsx::CellView::text("old text").with_style(text_style),
            fastxlsx::CellView::number(7.0),
        });
        editable.append_row({
            fastxlsx::CellView::boolean(true),
        });

        untouched.append_row({
            fastxlsx::CellView::text("keep me").with_style(text_style),
            fastxlsx::CellView::number(99.0),
        });

        workbook.close();
    }

    const auto source_entries = fastxlsx::test::read_zip_entries(source_path);
    check(source_entries.find("xl/calcChain.xml") == source_entries.end(),
        "writer source should not contain calcChain");
    check(source_entries.find("xl/sharedStrings.xml") != source_entries.end(),
        "writer source should contain shared strings");
    check(source_entries.find("xl/styles.xml") != source_entries.end(),
        "writer source should contain styles");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source_path);
    const std::vector<fastxlsx::detail::WorkbookSheetReference> source_sheets =
        source_reader.workbook_sheets();
    check(source_sheets.size() == 2,
        "PackageReader should resolve workbook sheets from a FastXLSX writer package");

    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName untouched_part("/xl/worksheets/sheet2.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");

    check(source_reader.worksheet_part_by_sheet_name("Patch Source") == worksheet_part,
        "PackageReader should locate the editable writer sheet by name");
    check(source_reader.worksheet_part_by_sheet_name("Untouched") == untouched_part,
        "PackageReader should locate the untouched writer sheet by name");

    const std::string editable_sheet_before =
        source_reader.read_entry("xl/worksheets/sheet1.xml");
    const std::size_t writer_prolog_end = editable_sheet_before.find("<worksheet");
    check(writer_prolog_end != std::string::npos && writer_prolog_end > 0,
        "writer source worksheet should have an XML declaration/prolog before the root element");
    const std::string writer_prolog = editable_sheet_before.substr(0, writer_prolog_end);
    check_contains(writer_prolog, "<?xml",
        "writer source worksheet prolog should include the XML declaration");
    const std::string untouched_sheet_before =
        source_reader.read_entry("xl/worksheets/sheet2.xml");
    const std::string content_types_before =
        source_reader.read_entry("[Content_Types].xml");
    const std::string package_relationships_before =
        source_reader.read_entry("_rels/.rels");
    const std::string workbook_relationships_before =
        source_reader.read_entry("xl/_rels/workbook.xml.rels");
    const std::string shared_strings_before =
        source_reader.read_entry("xl/sharedStrings.xml");
    const std::string styles_before =
        source_reader.read_entry("xl/styles.xml");
    const std::string core_properties_before =
        source_reader.read_entry("docProps/core.xml");
    const std::string app_properties_before =
        source_reader.read_entry("docProps/app.xml");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source_path);
    const std::string replacement_sheet_data =
        R"(<sheetData><row r="1"><c r="A1" s="1" t="s"><v>0</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Patch Source", replacement_sheet_data);

    using PayloadAuditKind = fastxlsx::detail::WorksheetPayloadDependencyAuditKind;
    using PayloadAuditScope = fastxlsx::detail::WorksheetPayloadDependencyAuditScope;
    using WorkbookAuditKind = fastxlsx::detail::WorkbookPayloadDependencyAuditKind;
    using WorkbookAuditScope = fastxlsx::detail::WorkbookPayloadDependencyAuditScope;

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "writer roundtrip sheetData replacement should resolve the worksheet part");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "writer roundtrip sheetData replacement should plan worksheet local-DOM rewrite");
    check(worksheet_plan->reason.find("bounded local sheetData replacement")
            != std::string::npos,
        "writer roundtrip sheetData replacement should disclose bounded local rewrite");

    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "writer roundtrip sheetData replacement should plan workbook calc metadata rewrite");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "writer roundtrip sheetData replacement should rewrite workbook as small XML");
    check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
        "writer roundtrip sheetData replacement should not invent a removed calcChain payload");
    check(editor.edit_plan().full_calculation_on_load(),
        "writer roundtrip sheetData replacement should request full calculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Remove,
        "writer roundtrip sheetData replacement should keep default calcChain remove policy");
    check(has_note_containing(editor.edit_plan().notes(),
              {"bounded local worksheet XML rewrite", "not the large-file streaming"}),
        "writer roundtrip sheetData replacement should audit bounded local rewrite scope");
    check(has_payload_audit(editor.edit_plan().worksheet_payload_dependency_audits(),
              worksheet_part, PayloadAuditKind::SharedStrings,
              PayloadAuditScope::SheetDataReplacement, "c",
              {"shared string indexes", "xl/sharedStrings.xml"}),
        "writer roundtrip sheetData replacement should audit sharedStrings references");
    check(has_payload_audit(editor.edit_plan().worksheet_payload_dependency_audits(),
              worksheet_part, PayloadAuditKind::Styles,
              PayloadAuditScope::SheetDataReplacement, "c",
              {"style id references", "xl/styles.xml"}),
        "writer roundtrip sheetData replacement should audit style references");
    check(has_workbook_payload_audit(editor.edit_plan().workbook_payload_dependency_audits(),
              workbook_part, WorkbookAuditKind::CalcMetadata,
              WorkbookAuditScope::WorksheetRewrite, "calcPr",
              {"worksheet rewrite", "calcPr"}),
        "writer roundtrip sheetData replacement should audit workbook calc metadata");
    check(has_workbook_payload_audit(editor.edit_plan().workbook_payload_dependency_audits(),
              workbook_part, WorkbookAuditKind::DefinedNames,
              WorkbookAuditScope::WorksheetRewrite, "definedNames",
              {"worksheet rewrite", "definedNames"}),
        "writer roundtrip sheetData replacement should audit workbook definedNames review");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load,
        "writer roundtrip output plan should request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Remove,
        "writer roundtrip output plan should carry calcChain remove policy");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "writer roundtrip output plan should local-DOM-rewrite the target worksheet");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "writer roundtrip output plan should rewrite workbook calc metadata");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet2.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "writer roundtrip output plan should preserve untouched writer worksheet");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "writer roundtrip output plan should keep content types unchanged");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "writer roundtrip output plan should keep package relationships unchanged");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "writer roundtrip output plan should keep workbook relationships unchanged");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "writer roundtrip output plan should preserve sharedStrings");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "writer roundtrip output plan should preserve styles");
    check(find_output_entry_plan(output_plan.entries, "xl/calcChain.xml") == nullptr,
        "writer roundtrip output plan should not create calcChain");
    check(has_payload_audit(output_plan.worksheet_payload_dependency_audits,
              worksheet_part, PayloadAuditKind::SharedStrings,
              PayloadAuditScope::SheetDataReplacement, "c",
              {"shared string indexes", "xl/sharedStrings.xml"}),
        "writer roundtrip output plan should keep sharedStrings payload audit");
    check(has_payload_audit(output_plan.worksheet_payload_dependency_audits,
              worksheet_part, PayloadAuditKind::Styles,
              PayloadAuditScope::SheetDataReplacement, "c",
              {"style id references", "xl/styles.xml"}),
        "writer roundtrip output plan should keep styles payload audit");
    check(has_workbook_payload_audit(output_plan.workbook_payload_dependency_audits,
              workbook_part, WorkbookAuditKind::CalcMetadata,
              WorkbookAuditScope::WorksheetRewrite, "calcPr",
              {"worksheet rewrite", "calcPr"}),
        "writer roundtrip output plan should keep workbook calc metadata audit");
    check(has_workbook_payload_audit(output_plan.workbook_payload_dependency_audits,
              workbook_part, WorkbookAuditKind::DefinedNames,
              WorkbookAuditScope::WorksheetRewrite, "definedNames",
              {"worksheet rewrite", "definedNames"}),
        "writer roundtrip output plan should keep workbook definedNames audit");

    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "writer roundtrip output should not contain calcChain");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("Patch Source") == worksheet_part,
        "writer roundtrip output should keep editable sheet lookup readable");
    check(output_reader.worksheet_part_by_sheet_name("Untouched") == untouched_part,
        "writer roundtrip output should keep untouched sheet lookup readable");

    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check(worksheet_xml.rfind(writer_prolog, 0) == 0,
        "writer roundtrip output should preserve the worksheet XML declaration/prolog");
    check(worksheet_xml.find("<worksheet") == writer_prolog.size(),
        "writer roundtrip output should keep the worksheet root immediately after the prolog");
    check_contains(worksheet_xml, replacement_sheet_data,
        "writer roundtrip output should write replacement sheetData");
    check_not_contains(worksheet_xml, R"(r="B1")",
        "writer roundtrip output should remove old second cell");
    check_not_contains(worksheet_xml, R"(r="A2")",
        "writer roundtrip output should remove old second row");
    check_contains(output_reader.read_entry("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "writer roundtrip output should request full recalculation in workbook XML");

    check_entry_bytes(output_reader, "xl/worksheets/sheet2.xml", untouched_sheet_before);
    check_entry_bytes(output_reader, "[Content_Types].xml", content_types_before);
    check_entry_bytes(output_reader, "_rels/.rels", package_relationships_before);
    check_entry_bytes(
        output_reader, "xl/_rels/workbook.xml.rels", workbook_relationships_before);
    check_entry_bytes(output_reader, "xl/sharedStrings.xml", shared_strings_before);
    check_entry_bytes(output_reader, "xl/styles.xml", styles_before);
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "writer roundtrip output should preserve sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "writer roundtrip output should preserve styles content type override");
    check_contains(output_reader.read_entry("xl/_rels/workbook.xml.rels"),
        "relationships/sharedStrings",
        "writer roundtrip output should preserve sharedStrings workbook relationship");
    check_contains(output_reader.read_entry("xl/_rels/workbook.xml.rels"),
        "relationships/styles",
        "writer roundtrip output should preserve styles workbook relationship");
    check_entry_bytes(output_reader, "docProps/core.xml", core_properties_before);
    check_entry_bytes(output_reader, "docProps/app.xml", app_properties_before);
}

void test_package_editor_controlled_template_fill_fixture_uses_bounded_sheet_data_patch()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-editor-template-fill-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-template-fill-output.xlsx");

    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        auto workbook = fastxlsx::WorkbookWriter::create(source_path, options);
        const auto text_style = workbook.add_style(fastxlsx::CellStyle {"@"});
        auto template_sheet = workbook.add_worksheet("Template Fill");
        auto untouched = workbook.add_worksheet("Untouched");

        template_sheet.append_row({
            fastxlsx::CellView::text("{{customer}}").with_style(text_style),
            fastxlsx::CellView::text("{{total}}").with_style(text_style),
        });
        template_sheet.append_row({
            fastxlsx::CellView::text("{{notes}}").with_style(text_style),
        });
        untouched.append_row({
            fastxlsx::CellView::text("keep me").with_style(text_style),
        });

        workbook.close();
    }

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source_path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName untouched_part("/xl/worksheets/sheet2.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");

    check(source_reader.worksheet_part_by_sheet_name("Template Fill") == worksheet_part,
        "template-fill fixture should locate the editable sheet by name");
    check(source_reader.worksheet_part_by_sheet_name("Untouched") == untouched_part,
        "template-fill fixture should locate the untouched sheet by name");
    check(source_reader.find_entry("xl/calcChain.xml") == nullptr,
        "template-fill source should not contain calcChain");

    const std::string untouched_sheet_before =
        source_reader.read_entry("xl/worksheets/sheet2.xml");
    const std::string content_types_before =
        source_reader.read_entry("[Content_Types].xml");
    const std::string package_relationships_before =
        source_reader.read_entry("_rels/.rels");
    const std::string workbook_relationships_before =
        source_reader.read_entry("xl/_rels/workbook.xml.rels");
    const std::string shared_strings_before =
        source_reader.read_entry("xl/sharedStrings.xml");
    const std::string styles_before =
        source_reader.read_entry("xl/styles.xml");

    check_contains(shared_strings_before, "{{customer}}",
        "template-fill source should keep placeholder text in sharedStrings");
    check_contains(shared_strings_before, "{{notes}}",
        "template-fill source should keep all placeholders in sharedStrings");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source_path);
    const std::string replacement_sheet_data =
        R"(<sheetData><row r="1"><c r="A1" s="1" t="inlineStr"><is><t>Acme Corp</t></is></c><c r="B1"><v>1234</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Template Fill", replacement_sheet_data);

    using PayloadAuditKind = fastxlsx::detail::WorksheetPayloadDependencyAuditKind;
    using PayloadAuditScope = fastxlsx::detail::WorksheetPayloadDependencyAuditScope;

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "template-fill patch should resolve the target worksheet part");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "template-fill patch should use the current bounded local helper");
    check(worksheet_plan->reason.find("bounded local sheetData replacement")
            != std::string::npos,
        "template-fill patch should disclose the bounded local rewrite reason");

    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "template-fill patch should plan workbook calc metadata rewrite");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "template-fill patch should rewrite workbook calc metadata as small XML");
    check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
        "template-fill patch should not invent a removed calcChain payload");
    check(editor.edit_plan().full_calculation_on_load(),
        "template-fill patch should request full calculation");
    check(has_note_containing(editor.edit_plan().notes(),
              {"bounded local worksheet XML rewrite", "not the large-file streaming"}),
        "template-fill patch should audit that it is not the future streaming transformer");
    check(has_payload_audit(editor.edit_plan().worksheet_payload_dependency_audits(),
              worksheet_part, PayloadAuditKind::Styles,
              PayloadAuditScope::SheetDataReplacement, "c",
              {"style id references", "xl/styles.xml"}),
        "template-fill patch should audit style references in replacement cells");
    check(!has_payload_audit(editor.edit_plan().worksheet_payload_dependency_audits(),
              worksheet_part, PayloadAuditKind::SharedStrings,
              PayloadAuditScope::SheetDataReplacement, "c",
              {"shared string indexes", "xl/sharedStrings.xml"}),
        "inline-string template fill should not claim shared string index migration");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load,
        "template-fill output plan should request full calculation");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "template-fill output plan should local-DOM-rewrite the target worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet2.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "template-fill output plan should preserve the untouched worksheet");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "template-fill output plan should preserve sharedStrings bytes");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "template-fill output plan should preserve styles bytes");

    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "template-fill output should not create calcChain");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("Template Fill") == worksheet_part,
        "template-fill output should keep editable sheet lookup readable");
    check(output_reader.worksheet_part_by_sheet_name("Untouched") == untouched_part,
        "template-fill output should keep untouched sheet lookup readable");

    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, replacement_sheet_data,
        "template-fill output should write caller-supplied replacement sheetData");
    check_not_contains(worksheet_xml, R"(r="A2")",
        "template-fill output should remove old placeholder rows from the target sheet");
    check_contains(output_reader.read_entry("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "template-fill output should request workbook recalculation");

    check_entry_bytes(output_reader, "xl/worksheets/sheet2.xml", untouched_sheet_before);
    check_entry_bytes(output_reader, "[Content_Types].xml", content_types_before);
    check_entry_bytes(output_reader, "_rels/.rels", package_relationships_before);
    check_entry_bytes(
        output_reader, "xl/_rels/workbook.xml.rels", workbook_relationships_before);
    check_entry_bytes(output_reader, "xl/sharedStrings.xml", shared_strings_before);
    check_entry_bytes(output_reader, "xl/styles.xml", styles_before);
    check_contains(output_reader.read_entry("xl/sharedStrings.xml"), "{{customer}}",
        "template-fill output should preserve old placeholder sharedStrings instead of pruning");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "template-fill output should preserve sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "template-fill output should preserve styles content type override");
}


void test_package_editor_patches_writer_sheet_data_and_preserves_unknown_entry()
{
    const std::filesystem::path writer_source_path =
        output_path("fastxlsx-package-editor-writer-unknown-base.xlsx");
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-editor-writer-unknown-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-writer-unknown-output.xlsx");
    const std::string unknown_bytes = std::string("writer-opaque\0payload", 21);

    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        auto workbook = fastxlsx::WorkbookWriter::create(writer_source_path, options);
        const auto text_style = workbook.add_style(fastxlsx::CellStyle {"@"});
        auto editable = workbook.add_worksheet("Patch Source");
        auto untouched = workbook.add_worksheet("Untouched");

        editable.append_row({
            fastxlsx::CellView::text("old text").with_style(text_style),
            fastxlsx::CellView::number(7.0),
        });
        untouched.append_row({
            fastxlsx::CellView::text("keep me").with_style(text_style),
            fastxlsx::CellView::number(99.0),
        });

        workbook.close();
    }

    const fastxlsx::detail::PackageReader writer_reader =
        fastxlsx::detail::PackageReader::open(writer_source_path);
    std::string augmented_content_types =
        writer_reader.read_entry("[Content_Types].xml");
    check(augmented_content_types.find(R"(Default Extension="bin")")
            == std::string::npos,
        "writer package fixture should not already contain a bin default");
    const std::size_t content_types_close = augmented_content_types.rfind("</Types>");
    check(content_types_close != std::string::npos,
        "writer package fixture should contain a closing Types element");
    augmented_content_types.insert(content_types_close,
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)");

    std::vector<fastxlsx::detail::PackageEntry> augmented_entries;
    for (const fastxlsx::detail::PackageReaderEntry& entry : writer_reader.entries()) {
        std::string data = writer_reader.read_entry(entry.name);
        if (entry.name == "[Content_Types].xml") {
            data = augmented_content_types;
        }
        augmented_entries.emplace_back(entry.name, std::move(data));
    }
    augmented_entries.emplace_back("custom/opaque.bin", unknown_bytes);
    fastxlsx::detail::write_package(source_path, augmented_entries,
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source_path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName untouched_part("/xl/worksheets/sheet2.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    check(source_reader.worksheet_part_by_sheet_name("Patch Source") == worksheet_part,
        "augmented writer package should locate the editable sheet by name");
    check(source_reader.worksheet_part_by_sheet_name("Untouched") == untouched_part,
        "augmented writer package should locate the untouched sheet by name");
    check(source_reader.find_entry("xl/calcChain.xml") == nullptr,
        "augmented writer package should not contain calcChain");
    check(source_reader.content_types().default_for("bin") != nullptr,
        "augmented writer package should expose unknown bin content type default");
    check(source_reader.content_types().override_for(unknown_part) == nullptr,
        "augmented writer package should not promote unknown bin entry to override");

    const std::string editable_sheet_before =
        source_reader.read_entry("xl/worksheets/sheet1.xml");
    const std::size_t writer_prolog_end = editable_sheet_before.find("<worksheet");
    check(writer_prolog_end != std::string::npos && writer_prolog_end > 0,
        "augmented writer source worksheet should preserve the XML declaration");
    const std::string writer_prolog = editable_sheet_before.substr(0, writer_prolog_end);
    const std::string untouched_sheet_before =
        source_reader.read_entry("xl/worksheets/sheet2.xml");
    const std::string content_types_before =
        source_reader.read_entry("[Content_Types].xml");
    const std::string package_relationships_before =
        source_reader.read_entry("_rels/.rels");
    const std::string workbook_relationships_before =
        source_reader.read_entry("xl/_rels/workbook.xml.rels");
    const std::string shared_strings_before =
        source_reader.read_entry("xl/sharedStrings.xml");
    const std::string styles_before =
        source_reader.read_entry("xl/styles.xml");
    const std::string core_properties_before =
        source_reader.read_entry("docProps/core.xml");
    const std::string app_properties_before =
        source_reader.read_entry("docProps/app.xml");
    check_entry_bytes(source_reader, "custom/opaque.bin", unknown_bytes);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source_path);
    const std::string replacement_sheet_data =
        R"(<sheetData><row r="1"><c r="A1" s="1" t="s"><v>0</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Patch Source", replacement_sheet_data);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr
            && worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "writer unknown sheetData patch should local-DOM-rewrite target worksheet");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr
            && workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "writer unknown sheetData patch should rewrite workbook calc metadata");
    const auto* unknown_plan = editor.edit_plan().find_part(unknown_part);
    check(unknown_plan != nullptr
            && unknown_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "writer unknown sheetData patch should keep unknown part copy-original");
    check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
        "writer unknown sheetData patch should not invent calcChain removal");
    check(editor.edit_plan().full_calculation_on_load(),
        "writer unknown sheetData patch should request full calculation");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "writer unknown output plan should local-DOM-rewrite target worksheet");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "writer unknown output plan should rewrite workbook calc metadata");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet2.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "writer unknown output plan should preserve untouched worksheet");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "writer unknown output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "writer unknown output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "writer unknown output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "writer unknown output plan should preserve sharedStrings");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "writer unknown output plan should preserve styles");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "writer unknown output plan should preserve unknown entry");
    check(find_output_entry_plan(output_plan.entries, "xl/calcChain.xml") == nullptr,
        "writer unknown output plan should not create calcChain");

    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "writer unknown output should not contain calcChain");
    check(output_entries.find("custom/opaque.bin") != output_entries.end(),
        "writer unknown output should preserve the unknown entry");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("Patch Source") == worksheet_part,
        "writer unknown output should keep editable sheet lookup readable");
    check(output_reader.worksheet_part_by_sheet_name("Untouched") == untouched_part,
        "writer unknown output should keep untouched sheet lookup readable");

    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check(worksheet_xml.rfind(writer_prolog, 0) == 0,
        "writer unknown output should preserve the worksheet XML declaration/prolog");
    check_contains(worksheet_xml, replacement_sheet_data,
        "writer unknown output should write replacement sheetData");
    check_not_contains(worksheet_xml, R"(r="B1")",
        "writer unknown output should remove the old target sheetData cells");
    check_contains(output_reader.read_entry("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "writer unknown output should request full recalculation in workbook XML");

    check_entry_bytes(output_reader, "xl/worksheets/sheet2.xml", untouched_sheet_before);
    check_entry_bytes(output_reader, "[Content_Types].xml", content_types_before);
    check_entry_bytes(output_reader, "_rels/.rels", package_relationships_before);
    check_entry_bytes(
        output_reader, "xl/_rels/workbook.xml.rels", workbook_relationships_before);
    check_entry_bytes(output_reader, "xl/sharedStrings.xml", shared_strings_before);
    check_entry_bytes(output_reader, "xl/styles.xml", styles_before);
    check_entry_bytes(output_reader, "docProps/core.xml", core_properties_before);
    check_entry_bytes(output_reader, "docProps/app.xml", app_properties_before);
    check_entry_bytes(output_reader, "custom/opaque.bin", unknown_bytes);
    check(output_reader.content_types().default_for("bin") != nullptr,
        "writer unknown output should preserve unknown bin content type default");
    check(output_reader.content_types().override_for(unknown_part) == nullptr,
        "writer unknown output should not promote unknown bin entry to override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "writer unknown output should preserve sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "writer unknown output should preserve styles content type override");
}

void test_package_editor_replaces_worksheet_by_sheet_name()
{
    LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-worksheet-by-name-source.xlsx");
    source.package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/./workbook.xml"/>)"
        R"(</Relationships>)";
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
        output_path("fastxlsx-package-editor-worksheet-by-name-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string replacement_worksheet =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!-- FastXLSX patch keeps caller-owned worksheet prolog -->\n"
        "<?fastxlsx-patch preserve-prolog?>\n"
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="5"><c r="B5"><v>55</v></c></row></sheetData>)"
        R"(</worksheet>)";
    replace_worksheet_part_by_name_from_single_chunk_source(editor, "Sheet1", replacement_worksheet);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "sheet-name worksheet replacement should resolve the worksheet part");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "sheet-name worksheet replacement should plan worksheet stream rewrite");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "sheet-name worksheet replacement should update workbook calc metadata");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "sheet-name worksheet replacement should plan workbook local-DOM rewrite");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "sheet-name worksheet replacement should remove stale calcChain by default");
    check(editor.edit_plan().full_calculation_on_load(),
        "sheet-name worksheet replacement should request full calculation");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "sheet-name worksheet replacement should remove calcChain from output manifest");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "sheet-name worksheet output plan should stream-rewrite worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheet-name worksheet output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheet-name worksheet output plan should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "sheet-name worksheet replacement output should omit stale calcChain");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("Sheet1") == worksheet_part,
        "sheet-name worksheet replacement should keep dot-segment sheet lookup readable");
    const auto* package_workbook_relationship =
        output_reader.package_relationships().find_by_id("rId1");
    check(package_workbook_relationship != nullptr
            && package_workbook_relationship->target == "xl/./workbook.xml",
        "sheet-name worksheet replacement should preserve dot-segment package workbook target");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "sheet-name worksheet replacement should keep workbook relationships readable");
    check(workbook_relationships->find_by_id("rId1") != nullptr
            && workbook_relationships->find_by_id("rId1")->target
                == "./worksheets/../worksheets/sheet1.xml",
        "sheet-name worksheet replacement should preserve dot-segment worksheet target");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_worksheet,
        "sheet-name worksheet replacement should write the target worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "sheet-name worksheet replacement should preserve worksheet relationships bytes");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "sheet-name worksheet replacement should keep worksheet relationships readable");
    check(worksheet_relationships->find_by_id("rId1") != nullptr,
        "sheet-name worksheet replacement should keep drawing relationship readable");
    check(worksheet_relationships->find_by_id("rId9") != nullptr,
        "sheet-name worksheet replacement should keep unknown extension relationship readable");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "sheet-name worksheet replacement should preserve unknown extension bytes");
    check(output_reader.content_types().override_for(opaque_extension_part) == nullptr,
        "sheet-name worksheet replacement should keep unknown extension default content type");
    check(output_reader.content_types().override_for(calc_chain_part) == nullptr,
        "sheet-name worksheet replacement should remove calcChain content type override");
    const std::string output_workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_contains(output_workbook_relationships,
        R"(Target="./worksheets/../worksheets/sheet1.xml")",
        "sheet-name worksheet replacement should preserve dot-segment worksheet target text");
    check_not_contains(output_workbook_relationships, "relationships/calcChain",
        "sheet-name worksheet replacement should remove calcChain workbook relationship");
    check_contains(output_reader.read_entry("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "sheet-name worksheet replacement should request workbook recalculation");
}

void test_package_editor_replaces_worksheet_by_planned_workbook_sheet_name()
{
    LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-worksheet-by-name-planned-catalog-source.xlsx");
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
        output_path("fastxlsx-package-editor-worksheet-by-name-planned-catalog-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string replacement_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Renamed" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<definedNames><definedName name="ReportRange">Renamed!$A$1:$B$2</definedName></definedNames>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, replacement_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "ordinary workbook replacement before by-name worksheet patch");

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

    bool failed = false;
    try {
        replace_worksheet_part_by_name_from_single_chunk_source(editor,
            "Sheet1", "<worksheet><sheetData/></worksheet>");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "workbook sheet name is not present",
            "planned workbook catalog old-name failure should use planned sheet names");
    }
    check(failed,
        "PackageEditor should reject old source sheet name after planned workbook replacement");

    check(editor.edit_plan().size() == queued_plan_size,
        "planned catalog old-name worksheet failure should preserve queued edit-plan size");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "planned catalog old-name worksheet failure should not append notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == queued_relationship_target_audit_count,
        "planned catalog old-name worksheet failure should not append relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == queued_worksheet_relationship_reference_audit_count,
        "planned catalog old-name worksheet failure should not append worksheet relationship audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == queued_worksheet_payload_dependency_audit_count,
        "planned catalog old-name worksheet failure should not append worksheet payload audits");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "planned catalog old-name worksheet failure should preserve package-entry audit count");
    check(editor.edit_plan().removed_package_entries().size()
            == queued_removed_package_entry_count,
        "planned catalog old-name worksheet failure should preserve removed package-entry audit count");
    check(editor.edit_plan().removed_parts().empty(),
        "planned catalog old-name worksheet failure should not record removed parts");
    check(!editor.edit_plan().full_calculation_on_load(),
        "planned catalog old-name worksheet failure should not request recalculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "planned catalog old-name worksheet failure should not change calcChain policy");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr
            && workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "planned catalog old-name worksheet failure should keep prior workbook replacement");
    check(workbook_plan->reason.find("ordinary workbook replacement before by-name worksheet patch")
            != std::string::npos,
        "planned catalog old-name worksheet failure should keep prior workbook replacement reason");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "planned catalog old-name worksheet failure should keep workbook local-DOM-rewrite");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "planned catalog old-name worksheet failure should leave worksheet copy-original");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "planned catalog old-name worksheet failure should leave calcChain copy-original");

    const std::string replacement_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="8"><c r="A8"><v>88</v></c></row></sheetData>)"
        R"(</worksheet>)";
    replace_worksheet_part_by_name_from_single_chunk_source(editor, "Renamed", replacement_worksheet);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "planned catalog worksheet replacement should resolve the renamed worksheet part");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "planned catalog worksheet replacement should plan worksheet stream rewrite");
    workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr
            && workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "planned catalog worksheet replacement should keep workbook local-DOM rewrite");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "planned catalog worksheet replacement should remove stale calcChain by default");
    check(editor.edit_plan().full_calculation_on_load(),
        "planned catalog worksheet replacement should request full calculation");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "planned catalog worksheet replacement should remove calcChain from output manifest");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "planned catalog worksheet output plan should keep workbook rewrite");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "planned catalog worksheet output plan should stream-rewrite worksheet");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "planned catalog worksheet output plan should omit stale calcChain");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "planned catalog worksheet output plan should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "planned catalog worksheet replacement output should omit stale calcChain");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("Renamed") == worksheet_part,
        "planned catalog worksheet output should expose the renamed sheet catalog");
    bool old_name_failed = false;
    try {
        (void)output_reader.worksheet_part_by_sheet_name("Sheet1");
    } catch (const std::exception&) {
        old_name_failed = true;
    }
    check(old_name_failed,
        "planned catalog worksheet output should not expose the old source sheet name");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_worksheet,
        "planned catalog worksheet replacement should write target worksheet bytes");
    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Renamed")",
        "planned catalog worksheet replacement should preserve planned workbook sheet name");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "planned catalog worksheet replacement should update workbook calc metadata");
    check_not_contains(output_reader.read_entry("[Content_Types].xml"), "calcChain+xml",
        "planned catalog worksheet replacement should remove calcChain content type");
    const std::string output_workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_contains(output_workbook_relationships,
        R"(Target="./worksheets/../worksheets/sheet1.xml")",
        "planned catalog worksheet replacement should preserve dot-segment worksheet target text");
    check_not_contains(output_workbook_relationships, "relationships/calcChain",
        "planned catalog worksheet replacement should remove calcChain relationship");
    check(output_reader.read_entry("custom/opaque-extension.bin") == source.opaque_extension,
        "planned catalog worksheet replacement should preserve unknown extension bytes");
    check(output_reader.content_types().override_for(opaque_extension_part) == nullptr,
        "planned catalog worksheet replacement should keep unknown extension default content type");
}

void test_package_editor_replaces_sheet_data_by_planned_workbook_sheet_name()
{
    LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-sheetdata-by-name-planned-catalog-source.xlsx");
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
        output_path("fastxlsx-package-editor-sheetdata-by-name-planned-catalog-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string replacement_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Renamed" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<definedNames><definedName name="ReportRange">Renamed!$A$1:$B$2</definedName></definedNames>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, replacement_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "ordinary workbook replacement before by-name sheetData patch");

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

    bool failed = false;
    try {
        replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Sheet1", "<sheetData/>");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "workbook sheet name is not present",
            "planned workbook catalog old-name sheetData failure should use planned sheet names");
    }
    check(failed,
        "PackageEditor should reject old source sheet name for planned sheetData patch");

    check(editor.edit_plan().size() == queued_plan_size,
        "planned catalog old-name sheetData failure should preserve queued edit-plan size");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "planned catalog old-name sheetData failure should not append notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == queued_relationship_target_audit_count,
        "planned catalog old-name sheetData failure should not append relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == queued_worksheet_relationship_reference_audit_count,
        "planned catalog old-name sheetData failure should not append worksheet relationship audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == queued_worksheet_payload_dependency_audit_count,
        "planned catalog old-name sheetData failure should not append worksheet payload audits");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "planned catalog old-name sheetData failure should preserve package-entry audit count");
    check(editor.edit_plan().removed_package_entries().size()
            == queued_removed_package_entry_count,
        "planned catalog old-name sheetData failure should preserve removed package-entry audit count");
    check(editor.edit_plan().removed_parts().empty(),
        "planned catalog old-name sheetData failure should not record removed parts");
    check(!editor.edit_plan().full_calculation_on_load(),
        "planned catalog old-name sheetData failure should not request recalculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "planned catalog old-name sheetData failure should not change calcChain policy");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr
            && workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "planned catalog old-name sheetData failure should keep prior workbook replacement");
    check(workbook_plan->reason.find("ordinary workbook replacement before by-name sheetData patch")
            != std::string::npos,
        "planned catalog old-name sheetData failure should keep prior workbook replacement reason");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "planned catalog old-name sheetData failure should keep workbook local-DOM-rewrite");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "planned catalog old-name sheetData failure should leave worksheet copy-original");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "planned catalog old-name sheetData failure should leave calcChain copy-original");

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="9"><c r="A9"><v>99</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Renamed", replacement_sheet_data);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "planned catalog sheetData replacement should resolve the renamed worksheet part");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "planned catalog sheetData replacement should plan worksheet local-DOM rewrite");
    workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr
            && workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "planned catalog sheetData replacement should keep workbook local-DOM rewrite");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "planned catalog sheetData replacement should remove stale calcChain by default");
    check(editor.edit_plan().full_calculation_on_load(),
        "planned catalog sheetData replacement should request full calculation");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "planned catalog sheetData replacement should remove calcChain from output manifest");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "planned catalog sheetData output plan should keep workbook rewrite");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "planned catalog sheetData output plan should local-DOM-rewrite worksheet");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "planned catalog sheetData output plan should omit stale calcChain");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "planned catalog sheetData output plan should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "planned catalog sheetData replacement output should omit stale calcChain");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("Renamed") == worksheet_part,
        "planned catalog sheetData output should expose the renamed sheet catalog");
    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, replacement_sheet_data,
        "planned catalog sheetData replacement should write replacement sheetData");
    check_not_contains(worksheet_xml, R"(<v>1</v>)",
        "planned catalog sheetData replacement should remove old sheetData rows");
    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Renamed")",
        "planned catalog sheetData replacement should preserve planned workbook sheet name");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "planned catalog sheetData replacement should update workbook calc metadata");
    check_not_contains(output_reader.read_entry("[Content_Types].xml"), "calcChain+xml",
        "planned catalog sheetData replacement should remove calcChain content type");
    const std::string output_workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_contains(output_workbook_relationships,
        R"(Target="./worksheets/../worksheets/sheet1.xml")",
        "planned catalog sheetData replacement should preserve dot-segment worksheet target text");
    check_not_contains(output_workbook_relationships, "relationships/calcChain",
        "planned catalog sheetData replacement should remove calcChain relationship");
    check(output_reader.read_entry("custom/opaque-extension.bin") == source.opaque_extension,
        "planned catalog sheetData replacement should preserve unknown extension bytes");
    check(output_reader.content_types().override_for(opaque_extension_part) == nullptr,
        "planned catalog sheetData replacement should keep unknown extension default content type");
}

void test_package_editor_planned_workbook_catalog_respects_relationship_namespace()
{
    LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-planned-catalog-namespace-source.xlsx");
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
        output_path("fastxlsx-package-editor-planned-catalog-namespace-output.xlsx");

    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const std::string replacement_workbook =
        R"(<workbook xmlns:rel="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="AltPlanned" sheetId="1" rel:id="rId1"/></sheets>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, replacement_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "ordinary workbook replacement with alternate relationship namespace prefix");

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="10"><c r="A10"><v>100</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "AltPlanned", replacement_sheet_data);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr
            && worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "alternate-prefix planned catalog should resolve the worksheet part");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr
            && workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "alternate-prefix planned catalog should keep workbook local-DOM rewrite");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "alternate-prefix planned catalog should remove stale calcChain by default");
    check(editor.edit_plan().full_calculation_on_load(),
        "alternate-prefix planned catalog should request full calculation");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "alternate-prefix planned catalog should remove calcChain from output manifest");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "alternate-prefix planned catalog output plan should keep workbook rewrite");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "alternate-prefix planned catalog output plan should local-DOM-rewrite worksheet");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "alternate-prefix planned catalog output plan should omit stale calcChain");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "alternate-prefix planned catalog output plan should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "alternate-prefix planned catalog output should omit stale calcChain");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("AltPlanned") == worksheet_part,
        "alternate-prefix planned catalog output should expose the planned sheet name");
    bool old_name_failed = false;
    try {
        (void)output_reader.worksheet_part_by_sheet_name("Sheet1");
    } catch (const std::exception&) {
        old_name_failed = true;
    }
    check(old_name_failed,
        "alternate-prefix planned catalog output should not expose the old source sheet name");
    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, replacement_sheet_data,
        "alternate-prefix planned catalog should write replacement sheetData");
    check_not_contains(worksheet_xml, R"(<v>1</v>)",
        "alternate-prefix planned catalog should remove old sheetData rows");
    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="AltPlanned")",
        "alternate-prefix planned catalog should preserve planned workbook sheet name");
    check_contains(workbook_xml, R"(rel:id="rId1")",
        "alternate-prefix planned catalog should preserve the namespace-qualified sheet id");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "alternate-prefix planned catalog should update workbook calc metadata");
    check_not_contains(output_reader.read_entry("[Content_Types].xml"), "calcChain+xml",
        "alternate-prefix planned catalog should remove calcChain content type");
    const std::string output_workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_contains(output_workbook_relationships,
        R"(Target="./worksheets/../worksheets/sheet1.xml")",
        "alternate-prefix planned catalog should preserve dot-segment worksheet target text");
    check_not_contains(output_workbook_relationships, "relationships/calcChain",
        "alternate-prefix planned catalog should remove calcChain relationship");
    check(output_reader.read_entry("custom/opaque-extension.bin") == source.opaque_extension,
        "alternate-prefix planned catalog should preserve unknown extension bytes");
    check(output_reader.content_types().override_for(opaque_extension_part) == nullptr,
        "alternate-prefix planned catalog should keep unknown extension default content type");

    const auto expect_invalid_planned_catalog_id =
        [&](const std::string& planned_workbook,
            std::string_view sheet_name,
            std::string_view output_name,
            const char* replacement_reason) {
            fastxlsx::detail::PackageEditor invalid_editor =
                fastxlsx::detail::PackageEditor::open(source.path);
            invalid_editor.replace_part(workbook_part, planned_workbook,
                fastxlsx::detail::PartWriteMode::LocalDomRewrite,
                replacement_reason);

            const std::size_t queued_plan_size = invalid_editor.edit_plan().size();
            const std::size_t queued_note_count =
                invalid_editor.edit_plan().notes().size();
            const std::size_t queued_relationship_target_audit_count =
                invalid_editor.edit_plan().relationship_target_audits().size();
            const std::size_t queued_worksheet_relationship_reference_audit_count =
                invalid_editor.edit_plan().worksheet_relationship_reference_audits().size();
            const std::size_t queued_worksheet_payload_dependency_audit_count =
                invalid_editor.edit_plan().worksheet_payload_dependency_audits().size();
            const std::size_t queued_package_entry_count =
                invalid_editor.edit_plan().package_entries().size();
            const std::size_t queued_removed_package_entry_count =
                invalid_editor.edit_plan().removed_package_entries().size();

            bool failed = false;
            try {
                replace_worksheet_sheet_data_by_name_from_single_chunk_source(invalid_editor,
                    sheet_name, "<sheetData/>");
            } catch (const std::exception& error) {
                failed = true;
                check_contains(error.what(), "missing relationship id",
                    "invalid planned catalog namespace failure should explain missing relationship id");
            }
            check(failed,
                "PackageEditor should reject planned catalog ids outside the relationship namespace");

            check(invalid_editor.edit_plan().size() == queued_plan_size,
                "invalid planned catalog namespace failure should preserve queued edit-plan size");
            check(invalid_editor.edit_plan().notes().size() == queued_note_count,
                "invalid planned catalog namespace failure should not append notes");
            check(invalid_editor.edit_plan().relationship_target_audits().size()
                    == queued_relationship_target_audit_count,
                "invalid planned catalog namespace failure should not append relationship target audits");
            check(invalid_editor.edit_plan().worksheet_relationship_reference_audits().size()
                    == queued_worksheet_relationship_reference_audit_count,
                "invalid planned catalog namespace failure should not append worksheet relationship audits");
            check(invalid_editor.edit_plan().worksheet_payload_dependency_audits().size()
                    == queued_worksheet_payload_dependency_audit_count,
                "invalid planned catalog namespace failure should not append worksheet payload audits");
            check(invalid_editor.edit_plan().package_entries().size()
                    == queued_package_entry_count,
                "invalid planned catalog namespace failure should preserve package-entry audit count");
            check(invalid_editor.edit_plan().removed_package_entries().size()
                    == queued_removed_package_entry_count,
                "invalid planned catalog namespace failure should preserve removed package-entry audit count");
            check(invalid_editor.edit_plan().removed_parts().empty(),
                "invalid planned catalog namespace failure should not record removed parts");
            check(!invalid_editor.edit_plan().full_calculation_on_load(),
                "invalid planned catalog namespace failure should not request recalculation");
            check(invalid_editor.edit_plan().calc_chain_action()
                    == fastxlsx::detail::CalcChainAction::Preserve,
                "invalid planned catalog namespace failure should not change calcChain policy");
            const auto* invalid_workbook_plan =
                invalid_editor.edit_plan().find_part(workbook_part);
            check(invalid_workbook_plan != nullptr
                    && invalid_workbook_plan->write_mode
                        == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
                "invalid planned catalog namespace failure should keep prior workbook replacement");
            check(invalid_workbook_plan->reason.find(replacement_reason)
                    != std::string::npos,
                "invalid planned catalog namespace failure should keep prior workbook replacement reason");
            check_manifest_write_mode(invalid_editor, workbook_part,
                fastxlsx::detail::PartWriteMode::LocalDomRewrite,
                "invalid planned catalog namespace failure should keep workbook local-DOM-rewrite");
            check_manifest_write_mode(invalid_editor, worksheet_part,
                fastxlsx::detail::PartWriteMode::CopyOriginal,
                "invalid planned catalog namespace failure should leave worksheet copy-original");
            check_manifest_write_mode(invalid_editor, calc_chain_part,
                fastxlsx::detail::PartWriteMode::CopyOriginal,
                "invalid planned catalog namespace failure should leave calcChain copy-original");

            const std::filesystem::path invalid_output = output_path(output_name);
            invalid_editor.save_as(invalid_output);

            const fastxlsx::detail::PackageReader invalid_output_reader =
                fastxlsx::detail::PackageReader::open(invalid_output);
            check(invalid_output_reader.read_entry("xl/workbook.xml") == planned_workbook,
                "invalid planned catalog namespace failure output should keep queued workbook replacement");
            check(invalid_output_reader.read_entry("xl/worksheets/sheet1.xml")
                    == source.worksheet,
                "invalid planned catalog namespace failure output should preserve source worksheet bytes");
            check(invalid_output_reader.read_entry("xl/calcChain.xml")
                    == source.calc_chain,
                "invalid planned catalog namespace failure output should preserve calcChain bytes");
            check(invalid_output_reader.read_entry("custom/opaque-extension.bin")
                    == source.opaque_extension,
                "invalid planned catalog namespace failure output should preserve unknown extension bytes");
        };

    const std::string wrong_namespace_workbook =
        R"(<workbook xmlns:x="urn:fastxlsx:not-relationships">)"
        R"(<sheets><sheet name="WrongNs" sheetId="1" x:id="rId1"/></sheets>)"
        R"(</workbook>)";
    expect_invalid_planned_catalog_id(wrong_namespace_workbook, "WrongNs",
        "fastxlsx-package-editor-planned-catalog-wrong-namespace-output.xlsx",
        "wrong-namespace planned workbook catalog before by-name patch");

    const std::string unqualified_id_workbook =
        R"(<workbook><sheets><sheet name="PlainId" sheetId="1" id="rId1"/></sheets></workbook>)";
    expect_invalid_planned_catalog_id(unqualified_id_workbook, "PlainId",
        "fastxlsx-package-editor-planned-catalog-plain-id-output.xlsx",
        "plain-id planned workbook catalog before by-name patch");
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

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor sheetdata shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "sheetdata")) {
        test_package_editor_replaces_worksheet_sheet_data_and_preserves_metadata();
        test_package_editor_replaces_worksheet_sheet_data_from_chunk_source();
        test_package_editor_replaces_worksheet_sheet_data_by_sheet_name();
        test_package_editor_sheet_data_patch_without_calc_chain_keeps_relationship_metadata_copy_original();
        test_package_editor_replaces_worksheet_sheet_data_by_sheet_name_with_absolute_targets();
        test_package_editor_replaces_worksheet_sheet_data_by_sheet_name_with_dot_segment_targets();
        test_package_editor_patches_fastxlsx_writer_sheet_data_roundtrip();
        test_package_editor_controlled_template_fill_fixture_uses_bounded_sheet_data_patch();
        test_package_editor_patches_writer_sheet_data_and_preserves_unknown_entry();
        test_package_editor_replaces_worksheet_by_sheet_name();
        test_package_editor_replaces_worksheet_by_planned_workbook_sheet_name();
        test_package_editor_replaces_sheet_data_by_planned_workbook_sheet_name();
        test_package_editor_planned_workbook_catalog_respects_relationship_namespace();
        }

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
