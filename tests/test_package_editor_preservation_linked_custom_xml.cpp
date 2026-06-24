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
    return shard == "all" || shard == "preservation-linked-custom-xml";
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

void test_package_editor_worksheet_rewrite_preserves_custom_xml_parts()
{
    const CustomXmlSourcePackage source =
        write_custom_xml_source_package("fastxlsx-package-editor-custom-xml-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-custom-xml-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName custom_xml_part("/customXml/item1.xml");
    const fastxlsx::detail::PartName custom_xml_props_part("/customXml/itemProps1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string replacement_sheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>2048</v></c></row></sheetData>)"
        R"(</worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_sheet);

    const auto* custom_xml_plan = editor.edit_plan().find_part(custom_xml_part);
    check(custom_xml_plan != nullptr,
        "custom XML item should remain visible in worksheet rewrite edit plan");
    check(custom_xml_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML item should remain copy-original during worksheet rewrite");
    const auto* custom_xml_props_plan =
        editor.edit_plan().find_part(custom_xml_props_part);
    check(custom_xml_props_plan != nullptr,
        "custom XML properties should remain visible in worksheet rewrite edit plan");
    check(custom_xml_props_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties should remain copy-original during worksheet rewrite");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML worksheet rewrite should keep unrelated unknown part copy-original");

    const auto* custom_xml_relationships_plan =
        editor.edit_plan().find_package_entry("customXml/_rels/item1.xml.rels");
    check(custom_xml_relationships_plan != nullptr,
        "custom XML worksheet rewrite should audit preserved custom XML relationships");
    check(custom_xml_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML relationships should be copy-original in package-entry audit");
    check(custom_xml_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "custom XML relationships audit should keep structured role");
    check(custom_xml_relationships_plan->owner_part == custom_xml_part.value(),
        "custom XML relationships audit should keep owner part");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "custom XML worksheet rewrite should not rewrite content types without calcChain");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "custom XML worksheet rewrite should not rewrite package relationships");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load,
        "custom XML worksheet rewrite output plan should request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Remove,
        "custom XML worksheet rewrite output plan should expose calcChain removal policy");
    check(output_plan.relationship_target_audits.empty(),
        "custom XML worksheet rewrite output plan should not invent dependency audits");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "custom XML worksheet rewrite output plan should stream-rewrite worksheet");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/sheet1.xml",
        true, worksheet_part.value(),
        "custom XML worksheet rewrite output plan should classify worksheet as a part");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "custom XML worksheet rewrite output plan should update workbook metadata");
    check_output_entry_part_context(output_plan.entries, "xl/workbook.xml", true,
        "/xl/workbook.xml",
        "custom XML worksheet rewrite output plan should classify workbook as a part");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML worksheet rewrite output plan should preserve package relationships");
    check_output_entry_part_context(output_plan.entries, "_rels/.rels", false, "",
        "custom XML worksheet rewrite output plan should classify package relationships");
    check_output_entry_plan(output_plan.entries, "customXml/item1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML worksheet rewrite output plan should preserve custom XML item");
    check_output_entry_part_context(output_plan.entries, "customXml/item1.xml",
        true, custom_xml_part.value(),
        "custom XML worksheet rewrite output plan should classify custom XML item");
    check_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML worksheet rewrite output plan should preserve custom XML relationships");
    check_output_entry_part_context(output_plan.entries, "customXml/_rels/item1.xml.rels",
        false, "",
        "custom XML worksheet rewrite output plan should classify custom XML relationships");
    const auto* output_custom_xml_relationships_plan =
        find_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels");
    check(output_custom_xml_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "custom XML worksheet rewrite output plan should keep owner relationship role");
    check(output_custom_xml_relationships_plan->owner_part == custom_xml_part.value(),
        "custom XML worksheet rewrite output plan should keep custom XML owner context");
    check_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML worksheet rewrite output plan should preserve custom XML properties");
    check_output_entry_part_context(output_plan.entries, "customXml/itemProps1.xml",
        true, custom_xml_props_part.value(),
        "custom XML worksheet rewrite output plan should classify custom XML properties");
    check(find_output_entry_plan(output_plan.entries, "customXml/_rels/itemProps1.xml.rels")
            == nullptr,
        "custom XML worksheet rewrite output plan should not invent properties relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML worksheet rewrite output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "custom XML worksheet rewrite output plan should classify content types");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML worksheet rewrite output plan should preserve unknown entry");
    check_output_entry_part_context(output_plan.entries, "custom/opaque.bin", true,
        unknown_part.value(),
        "custom XML worksheet rewrite output plan should classify unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("customXml/item1.xml") != entries.end(),
        "custom XML worksheet rewrite output should keep custom XML item");
    check(entries.find("customXml/_rels/item1.xml.rels") != entries.end(),
        "custom XML worksheet rewrite output should keep custom XML relationships");
    check(entries.find("customXml/itemProps1.xml") != entries.end(),
        "custom XML worksheet rewrite output should keep custom XML properties");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_sheet,
        "custom XML worksheet rewrite should write replacement worksheet XML");
    check(output_reader.read_entry("customXml/item1.xml") == source.custom_xml,
        "custom XML worksheet rewrite should preserve custom XML item bytes");
    check(output_reader.read_entry("customXml/_rels/item1.xml.rels")
            == source.custom_xml_relationships,
        "custom XML worksheet rewrite should preserve custom XML relationships bytes");
    check(output_reader.read_entry("customXml/itemProps1.xml")
            == source.custom_xml_properties,
        "custom XML worksheet rewrite should preserve custom XML properties bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "custom XML worksheet rewrite should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "custom XML worksheet rewrite should preserve package relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "custom XML worksheet rewrite should preserve unknown bytes");

    const auto* package_custom_xml_relationship =
        output_reader.package_relationships().find_by_id("rIdCustomXml");
    check(package_custom_xml_relationship != nullptr,
        "custom XML worksheet rewrite should keep package customXml relationship");
    check(package_custom_xml_relationship->target == "customXml/item1.xml",
        "custom XML worksheet rewrite should not rewrite customXml package target");
    check(package_custom_xml_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "custom XML worksheet rewrite should keep customXml relationship internal");

    const auto* custom_xml_relationships =
        output_reader.relationships_for(custom_xml_part);
    check(custom_xml_relationships != nullptr,
        "custom XML worksheet rewrite should keep custom XML relationships readable");
    const auto* custom_xml_props_relationship =
        custom_xml_relationships->find_by_id("rIdCustomXmlProps");
    check(custom_xml_props_relationship != nullptr,
        "custom XML worksheet rewrite should keep customXmlProps relationship id");
    check(custom_xml_props_relationship->target == "itemProps1.xml",
        "custom XML worksheet rewrite should not rewrite customXmlProps target");
    check(output_reader.relationships_for(custom_xml_props_part) == nullptr,
        "custom XML worksheet rewrite should not invent custom XML properties relationships");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.package_relationships().find_by_id("rIdCustomXml") != nullptr,
        "relationship graph should keep package customXml relationship");
    check(output_graph.relationships_for(custom_xml_part)->find_by_id("rIdCustomXmlProps")
            != nullptr,
        "relationship graph should keep customXmlProps relationship");
    const auto* custom_xml_content_type =
        output_reader.content_types().content_type_for(custom_xml_part);
    check(custom_xml_content_type != nullptr && *custom_xml_content_type == "application/xml",
        "custom XML worksheet rewrite should preserve default XML content type for item");
    check(output_reader.content_types().override_for(custom_xml_props_part) != nullptr,
        "custom XML worksheet rewrite should preserve custom XML properties content type override");
}

void test_package_editor_replaces_custom_xml_and_preserves_package_links()
{
    const CustomXmlSourcePackage source =
        write_custom_xml_source_package("fastxlsx-package-editor-linked-custom-xml-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-linked-custom-xml-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName custom_xml_part("/customXml/item1.xml");
    const fastxlsx::detail::PartName custom_xml_props_part("/customXml/itemProps1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string replacement_custom_xml =
        R"(<fx:payload xmlns:fx="urn:fastxlsx:test-custom-xml">)"
        R"(<fx:value>Replacement custom XML payload</fx:value>)"
        R"(</fx:payload>)";
    replace_part_with_memory_chunks(editor, custom_xml_part, replacement_custom_xml,
        "custom XML item replacement");

    const auto* custom_xml_plan = editor.edit_plan().find_part(custom_xml_part);
    check(custom_xml_plan != nullptr,
        "custom XML replacement should keep target part in edit plan");
    check(custom_xml_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "custom XML replacement should record final write mode");
    check(custom_xml_plan->reason.find("custom XML") != std::string::npos,
        "custom XML replacement should keep readable replacement reason");
    check_manifest_write_mode(editor, custom_xml_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "custom XML replacement should update manifest write mode");
    const auto* custom_xml_relationships_audit =
        editor.edit_plan().find_package_entry("customXml/_rels/item1.xml.rels");
    check(custom_xml_relationships_audit != nullptr,
        "custom XML replacement should audit preserved owner relationships");
    check(custom_xml_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML owner relationships audit should be copy-original");
    check(custom_xml_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "custom XML owner relationships audit should keep source relationship role");
    check(custom_xml_relationships_audit->owner_part == custom_xml_part.value(),
        "custom XML owner relationships audit should keep owner part");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "custom XML replacement should not rewrite content types for default XML part");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "custom XML replacement should not rewrite package relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(custom_xml_props_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML replacement should keep properties part copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML replacement should keep unrelated unknown part copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "custom XML replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "custom XML replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "custom XML replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "custom XML replacement output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "custom XML replacement output plan should not expose removed package entries");
    check_output_entry_plan(output_plan.entries, "customXml/item1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "custom XML replacement output plan should rewrite custom XML item");
    check_output_entry_part_context(output_plan.entries, "customXml/item1.xml",
        true, custom_xml_part.value(),
        "custom XML replacement output plan should classify rewritten custom XML item");
    const auto* output_custom_xml_plan =
        find_output_entry_plan(output_plan.entries, "customXml/item1.xml");
    check(output_custom_xml_plan->reason.find("custom XML") != std::string::npos,
        "custom XML replacement output plan should keep replacement reason");
    check_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement output plan should preserve owner relationships");
    check_output_entry_part_context(output_plan.entries, "customXml/_rels/item1.xml.rels",
        false, "",
        "custom XML replacement output plan should classify owner relationships as metadata");
    const auto* output_custom_xml_relationships_plan =
        find_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels");
    check(output_custom_xml_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "custom XML replacement output plan should classify owner relationships metadata");
    check(output_custom_xml_relationships_plan->owner_part == custom_xml_part.value(),
        "custom XML replacement output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "custom XML replacement output plan should classify content types as metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement output plan should preserve properties part");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement output plan should preserve unknown entry");
    check(find_output_entry_plan(output_plan.entries, "customXml/_rels/itemProps1.xml.rels")
            == nullptr,
        "custom XML replacement output plan should not invent properties relationships");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(
        editor.reader(), output_reader, custom_xml_part.zip_path());
    check(output_reader.read_entry("customXml/item1.xml") == replacement_custom_xml,
        "custom XML replacement output should write replacement payload");
    check(output_reader.read_entry("customXml/_rels/item1.xml.rels")
            == source.custom_xml_relationships,
        "custom XML replacement should preserve owner relationships bytes");
    check(output_reader.read_entry("customXml/itemProps1.xml")
            == source.custom_xml_properties,
        "custom XML replacement should preserve properties bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "custom XML replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "custom XML replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "custom XML replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "custom XML replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "custom XML replacement should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "custom XML replacement should preserve unknown bytes");

    const auto* package_custom_xml_relationship =
        output_reader.package_relationships().find_by_id("rIdCustomXml");
    check(package_custom_xml_relationship != nullptr,
        "custom XML replacement should keep package customXml relationship");
    check(package_custom_xml_relationship->target == "customXml/item1.xml",
        "custom XML replacement should not rewrite package customXml target");
    check(package_custom_xml_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "custom XML replacement should keep package customXml target mode");

    const auto* custom_xml_relationships =
        output_reader.relationships_for(custom_xml_part);
    check(custom_xml_relationships != nullptr,
        "custom XML replacement should keep owner relationships readable");
    const auto* custom_xml_props_relationship =
        custom_xml_relationships->find_by_id("rIdCustomXmlProps");
    check(custom_xml_props_relationship != nullptr,
        "custom XML replacement should keep customXmlProps relationship id");
    check(custom_xml_props_relationship->target == "itemProps1.xml",
        "custom XML replacement should not rewrite customXmlProps target");
    check(custom_xml_props_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "custom XML replacement should keep customXmlProps target mode");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.package_relationships().find_by_id("rIdCustomXml") != nullptr,
        "relationship graph should keep package customXml relationship after replacement");
    check(output_graph.relationships_for(custom_xml_part)->find_by_id("rIdCustomXmlProps")
            != nullptr,
        "relationship graph should keep customXmlProps relationship after replacement");
    const auto* custom_xml_content_type =
        output_reader.content_types().content_type_for(custom_xml_part);
    check(custom_xml_content_type != nullptr && *custom_xml_content_type == "application/xml",
        "custom XML replacement should preserve default XML content type for item");
    check(output_reader.content_types().override_for(custom_xml_props_part) != nullptr,
        "custom XML replacement should preserve custom XML properties content type override");
}

void test_package_editor_repeated_custom_xml_replacement_updates_final_state()
{
    const CustomXmlSourcePackage source =
        write_custom_xml_source_package(
            "fastxlsx-package-editor-repeat-custom-xml-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-repeat-custom-xml-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName custom_xml_part("/customXml/item1.xml");
    const fastxlsx::detail::PartName custom_xml_props_part("/customXml/itemProps1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");
    const std::string custom_xml_relationships_entry = "customXml/_rels/item1.xml.rels";

    const std::string stale_custom_xml =
        R"(<fx:payload xmlns:fx="urn:fastxlsx:test-custom-xml">)"
        R"(<fx:value>Stale custom XML payload</fx:value>)"
        R"(</fx:payload>)";
    const std::string final_custom_xml =
        R"(<fx:payload xmlns:fx="urn:fastxlsx:test-custom-xml">)"
        R"(<fx:value>Final custom XML payload</fx:value>)"
        R"(</fx:payload>)";

    replace_part_with_memory_chunks(editor, custom_xml_part, stale_custom_xml,
        "stale repeated custom XML item replacement");
    replace_part_with_memory_chunks(editor, custom_xml_part, final_custom_xml,
        "final repeated custom XML item replacement");

    const auto* custom_xml_plan = editor.edit_plan().find_part(custom_xml_part);
    check(custom_xml_plan != nullptr,
        "repeated custom XML replacement should keep an active edit-plan part");
    check(custom_xml_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated custom XML replacement should keep final local-DOM-rewrite mode");
    check(custom_xml_plan->reason.find("final repeated") != std::string::npos,
        "repeated custom XML replacement should keep final reason");
    check(custom_xml_plan->reason.find("stale repeated") == std::string::npos,
        "repeated custom XML replacement should drop stale reason");
    check_manifest_write_mode(editor, custom_xml_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated custom XML replacement should mirror final write mode into manifest");
    const auto* custom_xml_content_type =
        editor.manifest().content_types().content_type_for(custom_xml_part);
    check(custom_xml_content_type != nullptr && *custom_xml_content_type == "application/xml",
        "repeated custom XML replacement should keep default XML content type");
    check(editor.edit_plan().find_removed_part(custom_xml_part) == nullptr,
        "repeated custom XML replacement should not leave removed-part audit");
    check(editor.edit_plan().find_removed_package_entry(custom_xml_relationships_entry)
            == nullptr,
        "repeated custom XML replacement should not leave owner relationships omission");
    const auto* custom_xml_relationships_audit =
        editor.edit_plan().find_package_entry(custom_xml_relationships_entry);
    check(custom_xml_relationships_audit != nullptr,
        "repeated custom XML replacement should preserve owner relationships audit");
    check(custom_xml_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated custom XML replacement should preserve owner relationships bytes");
    check(custom_xml_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "repeated custom XML replacement should keep owner relationship audit role");
    check(custom_xml_relationships_audit->owner_part == custom_xml_part.value(),
        "repeated custom XML replacement should keep owner relationship context");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "repeated custom XML replacement should not rewrite content types audit");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "repeated custom XML replacement should not rewrite package relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated custom XML replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated custom XML replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(custom_xml_props_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated custom XML replacement should keep properties part copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated custom XML replacement should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "repeated custom XML replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "repeated custom XML replacement output plan should preserve calcChain policy");
    check(output_plan.relationship_target_audits.empty(),
        "repeated custom XML replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "repeated custom XML replacement output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "repeated custom XML replacement output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "customXml/item1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "repeated custom XML replacement output plan should rewrite custom XML item");
    const auto* output_custom_xml_plan =
        find_output_entry_plan(output_plan.entries, "customXml/item1.xml");
    check(output_custom_xml_plan->reason.find("final repeated") != std::string::npos,
        "repeated custom XML replacement output plan should keep final reason");
    check(output_custom_xml_plan->reason.find("stale repeated") == std::string::npos,
        "repeated custom XML replacement output plan should drop stale reason");
    check_output_entry_plan(output_plan.entries, custom_xml_relationships_entry,
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated custom XML replacement output plan should preserve owner relationships");
    const auto* output_custom_xml_relationships_plan =
        find_output_entry_plan(output_plan.entries, custom_xml_relationships_entry);
    check(output_custom_xml_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "repeated custom XML replacement output plan should keep owner relationship audit role");
    check(output_custom_xml_relationships_plan->owner_part == custom_xml_part.value(),
        "repeated custom XML replacement output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated custom XML replacement output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated custom XML replacement output plan should preserve package relationships");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("customXml/item1.xml") == final_custom_xml,
        "repeated custom XML replacement should write final custom XML payload");
    check(output_reader.read_entry("customXml/item1.xml") != stale_custom_xml,
        "repeated custom XML replacement should not write stale custom XML payload");
    check(output_reader.read_entry(custom_xml_relationships_entry)
            == source.custom_xml_relationships,
        "repeated custom XML replacement should preserve owner relationships bytes");
    check(output_reader.read_entry("customXml/itemProps1.xml")
            == source.custom_xml_properties,
        "repeated custom XML replacement should preserve properties bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "repeated custom XML replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "repeated custom XML replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "repeated custom XML replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "repeated custom XML replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "repeated custom XML replacement should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "repeated custom XML replacement should preserve unknown bytes");

    const auto* package_custom_xml_relationship =
        output_reader.package_relationships().find_by_id("rIdCustomXml");
    check(package_custom_xml_relationship != nullptr,
        "repeated custom XML replacement should keep package customXml relationship id");
    check(package_custom_xml_relationship->target == "customXml/item1.xml",
        "repeated custom XML replacement should preserve package customXml target");
    const auto* custom_xml_relationships =
        output_reader.relationships_for(custom_xml_part);
    check(custom_xml_relationships != nullptr,
        "repeated custom XML replacement should keep owner relationships readable");
    const auto* custom_xml_props_relationship =
        custom_xml_relationships->find_by_id("rIdCustomXmlProps");
    check(custom_xml_props_relationship != nullptr,
        "repeated custom XML replacement should keep customXmlProps relationship id");
    check(custom_xml_props_relationship->target == "itemProps1.xml",
        "repeated custom XML replacement should preserve customXmlProps target");
    check(output_reader.content_types().content_type_for(custom_xml_part) != nullptr,
        "repeated custom XML replacement should keep default XML content type");
    check(output_reader.content_types().override_for(custom_xml_props_part) != nullptr,
        "repeated custom XML replacement should keep properties content type override");
}

void test_package_editor_removes_custom_xml_and_preserves_package_links()
{
    const CustomXmlSourcePackage source =
        write_custom_xml_source_package("fastxlsx-package-editor-remove-custom-xml-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-custom-xml-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName custom_xml_part("/customXml/item1.xml");
    const fastxlsx::detail::PartName custom_xml_props_part("/customXml/itemProps1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    editor.remove_part(custom_xml_part, "explicit custom XML item removal");

    check(editor.edit_plan().find_part(custom_xml_part) == nullptr,
        "custom XML removal should clear the active edit-plan part");
    const auto* removed_custom_xml =
        editor.edit_plan().find_removed_part(custom_xml_part);
    check(removed_custom_xml != nullptr,
        "custom XML removal should record removed-part audit");
    check(removed_custom_xml->reason.find("custom XML") != std::string::npos,
        "custom XML removal should keep readable removal reason");
    check(removed_custom_xml->reason.find("inbound relationship preserved")
            != std::string::npos,
        "custom XML removal should audit preserved inbound relationship");
    check(removed_custom_xml->reason.find("_rels/.rels") != std::string::npos,
        "custom XML removal inbound audit should include package relationships entry");
    check(removed_custom_xml->reason.find("rIdCustomXml") != std::string::npos,
        "custom XML removal inbound audit should include package relationship id");
    check(removed_custom_xml->reason.find("customXml/item1.xml") != std::string::npos,
        "custom XML removal inbound audit should include raw package target");
    check(removed_custom_xml->inbound_relationships.size() == 1,
        "custom XML removal should keep one structured inbound audit");
    const auto& inbound = removed_custom_xml->inbound_relationships.front();
    check(inbound.owner_part.empty(),
        "custom XML removal should keep package inbound owner part empty");
    check(inbound.owner_entry == "_rels/.rels",
        "custom XML removal should keep package relationships entry");
    check(inbound.relationship_id == "rIdCustomXml",
        "custom XML removal should keep package relationship id");
    check(inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXml",
        "custom XML removal should keep package relationship type");
    check(inbound.relationship_target == "customXml/item1.xml",
        "custom XML removal should keep package raw target");
    check(inbound.target_part == custom_xml_part,
        "custom XML removal should keep normalized custom XML target");
    check(editor.manifest().find_part(custom_xml_part) == nullptr,
        "custom XML removal should remove target part from manifest");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "custom XML removal should not rewrite content types for default XML part");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "custom XML removal should not rewrite package relationships");
    const auto* removed_custom_xml_relationships =
        editor.edit_plan().find_removed_package_entry("customXml/_rels/item1.xml.rels");
    check(removed_custom_xml_relationships != nullptr,
        "custom XML removal should omit source-owned custom XML relationships");
    check(removed_custom_xml_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "custom XML owner relationships omission should keep source relationship role");
    check(removed_custom_xml_relationships->owner_part == custom_xml_part.value(),
        "custom XML owner relationships omission should keep owner part");
    check(editor.edit_plan().find_package_entry("customXml/_rels/item1.xml.rels") == nullptr,
        "custom XML removal should not keep active owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(custom_xml_props_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML removal should keep properties part copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML removal should keep unrelated unknown part copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "custom XML removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "custom XML removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "custom XML removal output plan should not invent dependency audits");
    check(output_plan.removed_parts.size() == 1,
        "custom XML removal output plan should expose one removed part");
    check(output_plan.removed_parts.front().part_name == custom_xml_part,
        "custom XML removal output plan should expose removed custom XML item");
    check(output_plan.removed_parts.front().reason.find("custom XML") != std::string::npos,
        "custom XML removal output plan should keep removed item reason");
    check(output_plan.removed_parts.front().inbound_relationships.size() == 1,
        "custom XML removal output plan should expose removed item inbound audit");
    check(output_plan.removed_package_entries.size() == 1,
        "custom XML removal output plan should expose owner relationships omission");
    check(output_plan.removed_package_entries.front().entry_name
            == "customXml/_rels/item1.xml.rels",
        "custom XML removal output plan should omit item owner relationships");
    check(output_plan.removed_package_entries.front().audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "custom XML removal output plan should classify omitted owner relationships");
    check(output_plan.removed_package_entries.front().owner_part == custom_xml_part.value(),
        "custom XML removal output plan should keep omitted owner context");
    check_output_entry_plan(output_plan.entries, "customXml/item1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "custom XML removal output plan should omit custom XML item");
    check_output_entry_part_context(output_plan.entries, "customXml/item1.xml",
        true, custom_xml_part.value(),
        "custom XML removal output plan should classify omitted custom XML item");
    const auto* output_custom_xml_plan =
        find_output_entry_plan(output_plan.entries, "customXml/item1.xml");
    check(output_custom_xml_plan->reason.find("custom XML") != std::string::npos,
        "custom XML removal output plan should keep item removal reason");
    check(output_custom_xml_plan->inbound_relationships.size() == 1,
        "custom XML removal output plan should expose package inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "customXml/item1.xml", "", "_rels/.rels", "rIdCustomXml",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXml",
        "customXml/item1.xml", custom_xml_part,
        "custom XML removal output plan should keep package inbound audit");
    check_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "custom XML removal output plan should omit owner relationships");
    check_output_entry_part_context(output_plan.entries, "customXml/_rels/item1.xml.rels",
        false, "",
        "custom XML removal output plan should classify owner relationships as metadata");
    const auto* output_custom_xml_relationships_plan =
        find_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels");
    check(output_custom_xml_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "custom XML removal output plan should classify owner relationships metadata");
    check(output_custom_xml_relationships_plan->owner_part == custom_xml_part.value(),
        "custom XML removal output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML removal output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "custom XML removal output plan should classify content types as metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML removal output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML removal output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML removal output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML removal output plan should preserve properties part");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML removal output plan should preserve unknown entry");
    check(find_output_entry_plan(output_plan.entries, "customXml/_rels/itemProps1.xml.rels")
            == nullptr,
        "custom XML removal output plan should not invent properties relationships");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("customXml/item1.xml") == entries.end(),
        "custom XML removal output should omit custom XML item");
    check(entries.find("customXml/_rels/item1.xml.rels") == entries.end(),
        "custom XML removal output should omit custom XML owner relationships");
    check(entries.find("customXml/itemProps1.xml") != entries.end(),
        "custom XML removal output should preserve custom XML properties");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "custom XML removal should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "custom XML removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "custom XML removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "custom XML removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "custom XML removal should preserve worksheet bytes");
    check(output_reader.read_entry("customXml/itemProps1.xml")
            == source.custom_xml_properties,
        "custom XML removal should preserve properties bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "custom XML removal should preserve unknown bytes");

    const auto* package_custom_xml_relationship =
        output_reader.package_relationships().find_by_id("rIdCustomXml");
    check(package_custom_xml_relationship != nullptr,
        "custom XML removal should keep package customXml relationship");
    check(package_custom_xml_relationship->target == "customXml/item1.xml",
        "custom XML removal should not rewrite package customXml target");
    check(package_custom_xml_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "custom XML removal should keep package customXml target mode");
    check(output_reader.relationships_for(custom_xml_part) == nullptr,
        "custom XML removal should not keep owner relationships for absent item");
    const auto* custom_xml_content_type =
        output_reader.content_types().content_type_for(custom_xml_part);
    check(custom_xml_content_type != nullptr && *custom_xml_content_type == "application/xml",
        "custom XML removal should preserve default XML content type for item path");
    check(output_reader.content_types().override_for(custom_xml_props_part) != nullptr,
        "custom XML removal should preserve custom XML properties content type override");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.package_relationships().find_by_id("rIdCustomXml") != nullptr,
        "relationship graph should keep package customXml relationship after removal");
    check(output_graph.relationships_for(custom_xml_part) == nullptr,
        "relationship graph should not attach owner relationships to absent custom XML item");
}

void test_package_editor_custom_xml_replacement_restores_prior_removal()
{
    const CustomXmlSourcePackage source =
        write_custom_xml_source_package("fastxlsx-package-editor-replace-after-remove-custom-xml-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-custom-xml-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName custom_xml_part("/customXml/item1.xml");
    const fastxlsx::detail::PartName custom_xml_props_part("/customXml/itemProps1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    editor.remove_part(custom_xml_part, "temporary custom XML item removal");
    check(editor.edit_plan().find_removed_part(custom_xml_part) != nullptr,
        "setup should record removed custom XML before replacement restore");
    check(editor.edit_plan().find_removed_package_entry("customXml/_rels/item1.xml.rels")
            != nullptr,
        "setup should omit custom XML owner relationships before replacement restore");
    check(editor.manifest().find_part(custom_xml_part) == nullptr,
        "setup should remove custom XML from manifest before replacement restore");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "setup should not rewrite content types for default custom XML removal");

    const std::string restored_custom_xml =
        R"(<fx:payload xmlns:fx="urn:fastxlsx:test-custom-xml">)"
        R"(<fx:value>Restored custom XML payload</fx:value>)"
        R"(</fx:payload>)";
    replace_part_with_memory_chunks(editor, custom_xml_part, restored_custom_xml,
        "restored custom XML after removal");

    check(editor.edit_plan().find_removed_part(custom_xml_part) == nullptr,
        "custom XML replacement after removal should clear stale removed-part audit");
    check(editor.edit_plan().find_removed_package_entry("customXml/_rels/item1.xml.rels")
            == nullptr,
        "custom XML replacement after removal should clear stale owner relationships omission");
    const auto* custom_xml_plan = editor.edit_plan().find_part(custom_xml_part);
    check(custom_xml_plan != nullptr,
        "custom XML replacement after removal should restore active edit-plan part");
    check(custom_xml_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "custom XML replacement after removal should keep final write mode");
    check(custom_xml_plan->reason.find("after removal") != std::string::npos,
        "custom XML replacement after removal should keep final replacement reason");
    check_manifest_write_mode(editor, custom_xml_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "custom XML replacement after removal should restore manifest part write mode");
    const auto* custom_xml_relationships_audit =
        editor.edit_plan().find_package_entry("customXml/_rels/item1.xml.rels");
    check(custom_xml_relationships_audit != nullptr,
        "custom XML replacement after removal should restore owner relationships audit");
    check(custom_xml_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML replacement after removal owner relationships should be copy-original");
    check(custom_xml_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "custom XML replacement after removal owner relationships should keep source role");
    check(custom_xml_relationships_audit->owner_part == custom_xml_part.value(),
        "custom XML replacement after removal owner relationships should keep owner part");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "custom XML replacement after removal should not rewrite content types");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "custom XML replacement after removal should not rewrite package relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML replacement after removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML replacement after removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(custom_xml_props_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML replacement after removal should keep properties copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML replacement after removal should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "custom XML replacement after removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "custom XML replacement after removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "custom XML replacement after removal output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "custom XML replacement after removal output plan should clear stale removed-part audits");
    check(output_plan.removed_package_entries.empty(),
        "custom XML replacement after removal output plan should clear stale removed package-entry audits");
    check_output_entry_plan(output_plan.entries, "customXml/item1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "custom XML replacement after removal output plan should rewrite custom XML item");
    check_output_entry_part_context(output_plan.entries, "customXml/item1.xml",
        true, custom_xml_part.value(),
        "custom XML replacement after removal output plan should classify rewritten custom XML item");
    const auto* output_custom_xml_plan =
        find_output_entry_plan(output_plan.entries, "customXml/item1.xml");
    check(output_custom_xml_plan->reason.find("after removal") != std::string::npos,
        "custom XML replacement after removal output plan should keep replacement reason");
    check_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement after removal output plan should preserve owner relationships");
    check_output_entry_part_context(output_plan.entries, "customXml/_rels/item1.xml.rels",
        false, "",
        "custom XML replacement after removal output plan should classify owner relationships as metadata");
    const auto* output_custom_xml_relationships_plan =
        find_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels");
    check(output_custom_xml_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "custom XML replacement after removal output plan should classify owner relationships metadata");
    check(output_custom_xml_relationships_plan->owner_part == custom_xml_part.value(),
        "custom XML replacement after removal output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement after removal output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "custom XML replacement after removal output plan should classify content types as metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement after removal output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement after removal output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement after removal output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement after removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement after removal output plan should preserve properties part");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement after removal output plan should preserve unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("customXml/item1.xml") != entries.end(),
        "custom XML replacement after removal output should restore item entry");
    check(entries.find("customXml/_rels/item1.xml.rels") != entries.end(),
        "custom XML replacement after removal output should restore owner relationships");
    check(entries.find("customXml/itemProps1.xml") != entries.end(),
        "custom XML replacement after removal output should keep properties part");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(
        editor.reader(), output_reader, custom_xml_part.zip_path());
    check(output_reader.read_entry("customXml/item1.xml") == restored_custom_xml,
        "custom XML replacement after removal should write restored payload");
    check(output_reader.read_entry("customXml/_rels/item1.xml.rels")
            == source.custom_xml_relationships,
        "custom XML replacement after removal should preserve owner relationships bytes");
    check(output_reader.read_entry("customXml/itemProps1.xml")
            == source.custom_xml_properties,
        "custom XML replacement after removal should preserve properties bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "custom XML replacement after removal should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "custom XML replacement after removal should preserve package relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "custom XML replacement after removal should preserve unknown bytes");

    const auto* package_custom_xml_relationship =
        output_reader.package_relationships().find_by_id("rIdCustomXml");
    check(package_custom_xml_relationship != nullptr,
        "custom XML replacement after removal should keep package customXml relationship");
    check(package_custom_xml_relationship->target == "customXml/item1.xml",
        "custom XML replacement after removal should not rewrite package customXml target");
    const auto* custom_xml_relationships =
        output_reader.relationships_for(custom_xml_part);
    check(custom_xml_relationships != nullptr,
        "custom XML replacement after removal should keep owner relationships readable");
    check(custom_xml_relationships->find_by_id("rIdCustomXmlProps") != nullptr,
        "custom XML replacement after removal should keep customXmlProps relationship");
    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.package_relationships().find_by_id("rIdCustomXml") != nullptr,
        "relationship graph should keep package customXml relationship after restore");
    check(output_graph.relationships_for(custom_xml_part)->find_by_id("rIdCustomXmlProps")
            != nullptr,
        "relationship graph should keep customXmlProps relationship after restore");
    const auto* custom_xml_content_type =
        output_reader.content_types().content_type_for(custom_xml_part);
    check(custom_xml_content_type != nullptr && *custom_xml_content_type == "application/xml",
        "custom XML replacement after removal should preserve default XML content type");
    check(output_reader.content_types().override_for(custom_xml_props_part) != nullptr,
        "custom XML replacement after removal should preserve properties content type override");
}

void test_package_editor_custom_xml_removal_overrides_prior_replacement()
{
    const CustomXmlSourcePackage source =
        write_custom_xml_source_package("fastxlsx-package-editor-remove-after-replace-custom-xml-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-custom-xml-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName custom_xml_part("/customXml/item1.xml");
    const fastxlsx::detail::PartName custom_xml_props_part("/customXml/itemProps1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string stale_custom_xml =
        R"(<fx:payload xmlns:fx="urn:fastxlsx:test-custom-xml">)"
        R"(<fx:value>Stale custom XML payload</fx:value>)"
        R"(</fx:payload>)";
    replace_part_with_memory_chunks(editor, custom_xml_part, stale_custom_xml,
        "prior custom XML replacement before removal");
    const auto* prior_custom_xml_plan = editor.edit_plan().find_part(custom_xml_part);
    check(prior_custom_xml_plan != nullptr,
        "setup should record active custom XML replacement before removal override");
    check(prior_custom_xml_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "setup custom XML replacement should be local-DOM-rewrite before removal override");
    check(editor.edit_plan().find_package_entry("customXml/_rels/item1.xml.rels")
            != nullptr,
        "setup custom XML replacement should audit owner relationships");

    editor.remove_part(custom_xml_part, "explicit custom XML removal after replacement");

    check(editor.edit_plan().find_part(custom_xml_part) == nullptr,
        "custom XML removal after replacement should clear active replacement entry");
    const auto* removed_custom_xml =
        editor.edit_plan().find_removed_part(custom_xml_part);
    check(removed_custom_xml != nullptr,
        "custom XML removal after replacement should record removed-part audit");
    check(removed_custom_xml->reason.find("after replacement") != std::string::npos,
        "custom XML removal after replacement should keep final removal reason");
    check(removed_custom_xml->reason.find("inbound relationship preserved")
            != std::string::npos,
        "custom XML removal after replacement should keep inbound relationship audit");
    check(removed_custom_xml->inbound_relationships.size() == 1,
        "custom XML removal after replacement should keep structured inbound audit");
    check(removed_custom_xml->inbound_relationships.front().target_part == custom_xml_part,
        "custom XML removal after replacement should keep normalized inbound target");
    check(editor.manifest().find_part(custom_xml_part) == nullptr,
        "custom XML removal after replacement should remove manifest part");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "custom XML removal after replacement should not rewrite content types");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "custom XML removal after replacement should not rewrite package relationships");
    const auto* removed_custom_xml_relationships =
        editor.edit_plan().find_removed_package_entry("customXml/_rels/item1.xml.rels");
    check(removed_custom_xml_relationships != nullptr,
        "custom XML removal after replacement should omit owner relationships");
    check(removed_custom_xml_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "custom XML removal after replacement owner omission should keep source role");
    check(removed_custom_xml_relationships->owner_part == custom_xml_part.value(),
        "custom XML removal after replacement owner omission should keep owner part");
    check(editor.edit_plan().find_package_entry("customXml/_rels/item1.xml.rels") == nullptr,
        "custom XML removal after replacement should clear active owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML removal after replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(custom_xml_props_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML removal after replacement should keep properties copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML removal after replacement should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "custom XML removal after replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "custom XML removal after replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "custom XML removal after replacement output plan should not invent dependency audits");
    check_output_entry_plan(output_plan.entries, "customXml/item1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "custom XML removal after replacement output plan should omit custom XML item");
    check_output_entry_part_context(output_plan.entries, "customXml/item1.xml",
        true, custom_xml_part.value(),
        "custom XML removal after replacement output plan should classify omitted custom XML item");
    const auto* output_custom_xml_plan =
        find_output_entry_plan(output_plan.entries, "customXml/item1.xml");
    check(output_custom_xml_plan->reason.find("after replacement") != std::string::npos,
        "custom XML removal after replacement output plan should keep final removal reason");
    check(output_custom_xml_plan->inbound_relationships.size() == 1,
        "custom XML removal after replacement output plan should expose package inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "customXml/item1.xml", "", "_rels/.rels", "rIdCustomXml",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXml",
        "customXml/item1.xml", custom_xml_part,
        "custom XML removal after replacement output plan should keep package inbound audit");
    check_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "custom XML removal after replacement output plan should omit owner relationships");
    check_output_entry_part_context(output_plan.entries, "customXml/_rels/item1.xml.rels",
        false, "",
        "custom XML removal after replacement output plan should classify owner relationships as metadata");
    const auto* output_custom_xml_relationships_plan =
        find_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels");
    check(output_custom_xml_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "custom XML removal after replacement output plan should classify owner relationships metadata");
    check(output_custom_xml_relationships_plan->owner_part == custom_xml_part.value(),
        "custom XML removal after replacement output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML removal after replacement output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML removal after replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML removal after replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML removal after replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML removal after replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML removal after replacement output plan should preserve properties part");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML removal after replacement output plan should preserve unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("customXml/item1.xml") == entries.end(),
        "custom XML removal after replacement output should omit item entry");
    check(entries.find("customXml/_rels/item1.xml.rels") == entries.end(),
        "custom XML removal after replacement output should omit owner relationships");
    check(entries.find("customXml/itemProps1.xml") != entries.end(),
        "custom XML removal after replacement output should keep properties part");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "custom XML removal after replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "custom XML removal after replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "custom XML removal after replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "custom XML removal after replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "custom XML removal after replacement should preserve worksheet bytes");
    check(output_reader.read_entry("customXml/itemProps1.xml")
            == source.custom_xml_properties,
        "custom XML removal after replacement should preserve properties bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "custom XML removal after replacement should preserve unknown bytes");

    const auto* package_custom_xml_relationship =
        output_reader.package_relationships().find_by_id("rIdCustomXml");
    check(package_custom_xml_relationship != nullptr,
        "custom XML removal after replacement should keep package customXml relationship");
    check(package_custom_xml_relationship->target == "customXml/item1.xml",
        "custom XML removal after replacement should not rewrite package customXml target");
    check(package_custom_xml_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "custom XML removal after replacement should keep package customXml target mode");
    check(output_reader.relationships_for(custom_xml_part) == nullptr,
        "custom XML removal after replacement should not keep owner relationships");
    const auto* custom_xml_content_type =
        output_reader.content_types().content_type_for(custom_xml_part);
    check(custom_xml_content_type != nullptr && *custom_xml_content_type == "application/xml",
        "custom XML removal after replacement should preserve default XML content type");
    check(output_reader.content_types().override_for(custom_xml_props_part) != nullptr,
        "custom XML removal after replacement should preserve properties content type override");
    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.package_relationships().find_by_id("rIdCustomXml") != nullptr,
        "relationship graph should keep package customXml relationship after removal override");
    check(output_graph.relationships_for(custom_xml_part) == nullptr,
        "relationship graph should not attach owner relationships after removal override");
}

void test_package_editor_replaces_custom_xml_properties_and_preserves_owner_links()
{
    const CustomXmlSourcePackage source =
        write_custom_xml_source_package("fastxlsx-package-editor-linked-custom-xml-props-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-linked-custom-xml-props-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName custom_xml_part("/customXml/item1.xml");
    const fastxlsx::detail::PartName custom_xml_props_part("/customXml/itemProps1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string replacement_properties =
        R"(<ds:datastoreItem ds:itemID="{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}" xmlns:ds="http://schemas.openxmlformats.org/officeDocument/2006/customXml">)"
        R"(<ds:schemaRefs><ds:schemaRef ds:uri="urn:fastxlsx:replacement"/></ds:schemaRefs>)"
        R"(</ds:datastoreItem>)";
    replace_part_with_memory_chunks(editor, custom_xml_props_part, replacement_properties,
        "custom XML properties replacement");

    const auto* custom_xml_props_plan =
        editor.edit_plan().find_part(custom_xml_props_part);
    check(custom_xml_props_plan != nullptr,
        "custom XML properties replacement should keep target part in edit plan");
    check(custom_xml_props_plan->write_mode
            == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "custom XML properties replacement should record final write mode");
    check(custom_xml_props_plan->reason.find("properties") != std::string::npos,
        "custom XML properties replacement should keep readable reason");
    check_manifest_write_mode(editor, custom_xml_props_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "custom XML properties replacement should update manifest write mode");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "custom XML properties replacement should not rewrite content types");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "custom XML properties replacement should not rewrite package relationships");
    check(editor.edit_plan().find_removed_part(custom_xml_props_part) == nullptr,
        "custom XML properties replacement should not record removed-part audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(custom_xml_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties replacement should keep custom XML item copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties replacement should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "custom XML properties replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "custom XML properties replacement output plan should keep calcChain preserve state");
    check(output_plan.notes.empty(),
        "custom XML properties replacement output plan should not invent audit notes");
    check(output_plan.relationship_target_audits.empty(),
        "custom XML properties replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "custom XML properties replacement output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "custom XML properties replacement output plan should not expose removed package entries");
    check_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "custom XML properties replacement output plan should rewrite properties part");
    check_output_entry_part_context(output_plan.entries, "customXml/itemProps1.xml",
        true, custom_xml_props_part.value(),
        "custom XML properties replacement output plan should classify properties part");
    const auto* output_props_plan =
        find_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml");
    check(output_props_plan->reason.find("properties") != std::string::npos,
        "custom XML properties replacement output plan should keep replacement reason");
    check(find_output_entry_plan(output_plan.entries, "customXml/_rels/itemProps1.xml.rels")
            == nullptr,
        "custom XML properties replacement output plan should not invent properties owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml",
        false, "",
        "custom XML properties replacement output plan should classify content types as metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "customXml/item1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement output plan should preserve custom XML item");
    check_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement output plan should preserve item owner relationships");
    check_output_entry_part_context(output_plan.entries, "customXml/_rels/item1.xml.rels",
        false, "",
        "custom XML properties replacement output plan should classify item owner relationships");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement output plan should preserve unknown entry");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(
        editor.reader(), output_reader, custom_xml_props_part.zip_path());
    check(output_reader.read_entry("customXml/itemProps1.xml") == replacement_properties,
        "custom XML properties replacement output should write replacement payload");
    check(output_reader.read_entry("customXml/item1.xml") == source.custom_xml,
        "custom XML properties replacement should preserve custom XML item bytes");
    check(output_reader.read_entry("customXml/_rels/item1.xml.rels")
            == source.custom_xml_relationships,
        "custom XML properties replacement should preserve owner relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "custom XML properties replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "custom XML properties replacement should preserve package relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "custom XML properties replacement should preserve unknown bytes");

    const auto* package_custom_xml_relationship =
        output_reader.package_relationships().find_by_id("rIdCustomXml");
    check(package_custom_xml_relationship != nullptr,
        "custom XML properties replacement should keep package customXml relationship");
    const auto* custom_xml_relationships =
        output_reader.relationships_for(custom_xml_part);
    check(custom_xml_relationships != nullptr,
        "custom XML properties replacement should keep owner relationships readable");
    const auto* custom_xml_props_relationship =
        custom_xml_relationships->find_by_id("rIdCustomXmlProps");
    check(custom_xml_props_relationship != nullptr,
        "custom XML properties replacement should keep customXmlProps relationship");
    check(custom_xml_props_relationship->target == "itemProps1.xml",
        "custom XML properties replacement should not rewrite customXmlProps target");
    check(custom_xml_props_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "custom XML properties replacement should keep customXmlProps target mode");
    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(custom_xml_part)->find_by_id("rIdCustomXmlProps")
            != nullptr,
        "relationship graph should keep customXmlProps relationship after properties replacement");
    check(output_reader.relationships_for(custom_xml_props_part) == nullptr,
        "custom XML properties replacement should not invent properties owner relationships");
    check(output_reader.content_types().override_for(custom_xml_props_part) != nullptr,
        "custom XML properties replacement should preserve properties content type override");
}

void test_package_editor_repeated_custom_xml_properties_replacement_updates_final_state()
{
    const CustomXmlSourcePackage source =
        write_custom_xml_source_package(
            "fastxlsx-package-editor-repeat-custom-xml-props-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-repeat-custom-xml-props-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName custom_xml_part("/customXml/item1.xml");
    const fastxlsx::detail::PartName custom_xml_props_part("/customXml/itemProps1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string stale_properties =
        R"(<ds:datastoreItem ds:itemID="{11111111-2222-3333-4444-aaaaaaaaaaaa}" xmlns:ds="http://schemas.openxmlformats.org/officeDocument/2006/customXml">)"
        R"(<ds:schemaRefs><ds:schemaRef ds:uri="urn:fastxlsx:stale"/></ds:schemaRefs>)"
        R"(</ds:datastoreItem>)";
    const std::string final_properties =
        R"(<ds:datastoreItem ds:itemID="{22222222-3333-4444-5555-bbbbbbbbbbbb}" xmlns:ds="http://schemas.openxmlformats.org/officeDocument/2006/customXml">)"
        R"(<ds:schemaRefs><ds:schemaRef ds:uri="urn:fastxlsx:final"/></ds:schemaRefs>)"
        R"(</ds:datastoreItem>)";

    replace_part_with_memory_chunks(editor, custom_xml_props_part, stale_properties,
        "stale repeated custom XML properties replacement");
    replace_part_with_memory_chunks(editor, custom_xml_props_part, final_properties,
        "final repeated custom XML properties replacement");

    const auto* custom_xml_props_plan =
        editor.edit_plan().find_part(custom_xml_props_part);
    check(custom_xml_props_plan != nullptr,
        "repeated custom XML properties replacement should keep an active edit-plan part");
    check(custom_xml_props_plan->write_mode
            == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated custom XML properties replacement should keep final local-DOM-rewrite mode");
    check(custom_xml_props_plan->reason.find("final repeated") != std::string::npos,
        "repeated custom XML properties replacement should keep final reason");
    check(custom_xml_props_plan->reason.find("stale repeated") == std::string::npos,
        "repeated custom XML properties replacement should drop stale reason");
    check_manifest_write_mode(editor, custom_xml_props_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated custom XML properties replacement should mirror final write mode into manifest");
    check(editor.manifest().content_types().override_for(custom_xml_props_part) != nullptr,
        "repeated custom XML properties replacement should keep properties content type override");
    check(editor.edit_plan().find_removed_part(custom_xml_props_part) == nullptr,
        "repeated custom XML properties replacement should not leave removed-part audit");
    check(editor.edit_plan().find_removed_package_entry(
              "customXml/_rels/itemProps1.xml.rels")
            == nullptr,
        "repeated custom XML properties replacement should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry("customXml/_rels/itemProps1.xml.rels")
            == nullptr,
        "repeated custom XML properties replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "repeated custom XML properties replacement should not rewrite content types audit");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "repeated custom XML properties replacement should not rewrite package relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated custom XML properties replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated custom XML properties replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(custom_xml_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated custom XML properties replacement should keep custom XML item copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated custom XML properties replacement should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "repeated custom XML properties replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "repeated custom XML properties replacement output plan should preserve calcChain policy");
    check(output_plan.notes.empty(),
        "repeated custom XML properties replacement output plan should not invent audit notes");
    check(output_plan.relationship_target_audits.empty(),
        "repeated custom XML properties replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "repeated custom XML properties replacement output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "repeated custom XML properties replacement output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "repeated custom XML properties replacement output plan should rewrite properties part");
    const auto* output_props_plan =
        find_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml");
    check(output_props_plan->reason.find("final repeated") != std::string::npos,
        "repeated custom XML properties replacement output plan should keep final reason");
    check(output_props_plan->reason.find("stale repeated") == std::string::npos,
        "repeated custom XML properties replacement output plan should drop stale reason");
    check(find_output_entry_plan(output_plan.entries, "customXml/_rels/itemProps1.xml.rels")
            == nullptr,
        "repeated custom XML properties replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated custom XML properties replacement output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated custom XML properties replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "customXml/item1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated custom XML properties replacement output plan should preserve custom XML item");
    check_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated custom XML properties replacement output plan should preserve item owner relationships");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("customXml/itemProps1.xml") == final_properties,
        "repeated custom XML properties replacement should write final properties payload");
    check(output_reader.read_entry("customXml/itemProps1.xml") != stale_properties,
        "repeated custom XML properties replacement should not write stale properties payload");
    check(output_reader.read_entry("customXml/item1.xml") == source.custom_xml,
        "repeated custom XML properties replacement should preserve custom XML item bytes");
    check(output_reader.read_entry("customXml/_rels/item1.xml.rels")
            == source.custom_xml_relationships,
        "repeated custom XML properties replacement should preserve item owner relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "repeated custom XML properties replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "repeated custom XML properties replacement should preserve package relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "repeated custom XML properties replacement should preserve unknown bytes");

    const auto* package_custom_xml_relationship =
        output_reader.package_relationships().find_by_id("rIdCustomXml");
    check(package_custom_xml_relationship != nullptr,
        "repeated custom XML properties replacement should keep package customXml relationship");
    const auto* custom_xml_relationships =
        output_reader.relationships_for(custom_xml_part);
    check(custom_xml_relationships != nullptr,
        "repeated custom XML properties replacement should keep item relationships readable");
    const auto* custom_xml_props_relationship =
        custom_xml_relationships->find_by_id("rIdCustomXmlProps");
    check(custom_xml_props_relationship != nullptr,
        "repeated custom XML properties replacement should keep customXmlProps relationship");
    check(custom_xml_props_relationship->target == "itemProps1.xml",
        "repeated custom XML properties replacement should preserve customXmlProps target");
    check(output_reader.relationships_for(custom_xml_props_part) == nullptr,
        "repeated custom XML properties replacement should not invent properties owner relationships");
    check(output_reader.content_types().override_for(custom_xml_props_part) != nullptr,
        "repeated custom XML properties replacement should keep properties content type override");
}

void test_package_editor_removes_custom_xml_properties_and_preserves_owner_links()
{
    const CustomXmlSourcePackage source =
        write_custom_xml_source_package("fastxlsx-package-editor-remove-custom-xml-props-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-custom-xml-props-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName custom_xml_part("/customXml/item1.xml");
    const fastxlsx::detail::PartName custom_xml_props_part("/customXml/itemProps1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    editor.remove_part(custom_xml_props_part,
        "explicit custom XML properties removal");

    check(editor.edit_plan().find_part(custom_xml_props_part) == nullptr,
        "custom XML properties removal should clear active part entry");
    const auto* removed_props =
        editor.edit_plan().find_removed_part(custom_xml_props_part);
    check(removed_props != nullptr,
        "custom XML properties removal should record removed-part audit");
    check(removed_props->reason.find("properties") != std::string::npos,
        "custom XML properties removal should keep readable removal reason");
    check(removed_props->reason.find("inbound relationship preserved")
            != std::string::npos,
        "custom XML properties removal should audit preserved inbound relationship");
    check(removed_props->inbound_relationships.size() == 1,
        "custom XML properties removal should keep one structured inbound audit");
    const auto& inbound = removed_props->inbound_relationships.front();
    check(inbound.owner_part == custom_xml_part.value(),
        "custom XML properties removal should keep custom XML owner part");
    check(inbound.owner_entry == "customXml/_rels/item1.xml.rels",
        "custom XML properties removal should keep owner relationships entry");
    check(inbound.relationship_id == "rIdCustomXmlProps",
        "custom XML properties removal should keep customXmlProps relationship id");
    check(inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXmlProps",
        "custom XML properties removal should keep customXmlProps relationship type");
    check(inbound.relationship_target == "itemProps1.xml",
        "custom XML properties removal should keep raw owner target");
    check(inbound.target_part == custom_xml_props_part,
        "custom XML properties removal should keep normalized properties target");
    check(editor.manifest().find_part(custom_xml_props_part) == nullptr,
        "custom XML properties removal should remove target part from manifest");
    check(editor.manifest().content_types().override_for(custom_xml_props_part) == nullptr,
        "custom XML properties removal should remove manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "custom XML properties removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "custom XML properties removal content types audit should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "custom XML properties removal content types audit should keep structured role");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "custom XML properties removal should not rewrite package relationships");
    check(editor.edit_plan().find_removed_package_entry("customXml/_rels/item1.xml.rels")
            == nullptr,
        "custom XML properties removal should not omit inbound owner relationships");
    check(editor.edit_plan().find_removed_package_entry("customXml/_rels/itemProps1.xml.rels")
            == nullptr,
        "custom XML properties removal should not invent missing properties owner relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(custom_xml_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties removal should keep custom XML item copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties removal should keep unknown copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("customXml/itemProps1.xml") == entries.end(),
        "custom XML properties removal output should omit properties part");
    check(entries.find("customXml/item1.xml") != entries.end(),
        "custom XML properties removal output should keep custom XML item");
    check(entries.find("customXml/_rels/item1.xml.rels") != entries.end(),
        "custom XML properties removal output should keep owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(custom_xml_props_part) == nullptr,
        "custom XML properties removal output should remove properties content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/customXml/itemProps1.xml",
        "custom XML properties removal content types XML should omit properties override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "custom XML properties removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "custom XML properties removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "custom XML properties removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "custom XML properties removal should preserve worksheet bytes");
    check(output_reader.read_entry("customXml/item1.xml") == source.custom_xml,
        "custom XML properties removal should preserve custom XML item bytes");
    check(output_reader.read_entry("customXml/_rels/item1.xml.rels")
            == source.custom_xml_relationships,
        "custom XML properties removal should preserve owner relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "custom XML properties removal should preserve unknown bytes");

    const auto* package_custom_xml_relationship =
        output_reader.package_relationships().find_by_id("rIdCustomXml");
    check(package_custom_xml_relationship != nullptr,
        "custom XML properties removal should keep package customXml relationship");
    const auto* custom_xml_relationships =
        output_reader.relationships_for(custom_xml_part);
    check(custom_xml_relationships != nullptr,
        "custom XML properties removal should keep owner relationships readable");
    const auto* custom_xml_props_relationship =
        custom_xml_relationships->find_by_id("rIdCustomXmlProps");
    check(custom_xml_props_relationship != nullptr,
        "custom XML properties removal should keep inbound customXmlProps relationship");
    check(custom_xml_props_relationship->target == "itemProps1.xml",
        "custom XML properties removal should not rewrite inbound customXmlProps target");
    check(custom_xml_props_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "custom XML properties removal should keep inbound customXmlProps target mode");
    check(output_reader.relationships_for(custom_xml_props_part) == nullptr,
        "custom XML properties removal should not invent properties owner relationships");
    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(custom_xml_part)->find_by_id("rIdCustomXmlProps")
            != nullptr,
        "relationship graph should keep inbound customXmlProps relationship after removal");
}

void test_package_editor_custom_xml_properties_replacement_restores_prior_removal()
{
    const CustomXmlSourcePackage source =
        write_custom_xml_source_package("fastxlsx-package-editor-replace-after-remove-custom-xml-props-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-custom-xml-props-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName custom_xml_part("/customXml/item1.xml");
    const fastxlsx::detail::PartName custom_xml_props_part("/customXml/itemProps1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    editor.remove_part(custom_xml_props_part,
        "temporary custom XML properties removal");
    check(editor.edit_plan().find_removed_part(custom_xml_props_part) != nullptr,
        "setup should record removed custom XML properties before replacement restore");
    check(editor.manifest().find_part(custom_xml_props_part) == nullptr,
        "setup should remove custom XML properties from manifest before replacement restore");
    const auto* removal_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(removal_content_types != nullptr,
        "setup should record content types rewrite before properties restore");
    check(removal_content_types->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "setup content types audit should be local-DOM-rewrite after properties removal");
    check(editor.edit_plan().find_removed_package_entry("customXml/_rels/item1.xml.rels")
            == nullptr,
        "setup should not omit inbound owner relationships before properties restore");

    const std::string restored_properties =
        R"(<ds:datastoreItem ds:itemID="{bbbbbbbb-cccc-dddd-eeee-ffffffffffff}" xmlns:ds="http://schemas.openxmlformats.org/officeDocument/2006/customXml">)"
        R"(<ds:schemaRefs><ds:schemaRef ds:uri="urn:fastxlsx:restored"/></ds:schemaRefs>)"
        R"(</ds:datastoreItem>)";
    replace_part_with_memory_chunks(editor, custom_xml_props_part, restored_properties,
        "restored custom XML properties after removal");

    check(editor.edit_plan().find_removed_part(custom_xml_props_part) == nullptr,
        "custom XML properties replacement after removal should clear stale removed-part audit");
    const auto* custom_xml_props_plan =
        editor.edit_plan().find_part(custom_xml_props_part);
    check(custom_xml_props_plan != nullptr,
        "custom XML properties replacement after removal should restore active part");
    check(custom_xml_props_plan->write_mode
            == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "custom XML properties replacement after removal should keep final write mode");
    check(custom_xml_props_plan->reason.find("after removal") != std::string::npos,
        "custom XML properties replacement after removal should keep final reason");
    check_manifest_write_mode(editor, custom_xml_props_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "custom XML properties replacement after removal should restore manifest write mode");
    check(editor.manifest().content_types().override_for(custom_xml_props_part) != nullptr,
        "custom XML properties replacement after removal should restore manifest content type override");
    const auto* restored_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(restored_content_types != nullptr,
        "custom XML properties replacement after removal should keep content types audit");
    check(restored_content_types->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties replacement after removal should restore source content types audit");
    check(restored_content_types->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "custom XML properties replacement after removal content types audit should keep role");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "custom XML properties replacement after removal should not rewrite package relationships");
    check(editor.edit_plan().find_removed_package_entry("customXml/_rels/item1.xml.rels")
            == nullptr,
        "custom XML properties replacement after removal should not omit inbound owner relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties replacement after removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties replacement after removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(custom_xml_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties replacement after removal should keep custom XML item copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties replacement after removal should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "custom XML properties replacement after removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "custom XML properties replacement after removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "custom XML properties replacement after removal output plan should not invent dependency audits");
    check_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "custom XML properties replacement after removal output plan should rewrite properties part");
    check_output_entry_part_context(output_plan.entries, "customXml/itemProps1.xml",
        true, custom_xml_props_part.value(),
        "custom XML properties replacement after removal output plan should classify properties part");
    const auto* output_props_plan =
        find_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml");
    check(output_props_plan->reason.find("after removal") != std::string::npos,
        "custom XML properties replacement after removal output plan should keep final reason");
    check(find_output_entry_plan(output_plan.entries, "customXml/_rels/itemProps1.xml.rels")
            == nullptr,
        "custom XML properties replacement after removal output plan should not invent properties owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement after removal output plan should restore content types copy-original");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml",
        false, "",
        "custom XML properties replacement after removal output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "custom XML properties replacement after removal output plan should keep content types role");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement after removal output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement after removal output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement after removal output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement after removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "customXml/item1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement after removal output plan should preserve custom XML item");
    check_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement after removal output plan should preserve item owner relationships");
    check_output_entry_part_context(output_plan.entries, "customXml/_rels/item1.xml.rels",
        false, "",
        "custom XML properties replacement after removal output plan should classify item owner relationships");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement after removal output plan should preserve unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("customXml/itemProps1.xml") != entries.end(),
        "custom XML properties replacement after removal output should restore properties part");
    check(entries.find("customXml/item1.xml") != entries.end(),
        "custom XML properties replacement after removal output should keep custom XML item");
    check(entries.find("customXml/_rels/item1.xml.rels") != entries.end(),
        "custom XML properties replacement after removal output should keep owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(
        editor.reader(), output_reader, custom_xml_props_part.zip_path());
    check(output_reader.read_entry("customXml/itemProps1.xml") == restored_properties,
        "custom XML properties replacement after removal should write restored payload");
    check(output_reader.read_entry("customXml/item1.xml") == source.custom_xml,
        "custom XML properties replacement after removal should preserve custom XML item bytes");
    check(output_reader.read_entry("customXml/_rels/item1.xml.rels")
            == source.custom_xml_relationships,
        "custom XML properties replacement after removal should preserve owner relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "custom XML properties replacement after removal should restore content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "custom XML properties replacement after removal should preserve package relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "custom XML properties replacement after removal should preserve unknown bytes");
    const auto* custom_xml_relationships =
        output_reader.relationships_for(custom_xml_part);
    check(custom_xml_relationships != nullptr,
        "custom XML properties replacement after removal should keep owner relationships readable");
    check(custom_xml_relationships->find_by_id("rIdCustomXmlProps") != nullptr,
        "custom XML properties replacement after removal should keep customXmlProps relationship");
    check(output_reader.content_types().override_for(custom_xml_props_part) != nullptr,
        "custom XML properties replacement after removal should restore content type override");
}

void test_package_editor_custom_xml_properties_removal_overrides_prior_replacement()
{
    const CustomXmlSourcePackage source =
        write_custom_xml_source_package("fastxlsx-package-editor-remove-after-replace-custom-xml-props-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-custom-xml-props-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName custom_xml_part("/customXml/item1.xml");
    const fastxlsx::detail::PartName custom_xml_props_part("/customXml/itemProps1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string stale_properties =
        R"(<ds:datastoreItem ds:itemID="{cccccccc-dddd-eeee-ffff-111111111111}" xmlns:ds="http://schemas.openxmlformats.org/officeDocument/2006/customXml">)"
        R"(<ds:schemaRefs><ds:schemaRef ds:uri="urn:fastxlsx:stale"/></ds:schemaRefs>)"
        R"(</ds:datastoreItem>)";
    replace_part_with_memory_chunks(editor, custom_xml_props_part, stale_properties,
        "prior custom XML properties replacement before removal");
    const auto* prior_props_plan =
        editor.edit_plan().find_part(custom_xml_props_part);
    check(prior_props_plan != nullptr,
        "setup should record active custom XML properties replacement before removal");
    check(prior_props_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "setup custom XML properties replacement should be stream-rewrite before removal");

    editor.remove_part(custom_xml_props_part,
        "explicit custom XML properties removal after replacement");

    check(editor.edit_plan().find_part(custom_xml_props_part) == nullptr,
        "custom XML properties removal after replacement should clear active replacement entry");
    const auto* removed_props =
        editor.edit_plan().find_removed_part(custom_xml_props_part);
    check(removed_props != nullptr,
        "custom XML properties removal after replacement should record removed-part audit");
    check(removed_props->reason.find("after replacement") != std::string::npos,
        "custom XML properties removal after replacement should keep final reason");
    check(removed_props->reason.find("inbound relationship preserved")
            != std::string::npos,
        "custom XML properties removal after replacement should keep inbound audit");
    check(removed_props->inbound_relationships.size() == 1,
        "custom XML properties removal after replacement should keep structured inbound audit");
    check(removed_props->inbound_relationships.front().target_part
            == custom_xml_props_part,
        "custom XML properties removal after replacement should keep normalized target");
    check(editor.manifest().find_part(custom_xml_props_part) == nullptr,
        "custom XML properties removal after replacement should remove manifest part");
    check(editor.manifest().content_types().override_for(custom_xml_props_part) == nullptr,
        "custom XML properties removal after replacement should remove content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "custom XML properties removal after replacement should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "custom XML properties removal after replacement content types audit should be rewrite");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "custom XML properties removal after replacement should not rewrite package relationships");
    check(editor.edit_plan().find_removed_package_entry("customXml/_rels/item1.xml.rels")
            == nullptr,
        "custom XML properties removal after replacement should not omit inbound owner relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties removal after replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(custom_xml_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties removal after replacement should keep custom XML item copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties removal after replacement should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "custom XML properties removal after replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "custom XML properties removal after replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "custom XML properties removal after replacement output plan should not invent dependency audits");
    check_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "custom XML properties removal after replacement output plan should omit properties part");
    check_output_entry_part_context(output_plan.entries, "customXml/itemProps1.xml",
        true, custom_xml_props_part.value(),
        "custom XML properties removal after replacement output plan should classify omitted properties");
    const auto* output_props_plan =
        find_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml");
    check(output_props_plan->reason.find("after replacement") != std::string::npos,
        "custom XML properties removal after replacement output plan should keep final reason");
    check(output_props_plan->inbound_relationships.size() == 1,
        "custom XML properties removal after replacement output plan should expose inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "customXml/itemProps1.xml", custom_xml_part.value(),
        "customXml/_rels/item1.xml.rels", "rIdCustomXmlProps",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXmlProps",
        "itemProps1.xml", custom_xml_props_part,
        "custom XML properties removal after replacement output plan should keep owner inbound audit");
    check(find_output_entry_plan(output_plan.entries, "customXml/_rels/itemProps1.xml.rels")
            == nullptr,
        "custom XML properties removal after replacement output plan should not invent properties owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "custom XML properties removal after replacement output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml",
        false, "",
        "custom XML properties removal after replacement output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "custom XML properties removal after replacement output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties removal after replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties removal after replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties removal after replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties removal after replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "customXml/item1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties removal after replacement output plan should preserve custom XML item");
    check_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties removal after replacement output plan should preserve item owner relationships");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties removal after replacement output plan should preserve unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("customXml/itemProps1.xml") == entries.end(),
        "custom XML properties removal after replacement output should omit properties part");
    check(entries.find("customXml/item1.xml") != entries.end(),
        "custom XML properties removal after replacement output should keep custom XML item");
    check(entries.find("customXml/_rels/item1.xml.rels") != entries.end(),
        "custom XML properties removal after replacement output should keep owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(custom_xml_props_part) == nullptr,
        "custom XML properties removal after replacement should remove output content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/customXml/itemProps1.xml",
        "custom XML properties removal after replacement content types should omit override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "custom XML properties removal after replacement should preserve package relationships bytes");
    check(output_reader.read_entry("customXml/item1.xml") == source.custom_xml,
        "custom XML properties removal after replacement should preserve custom XML item bytes");
    check(output_reader.read_entry("customXml/_rels/item1.xml.rels")
            == source.custom_xml_relationships,
        "custom XML properties removal after replacement should preserve owner relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "custom XML properties removal after replacement should preserve unknown bytes");
    const auto* custom_xml_relationships =
        output_reader.relationships_for(custom_xml_part);
    check(custom_xml_relationships != nullptr,
        "custom XML properties removal after replacement should keep owner relationships readable");
    check(custom_xml_relationships->find_by_id("rIdCustomXmlProps") != nullptr,
        "custom XML properties removal after replacement should keep inbound customXmlProps relationship");
    check(output_reader.relationships_for(custom_xml_props_part) == nullptr,
        "custom XML properties removal after replacement should not invent properties owner relationships");
}

void test_package_editor_custom_xml_item_removal_then_properties_replacement_keeps_owner_omitted()
{
    const CustomXmlSourcePackage source =
        write_custom_xml_source_package("fastxlsx-package-editor-remove-custom-xml-then-replace-props-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-custom-xml-then-replace-props-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName custom_xml_part("/customXml/item1.xml");
    const fastxlsx::detail::PartName custom_xml_props_part("/customXml/itemProps1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    editor.remove_part(custom_xml_part, "explicit custom XML item removal before properties rewrite");
    check(editor.edit_plan().find_removed_part(custom_xml_part) != nullptr,
        "setup should record removed custom XML item before properties rewrite");
    check(editor.edit_plan().find_removed_package_entry("customXml/_rels/item1.xml.rels")
            != nullptr,
        "setup should omit custom XML item owner relationships before properties rewrite");

    const std::string replacement_properties =
        R"(<ds:datastoreItem ds:itemID="{dddddddd-eeee-ffff-1111-222222222222}" xmlns:ds="http://schemas.openxmlformats.org/officeDocument/2006/customXml">)"
        R"(<ds:schemaRefs><ds:schemaRef ds:uri="urn:fastxlsx:properties-after-item-removal"/></ds:schemaRefs>)"
        R"(</ds:datastoreItem>)";
    replace_part_with_memory_chunks(editor, custom_xml_props_part, replacement_properties,
        "custom XML properties replacement after item removal");

    check(editor.edit_plan().find_removed_part(custom_xml_part) != nullptr,
        "properties replacement after item removal should keep removed custom XML item audit");
    check(editor.edit_plan().find_removed_package_entry("customXml/_rels/item1.xml.rels")
            != nullptr,
        "properties replacement after item removal should keep omitted owner relationships");
    check(editor.edit_plan().find_package_entry("customXml/_rels/item1.xml.rels") == nullptr,
        "properties replacement after item removal should not revive owner relationships audit");
    const auto* properties_plan =
        editor.edit_plan().find_part(custom_xml_props_part);
    check(properties_plan != nullptr,
        "properties replacement after item removal should keep active properties part");
    check(properties_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "properties replacement after item removal should keep final properties stream-rewrite mode");
    check(properties_plan->reason.find("after item removal") != std::string::npos,
        "properties replacement after item removal should keep final reason");
    check(editor.manifest().find_part(custom_xml_part) == nullptr,
        "properties replacement after item removal should keep custom XML item removed");
    check_manifest_write_mode(editor, custom_xml_props_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "properties replacement after item removal should mark properties part rewritten");
    check(editor.manifest().content_types().override_for(custom_xml_props_part) != nullptr,
        "properties replacement after item removal should keep properties content type override");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "properties replacement after item removal should not rewrite content types");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "properties replacement after item removal should not rewrite package relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "properties replacement after item removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "properties replacement after item removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "properties replacement after item removal should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "properties replacement after item removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "properties replacement after item removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "properties replacement after item removal output plan should not invent dependency audits");
    check(output_plan.removed_parts.size() == 1,
        "properties replacement after item removal output plan should expose one removed part");
    check(output_plan.removed_parts.front().part_name == custom_xml_part,
        "properties replacement after item removal output plan should expose removed custom XML item");
    check(output_plan.removed_parts.front().reason.find("before properties rewrite")
            != std::string::npos,
        "properties replacement after item removal output plan should keep removed item reason");
    check(output_plan.removed_parts.front().inbound_relationships.size() == 1,
        "properties replacement after item removal output plan should expose removed item inbound audit");
    check(output_plan.removed_package_entries.size() == 1,
        "properties replacement after item removal output plan should expose owner relationships omission");
    check(output_plan.removed_package_entries.front().entry_name
            == "customXml/_rels/item1.xml.rels",
        "properties replacement after item removal output plan should omit item owner relationships");
    check(output_plan.removed_package_entries.front().audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "properties replacement after item removal output plan should classify omitted owner relationships");
    check(output_plan.removed_package_entries.front().owner_part == custom_xml_part.value(),
        "properties replacement after item removal output plan should keep omitted owner context");
    check_output_entry_plan(output_plan.entries, "customXml/item1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "properties replacement after item removal output plan should omit custom XML item");
    check_output_entry_part_context(output_plan.entries, "customXml/item1.xml",
        true, custom_xml_part.value(),
        "properties replacement after item removal output plan should classify omitted custom XML item");
    const auto* output_custom_xml_plan =
        find_output_entry_plan(output_plan.entries, "customXml/item1.xml");
    check(output_custom_xml_plan->reason.find("before properties rewrite") != std::string::npos,
        "properties replacement after item removal output plan should keep item removal reason");
    check(output_custom_xml_plan->inbound_relationships.size() == 1,
        "properties replacement after item removal output plan should expose package inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "customXml/item1.xml", "", "_rels/.rels", "rIdCustomXml",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXml",
        "customXml/item1.xml", custom_xml_part,
        "properties replacement after item removal output plan should keep package inbound audit");
    check_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "properties replacement after item removal output plan should omit owner relationships");
    check_output_entry_part_context(output_plan.entries, "customXml/_rels/item1.xml.rels",
        false, "",
        "properties replacement after item removal output plan should classify owner relationships as metadata");
    const auto* output_custom_xml_relationships_plan =
        find_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels");
    check(output_custom_xml_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "properties replacement after item removal output plan should classify owner relationships metadata");
    check(output_custom_xml_relationships_plan->owner_part == custom_xml_part.value(),
        "properties replacement after item removal output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "properties replacement after item removal output plan should rewrite properties part");
    check_output_entry_part_context(output_plan.entries, "customXml/itemProps1.xml",
        true, custom_xml_props_part.value(),
        "properties replacement after item removal output plan should classify rewritten properties");
    const auto* output_props_plan =
        find_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml");
    check(output_props_plan->reason.find("after item removal") != std::string::npos,
        "properties replacement after item removal output plan should keep properties replacement reason");
    check(find_output_entry_plan(output_plan.entries, "customXml/_rels/itemProps1.xml.rels")
            == nullptr,
        "properties replacement after item removal output plan should not invent properties owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "properties replacement after item removal output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "properties replacement after item removal output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "properties replacement after item removal output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "properties replacement after item removal output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "properties replacement after item removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "properties replacement after item removal output plan should preserve unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("customXml/item1.xml") == entries.end(),
        "properties replacement after item removal output should omit custom XML item");
    check(entries.find("customXml/_rels/item1.xml.rels") == entries.end(),
        "properties replacement after item removal output should keep owner relationships omitted");
    check(entries.find("customXml/itemProps1.xml") != entries.end(),
        "properties replacement after item removal output should keep properties part");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("customXml/itemProps1.xml") == replacement_properties,
        "properties replacement after item removal should write replacement properties payload");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "properties replacement after item removal should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "properties replacement after item removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "properties replacement after item removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "properties replacement after item removal should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "properties replacement after item removal should preserve unknown bytes");

    const auto* package_custom_xml_relationship =
        output_reader.package_relationships().find_by_id("rIdCustomXml");
    check(package_custom_xml_relationship != nullptr,
        "properties replacement after item removal should keep package customXml relationship");
    check(package_custom_xml_relationship->target == "customXml/item1.xml",
        "properties replacement after item removal should not rewrite package customXml target");
    check(output_reader.relationships_for(custom_xml_part) == nullptr,
        "properties replacement after item removal should not revive owner relationships");
    check(output_reader.relationships_for(custom_xml_props_part) == nullptr,
        "properties replacement after item removal should not invent properties owner relationships");
    const auto* custom_xml_content_type =
        output_reader.content_types().content_type_for(custom_xml_part);
    check(custom_xml_content_type != nullptr && *custom_xml_content_type == "application/xml",
        "properties replacement after item removal should preserve default XML content type");
    check(output_reader.content_types().override_for(custom_xml_props_part) != nullptr,
        "properties replacement after item removal should keep properties content type override");
    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.package_relationships().find_by_id("rIdCustomXml") != nullptr,
        "relationship graph should keep package customXml relationship after properties rewrite");
    check(output_graph.relationships_for(custom_xml_part) == nullptr,
        "relationship graph should not attach owner relationships after properties rewrite");
}

void test_package_editor_custom_xml_properties_removal_then_item_replacement_keeps_properties_omitted()
{
    const CustomXmlSourcePackage source =
        write_custom_xml_source_package("fastxlsx-package-editor-remove-props-then-replace-custom-xml-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-props-then-replace-custom-xml-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName custom_xml_part("/customXml/item1.xml");
    const fastxlsx::detail::PartName custom_xml_props_part("/customXml/itemProps1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    editor.remove_part(custom_xml_props_part,
        "explicit custom XML properties removal before item rewrite");
    check(editor.edit_plan().find_removed_part(custom_xml_props_part) != nullptr,
        "setup should record removed custom XML properties before item rewrite");
    check(editor.manifest().find_part(custom_xml_props_part) == nullptr,
        "setup should remove custom XML properties from manifest before item rewrite");
    check(editor.edit_plan().find_removed_package_entry("customXml/_rels/item1.xml.rels")
            == nullptr,
        "setup should keep custom XML item owner relationships before item rewrite");

    const std::string replacement_custom_xml =
        R"(<fx:payload xmlns:fx="urn:fastxlsx:test-custom-xml">)"
        R"(<fx:value>Custom XML replacement after properties removal</fx:value>)"
        R"(</fx:payload>)";
    replace_part_with_memory_chunks(editor, custom_xml_part, replacement_custom_xml,
        "custom XML item replacement after properties removal");

    check(editor.edit_plan().find_removed_part(custom_xml_props_part) != nullptr,
        "item replacement after properties removal should keep removed properties audit");
    check(editor.edit_plan().find_part(custom_xml_props_part) == nullptr,
        "item replacement after properties removal should not restore active properties part");
    check(editor.manifest().find_part(custom_xml_props_part) == nullptr,
        "item replacement after properties removal should keep properties part removed");
    check(editor.manifest().content_types().override_for(custom_xml_props_part) == nullptr,
        "item replacement after properties removal should keep properties content type removed");
    const auto* custom_xml_plan = editor.edit_plan().find_part(custom_xml_part);
    check(custom_xml_plan != nullptr,
        "item replacement after properties removal should keep active custom XML item");
    check(custom_xml_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "item replacement after properties removal should keep final item write mode");
    check(custom_xml_plan->reason.find("after properties removal") != std::string::npos,
        "item replacement after properties removal should keep final reason");
    check_manifest_write_mode(editor, custom_xml_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "item replacement after properties removal should mark item rewritten");
    const auto* owner_relationships_entry =
        editor.edit_plan().find_package_entry("customXml/_rels/item1.xml.rels");
    check(owner_relationships_entry != nullptr,
        "item replacement after properties removal should keep owner relationships audit");
    check(owner_relationships_entry->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "item replacement after properties removal owner relationships should be copy-original");
    check(editor.edit_plan().find_removed_package_entry("customXml/_rels/item1.xml.rels")
            == nullptr,
        "item replacement after properties removal should not omit owner relationships");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "item replacement after properties removal should keep content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "item replacement after properties removal content types audit should be rewrite");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "item replacement after properties removal should not rewrite package relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "item replacement after properties removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "item replacement after properties removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "item replacement after properties removal should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "item replacement after properties removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "item replacement after properties removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "item replacement after properties removal output plan should not invent dependency audits");
    check(output_plan.removed_parts.size() == 1,
        "item replacement after properties removal output plan should expose one removed part");
    check(output_plan.removed_parts.front().part_name == custom_xml_props_part,
        "item replacement after properties removal output plan should expose removed properties part");
    check(output_plan.removed_parts.front().reason.find("before item rewrite")
            != std::string::npos,
        "item replacement after properties removal output plan should keep removed properties reason");
    check(output_plan.removed_parts.front().inbound_relationships.size() == 1,
        "item replacement after properties removal output plan should expose removed properties inbound audit");
    check(output_plan.removed_package_entries.empty(),
        "item replacement after properties removal output plan should not omit owner relationships");
    check_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "item replacement after properties removal output plan should omit properties part");
    check_output_entry_part_context(output_plan.entries, "customXml/itemProps1.xml",
        true, custom_xml_props_part.value(),
        "item replacement after properties removal output plan should classify omitted properties");
    const auto* output_props_plan =
        find_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml");
    check(output_props_plan->reason.find("before item rewrite") != std::string::npos,
        "item replacement after properties removal output plan should keep properties removal reason");
    check(output_props_plan->inbound_relationships.size() == 1,
        "item replacement after properties removal output plan should expose owner inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "customXml/itemProps1.xml", custom_xml_part.value(),
        "customXml/_rels/item1.xml.rels", "rIdCustomXmlProps",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXmlProps",
        "itemProps1.xml", custom_xml_props_part,
        "item replacement after properties removal output plan should keep owner inbound audit");
    check(find_output_entry_plan(output_plan.entries, "customXml/_rels/itemProps1.xml.rels")
            == nullptr,
        "item replacement after properties removal output plan should not invent properties owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "item replacement after properties removal output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml",
        false, "",
        "item replacement after properties removal output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "item replacement after properties removal output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "customXml/item1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "item replacement after properties removal output plan should rewrite custom XML item");
    check_output_entry_part_context(output_plan.entries, "customXml/item1.xml",
        true, custom_xml_part.value(),
        "item replacement after properties removal output plan should classify rewritten custom XML item");
    const auto* output_custom_xml_plan =
        find_output_entry_plan(output_plan.entries, "customXml/item1.xml");
    check(output_custom_xml_plan->reason.find("after properties removal") != std::string::npos,
        "item replacement after properties removal output plan should keep item replacement reason");
    check_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "item replacement after properties removal output plan should preserve item owner relationships");
    check_output_entry_part_context(output_plan.entries, "customXml/_rels/item1.xml.rels",
        false, "",
        "item replacement after properties removal output plan should classify owner relationships as metadata");
    const auto* output_custom_xml_relationships_plan =
        find_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels");
    check(output_custom_xml_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "item replacement after properties removal output plan should classify owner relationships metadata");
    check(output_custom_xml_relationships_plan->owner_part == custom_xml_part.value(),
        "item replacement after properties removal output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "item replacement after properties removal output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "item replacement after properties removal output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "item replacement after properties removal output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "item replacement after properties removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "item replacement after properties removal output plan should preserve unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("customXml/item1.xml") != entries.end(),
        "item replacement after properties removal output should keep custom XML item");
    check(entries.find("customXml/_rels/item1.xml.rels") != entries.end(),
        "item replacement after properties removal output should keep owner relationships");
    check(entries.find("customXml/itemProps1.xml") == entries.end(),
        "item replacement after properties removal output should omit properties part");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("customXml/item1.xml") == replacement_custom_xml,
        "item replacement after properties removal should write replacement item payload");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "item replacement after properties removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "item replacement after properties removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "item replacement after properties removal should preserve worksheet bytes");
    check(output_reader.read_entry("customXml/_rels/item1.xml.rels")
            == source.custom_xml_relationships,
        "item replacement after properties removal should preserve owner relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "item replacement after properties removal should preserve unknown bytes");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/customXml/itemProps1.xml",
        "item replacement after properties removal content types should omit properties override");
    check(output_reader.content_types().override_for(custom_xml_props_part) == nullptr,
        "item replacement after properties removal should not restore properties content type");
    const auto* package_custom_xml_relationship =
        output_reader.package_relationships().find_by_id("rIdCustomXml");
    check(package_custom_xml_relationship != nullptr,
        "item replacement after properties removal should keep package customXml relationship");
    check(package_custom_xml_relationship->target == "customXml/item1.xml",
        "item replacement after properties removal should not rewrite package customXml target");
    const auto* custom_xml_relationships =
        output_reader.relationships_for(custom_xml_part);
    check(custom_xml_relationships != nullptr,
        "item replacement after properties removal should keep owner relationships readable");
    const auto* custom_xml_props_relationship =
        custom_xml_relationships->find_by_id("rIdCustomXmlProps");
    check(custom_xml_props_relationship != nullptr,
        "item replacement after properties removal should keep inbound customXmlProps relationship");
    check(custom_xml_props_relationship->target == "itemProps1.xml",
        "item replacement after properties removal should not rewrite customXmlProps target");
    check(output_reader.relationships_for(custom_xml_props_part) == nullptr,
        "item replacement after properties removal should not invent properties owner relationships");
    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.package_relationships().find_by_id("rIdCustomXml") != nullptr,
        "relationship graph should keep package customXml relationship after item rewrite");
    check(output_graph.relationships_for(custom_xml_part)->find_by_id("rIdCustomXmlProps")
            != nullptr,
        "relationship graph should keep customXmlProps relationship after item rewrite");
}


} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor preservation shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "preservation-linked-custom-xml")) {
            test_package_editor_worksheet_rewrite_preserves_custom_xml_parts();
            test_package_editor_replaces_custom_xml_and_preserves_package_links();
            test_package_editor_repeated_custom_xml_replacement_updates_final_state();
            test_package_editor_removes_custom_xml_and_preserves_package_links();
            test_package_editor_custom_xml_replacement_restores_prior_removal();
            test_package_editor_custom_xml_removal_overrides_prior_replacement();
            test_package_editor_replaces_custom_xml_properties_and_preserves_owner_links();
            test_package_editor_repeated_custom_xml_properties_replacement_updates_final_state();
            test_package_editor_removes_custom_xml_properties_and_preserves_owner_links();
            test_package_editor_custom_xml_properties_replacement_restores_prior_removal();
            test_package_editor_custom_xml_properties_removal_overrides_prior_replacement();
            test_package_editor_custom_xml_item_removal_then_properties_replacement_keeps_owner_omitted();
            test_package_editor_custom_xml_properties_removal_then_item_replacement_keeps_properties_omitted();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
