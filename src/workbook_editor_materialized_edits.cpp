#include "workbook_editor_materialized_edits.hpp"

#include "package_editor.hpp"

#include <fastxlsx/workbook.hpp>

#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace fastxlsx::detail {

namespace {

using SharedStringIndexMap = std::map<std::string, std::uint32_t, std::less<>>;

struct MaterializedSharedStringsProjectionPlan {
    std::string part_name;
    std::shared_ptr<const CellStoreSharedStringIndexProvider> index_provider;
    std::optional<std::string> replacement_xml;
};

std::optional<MaterializedSharedStringsProjectionPlan>
try_plan_materialized_shared_strings_projection(
    const PackageReader& reader,
    const MaterializedWorksheetSessionRegistry& materialized_sessions)
{
    std::optional<WorkbookSharedStringsSnapshot> snapshot;
    try {
        snapshot = load_workbook_shared_strings_snapshot(reader);
        if (!snapshot.has_value()) {
            return std::nullopt;
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }

    if (snapshot->strings.size()
        > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return std::nullopt;
    }

    SharedStringIndexMap index_by_text;
    std::uint32_t next_index = 0;
    for (const std::string& value : snapshot->strings) {
        index_by_text.emplace(value, next_index);
        ++next_index;
    }

    const std::size_t source_string_count = snapshot->strings.size();
    std::vector<std::string> appended_strings;
    std::size_t appended_reference_count = 0;
    bool saw_text_record = false;
    for (const auto& [_, session] : materialized_sessions.sessions()) {
        if (!session.dirty()) {
            continue;
        }
        for (const auto& [position, record] : session.store().records()) {
            (void)position;
            if (record.kind != CellValueKind::Text) {
                continue;
            }
            saw_text_record = true;
            auto existing = index_by_text.find(record.text_value);
            if (existing == index_by_text.end()) {
                if (next_index == std::numeric_limits<std::uint32_t>::max()) {
                    return std::nullopt;
                }
                const std::uint32_t new_index = next_index;
                ++next_index;
                index_by_text.emplace(record.text_value, new_index);
                appended_strings.push_back(record.text_value);
                ++appended_reference_count;
                continue;
            }
            if (existing->second >= source_string_count) {
                ++appended_reference_count;
            }
        }
    }

    if (!saw_text_record) {
        return std::nullopt;
    }

    std::optional<std::string> replacement_xml =
        try_build_shared_strings_append_xml(
            *snapshot, appended_strings, appended_reference_count);
    if (!replacement_xml.has_value()) {
        return std::nullopt;
    }

    auto index_map = std::make_shared<SharedStringIndexMap>(std::move(index_by_text));
    auto index_provider = std::make_shared<CellStoreSharedStringIndexProvider>(
        [index_map = std::move(index_map)](std::string_view text) -> std::uint32_t {
            const auto found = index_map->find(text);
            if (found == index_map->end()) {
                throw FastXlsxError(
                    "materialized worksheet sharedStrings projection missing text index");
            }
            return found->second;
        });

    MaterializedSharedStringsProjectionPlan plan;
    plan.part_name = snapshot->part_name;
    plan.index_provider = std::move(index_provider);
    if (*replacement_xml != snapshot->xml) {
        plan.replacement_xml = std::move(*replacement_xml);
    }
    return plan;
}

} // namespace

std::vector<std::string> workbook_editor_pending_materialized_worksheet_names(
    const WorkbookEditorSheetCatalogPlan& sheet_catalog,
    const MaterializedWorksheetSessionRegistry& materialized_sessions)
{
    std::vector<std::string> names;
    for (const std::string& sheet_name : sheet_catalog.current_names()) {
        const MaterializedWorksheetSession* session =
            materialized_sessions.try_session(sheet_name);
        if (session != nullptr && session->dirty()) {
            names.push_back(sheet_name);
        }
    }
    return names;
}

void validate_workbook_editor_materialized_flush_targets(
    const WorkbookEditorSheetCatalogPlan& sheet_catalog,
    const std::vector<MaterializedWorksheetSheetDataProjection>& projections)
{
    for (const MaterializedWorksheetSheetDataProjection& projection : projections) {
        if (!sheet_catalog.has_current(projection.planned_name)) {
            throw FastXlsxError(
                workbook_editor_missing_planned_sheet_message(projection.planned_name));
        }
    }
}

WorkbookEditorMaterializedFlushResult
flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
    PackageEditor& editor,
    MaterializedWorksheetSessionRegistry& materialized_sessions,
    const WorkbookEditorSheetCatalogPlan& sheet_catalog)
{
    std::optional<MaterializedSharedStringsProjectionPlan> shared_strings_projection =
        try_plan_materialized_shared_strings_projection(editor.reader(), materialized_sessions);
    const std::vector<MaterializedWorksheetSheetDataProjection> projections =
        materialized_sessions.dirty_sheet_data_chunk_sources(
            shared_strings_projection.has_value()
                ? shared_strings_projection->index_provider
                : std::shared_ptr<const CellStoreSharedStringIndexProvider> {});
    validate_workbook_editor_materialized_flush_targets(sheet_catalog, projections);

    WorkbookEditorMaterializedFlushResult result;
    for (const MaterializedWorksheetSheetDataProjection& projection : projections) {
        editor.replace_worksheet_sheet_data_from_chunk_source_by_name(
            projection.planned_name, projection.read_next_chunk, {},
            std::string_view(projection.dimension_reference));

        MaterializedWorksheetSession* session =
            materialized_sessions.try_session(projection.planned_name);
        if (session != nullptr) {
            session->clear_dirty();
        }
        ++result.flushed_worksheet_count;
    }

    if (shared_strings_projection.has_value()
        && shared_strings_projection->replacement_xml.has_value()) {
        std::vector<PackageEntryChunk> chunks;
        chunks.push_back(
            PackageEntryChunk::memory(std::move(*shared_strings_projection->replacement_xml)));
        editor.replace_part_chunks(PartName(shared_strings_projection->part_name),
            std::move(chunks),
            "WorkbookEditor materialized worksheet sharedStrings append");
    }
    return result;
}

} // namespace fastxlsx::detail
