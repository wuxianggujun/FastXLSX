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

#ifndef FASTXLSX_PACKAGE_EDITOR_SHEETDATA_INCLUDE_CORE_TESTS
#define FASTXLSX_PACKAGE_EDITOR_SHEETDATA_INCLUDE_CORE_TESTS 1
#endif

#ifndef FASTXLSX_PACKAGE_EDITOR_SHEETDATA_INCLUDE_BY_NAME_TESTS
#define FASTXLSX_PACKAGE_EDITOR_SHEETDATA_INCLUDE_BY_NAME_TESTS 0
#endif

#ifndef FASTXLSX_PACKAGE_EDITOR_SHEETDATA_INCLUDE_PLANNED_CATALOG_TESTS
#define FASTXLSX_PACKAGE_EDITOR_SHEETDATA_INCLUDE_PLANNED_CATALOG_TESTS 0
#endif

#ifndef FASTXLSX_PACKAGE_EDITOR_SHEETDATA_SHARD_NAME
#define FASTXLSX_PACKAGE_EDITOR_SHEETDATA_SHARD_NAME "sheetdata"
#endif

#ifndef FASTXLSX_PACKAGE_EDITOR_SHEETDATA_RUN_TESTS
#define FASTXLSX_PACKAGE_EDITOR_SHEETDATA_RUN_TESTS()                                      \
    do {                                                                                   \
        test_package_editor_replaces_worksheet_sheet_data_and_preserves_metadata();         \
        test_package_editor_sheet_data_patch_without_calc_chain_keeps_relationship_metadata_copy_original(); \
        test_package_editor_patches_fastxlsx_writer_sheet_data_roundtrip();                 \
        test_package_editor_controlled_template_fill_fixture_uses_bounded_sheet_data_patch(); \
        test_package_editor_patches_writer_sheet_data_and_preserves_unknown_entry();        \
    } while (false)
#endif

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
    return shard == "all" || shard == "sheetdata"
        || shard == "sheetdata-by-name"
        || shard == "sheetdata-planned-catalog";
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

#if FASTXLSX_PACKAGE_EDITOR_SHEETDATA_INCLUDE_CORE_TESTS
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
#endif

#if FASTXLSX_PACKAGE_EDITOR_SHEETDATA_INCLUDE_BY_NAME_TESTS
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
#endif

#if FASTXLSX_PACKAGE_EDITOR_SHEETDATA_INCLUDE_CORE_TESTS
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
#endif

#if FASTXLSX_PACKAGE_EDITOR_SHEETDATA_INCLUDE_BY_NAME_TESTS
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
#endif

#if FASTXLSX_PACKAGE_EDITOR_SHEETDATA_INCLUDE_CORE_TESTS
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
#endif

#if FASTXLSX_PACKAGE_EDITOR_SHEETDATA_INCLUDE_PLANNED_CATALOG_TESTS
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
#endif


} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor sheetdata shard: " << shard << '\n';

        if (should_run_package_editor_shard(
                shard, FASTXLSX_PACKAGE_EDITOR_SHEETDATA_SHARD_NAME)) {
            FASTXLSX_PACKAGE_EDITOR_SHEETDATA_RUN_TESTS();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
