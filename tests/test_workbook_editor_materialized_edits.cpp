#include "../src/workbook_editor_materialized_edits.hpp"

#include <fastxlsx/cell_value.hpp>
#include <fastxlsx/detail/cell_store.hpp>
#include <fastxlsx/workbook.hpp>

#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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

void check_names(
    const std::vector<std::string>& actual,
    const std::vector<std::string>& expected,
    const char* message)
{
    if (actual != expected) {
        throw TestFailure(message);
    }
}

bool throws_fastxlsx_error(auto&& callable)
{
    try {
        callable();
    } catch (const fastxlsx::FastXlsxError&) {
        return true;
    }
    return false;
}

std::string read_all_chunks(auto& read_next_chunk)
{
    std::string xml;
    std::string chunk;
    while (read_next_chunk(chunk)) {
        xml += chunk;
    }
    return xml;
}

fastxlsx::detail::MaterializedWorksheetSession& materialize_session(
    fastxlsx::detail::MaterializedWorksheetSessionRegistry& registry,
    std::string planned_name)
{
    fastxlsx::detail::CellStore store;
    return registry.materialize(std::move(planned_name), std::move(store));
}

void test_pending_materialized_names_follow_current_catalog_order()
{
    fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data", "Other", "Clean"});
    catalog.record_rename("Data", "Renamed");

    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    materialize_session(registry, "Clean");
    materialize_session(registry, "Other")
        .set_cell(1, 1, fastxlsx::CellValue::number(2.0));
    materialize_session(registry, "Renamed")
        .set_cell(1, 1, fastxlsx::CellValue::text("dirty"));

    check_names(
        fastxlsx::detail::workbook_editor_pending_materialized_worksheet_names(
            catalog, registry),
        {"Renamed", "Other"},
        "pending materialized names should follow current planned catalog order");
}

void test_pending_materialized_names_ignore_stale_source_names()
{
    fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data", "Other"});
    catalog.record_rename("Data", "Renamed");

    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    materialize_session(registry, "Data")
        .set_cell(1, 1, fastxlsx::CellValue::text("stale-source-dirty"));
    materialize_session(registry, "Renamed")
        .set_cell(1, 1, fastxlsx::CellValue::text("current-dirty"));
    materialize_session(registry, "Other")
        .set_cell(1, 1, fastxlsx::CellValue::number(3.0));

    check_names(
        fastxlsx::detail::workbook_editor_pending_materialized_worksheet_names(
            catalog, registry),
        {"Renamed", "Other"},
        "pending materialized names should ignore dirty sessions outside current catalog");
}

void test_dirty_sheet_data_projections_include_only_dirty_sessions_and_dimensions()
{
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    materialize_session(registry, "Clean");
    materialize_session(registry, "Data")
        .set_cell(2, 2, fastxlsx::CellValue::text("dirty-b2"));
    fastxlsx::detail::MaterializedWorksheetSession& data =
        *registry.try_session("Data");
    data.set_cell(4, 4, fastxlsx::CellValue::number(4.0));
    materialize_session(registry, "Alpha")
        .set_cell(1, 1, fastxlsx::CellValue::boolean(true));

    const std::vector<fastxlsx::detail::MaterializedWorksheetSheetDataProjection>
        projections = registry.dirty_sheet_data_chunk_sources();

    check(projections.size() == 2,
        "dirty sheetData projections should include only dirty materialized sessions");
    check(projections[0].planned_name == "Alpha" &&
            projections[1].planned_name == "Data",
        "dirty sheetData projections should follow registry planned-name order");
    check(projections[0].dimension_reference == "A1",
        "dirty sheetData projection should expose single-cell dimensions");
    check(projections[1].dimension_reference == "B2:D4",
        "dirty sheetData projection should expose sparse multi-cell dimensions");

    auto alpha_read_next_chunk = projections[0].read_next_chunk;
    const std::string alpha_xml = read_all_chunks(alpha_read_next_chunk);
    check(alpha_xml.find("<worksheet") == std::string::npos &&
            alpha_xml.find("<dimension") == std::string::npos,
        "dirty sheetData projection should emit sheetData only");
    check(alpha_xml.find(R"(<c r="A1" t="b"><v>1</v></c>)") != std::string::npos,
        "dirty sheetData projection should include boolean cells");

    auto data_read_next_chunk = projections[1].read_next_chunk;
    const std::string data_xml = read_all_chunks(data_read_next_chunk);
    check(data_xml.find(R"(<c r="B2" t="inlineStr"><is><t>dirty-b2</t></is></c>)")
            != std::string::npos,
        "dirty sheetData projection should include text cells");
    check(data_xml.find(R"(<c r="D4"><v>4</v></c>)") != std::string::npos,
        "dirty sheetData projection should include numeric cells");
    check(data_xml.find("Clean") == std::string::npos,
        "dirty sheetData projection should not include clean session data");
}

void test_dirty_sheet_data_projection_uses_shared_string_index_provider()
{
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    materialize_session(registry, "Data")
        .set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    fastxlsx::detail::MaterializedWorksheetSession& data =
        *registry.try_session("Data");
    data.set_cell(1, 2, fastxlsx::CellValue::text("appended"));
    data.set_cell(2, 1, fastxlsx::CellValue::number(3.0));

    auto shared_string_index_provider =
        std::make_shared<fastxlsx::detail::CellStoreSharedStringIndexProvider>(
            [](std::string_view text) -> std::uint32_t {
                if (text == "existing") {
                    return 0;
                }
                if (text == "appended") {
                    return 5;
                }
                throw fastxlsx::FastXlsxError(
                    "unexpected shared string text in materialized test");
            });

    const std::vector<fastxlsx::detail::MaterializedWorksheetSheetDataProjection>
        projections =
            registry.dirty_sheet_data_chunk_sources(shared_string_index_provider);

    check(projections.size() == 1,
        "shared-string sheetData projection should include dirty session");
    check(projections[0].planned_name == "Data",
        "shared-string sheetData projection should preserve planned name");
    check(projections[0].dimension_reference == "A1:B2",
        "shared-string sheetData projection should expose sparse dimensions");

    auto read_next_chunk = projections[0].read_next_chunk;
    const std::string xml = read_all_chunks(read_next_chunk);
    check(xml.find(R"(<c r="A1" t="s"><v>0</v></c>)") != std::string::npos,
        "shared-string sheetData projection should reuse source string indexes");
    check(xml.find(R"(<c r="B1" t="s"><v>5</v></c>)") != std::string::npos,
        "shared-string sheetData projection should emit appended string indexes");
    check(xml.find(R"(<c r="A2"><v>3</v></c>)") != std::string::npos,
        "shared-string sheetData projection should leave numeric cells value-only");
    check(xml.find("inlineStr") == std::string::npos,
        "shared-string sheetData projection should not emit inline strings");
}

void test_dirty_worksheet_projection_uses_shared_string_index_provider()
{
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    materialize_session(registry, "Data")
        .set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    fastxlsx::detail::MaterializedWorksheetSession& data =
        *registry.try_session("Data");
    data.set_cell(3, 2, fastxlsx::CellValue::text("appended"));
    data.set_cell(3, 3, fastxlsx::CellValue::number(9.0));

    auto shared_string_index_provider =
        std::make_shared<fastxlsx::detail::CellStoreSharedStringIndexProvider>(
            [](std::string_view text) -> std::uint32_t {
                if (text == "existing") {
                    return 0;
                }
                if (text == "appended") {
                    return 7;
                }
                throw fastxlsx::FastXlsxError(
                    "unexpected shared string text in worksheet projection test");
            });

    const std::vector<fastxlsx::detail::MaterializedWorksheetProjection>
        projections =
            registry.dirty_worksheet_chunk_sources(shared_string_index_provider);

    check(projections.size() == 1,
        "shared-string worksheet projection should include dirty session");
    check(projections[0].planned_name == "Data",
        "shared-string worksheet projection should preserve planned name");

    auto read_next_chunk = projections[0].read_next_chunk;
    const std::string xml = read_all_chunks(read_next_chunk);
    check(xml.find(R"(<?xml version="1.0" encoding="UTF-8"?>)")
            != std::string::npos,
        "shared-string worksheet projection should emit XML declaration");
    check(xml.find(R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)")
            != std::string::npos,
        "shared-string worksheet projection should emit worksheet root");
    check(xml.find(R"(<dimension ref="A1:C3"/>)") != std::string::npos,
        "shared-string worksheet projection should emit sparse dimensions");
    check(xml.find(R"(<c r="A1" t="s"><v>0</v></c>)") != std::string::npos,
        "shared-string worksheet projection should reuse source string indexes");
    check(xml.find(R"(<c r="B3" t="s"><v>7</v></c>)") != std::string::npos,
        "shared-string worksheet projection should emit appended string indexes");
    check(xml.find(R"(<c r="C3"><v>9</v></c>)") != std::string::npos,
        "shared-string worksheet projection should leave numeric cells value-only");
    check(xml.find("inlineStr") == std::string::npos,
        "shared-string worksheet projection should not emit inline strings");
}

void test_dirty_sheet_data_projection_provider_failure_keeps_session_dirty()
{
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    materialize_session(registry, "Data")
        .set_cell(1, 1, fastxlsx::CellValue::text("known"));
    fastxlsx::detail::MaterializedWorksheetSession& data =
        *registry.try_session("Data");
    data.set_cell(1, 2, fastxlsx::CellValue::text("missing"));

    auto shared_string_index_provider =
        std::make_shared<fastxlsx::detail::CellStoreSharedStringIndexProvider>(
            [](std::string_view text) -> std::uint32_t {
                if (text == "known") {
                    return 0;
                }
                throw fastxlsx::FastXlsxError(
                    "missing shared string index in sheetData projection test");
            });

    const std::vector<fastxlsx::detail::MaterializedWorksheetSheetDataProjection>
        projections =
            registry.dirty_sheet_data_chunk_sources(shared_string_index_provider);

    check(projections.size() == 1,
        "failing shared-string sheetData projection should still be created");
    check(data.dirty(),
        "failing shared-string sheetData projection should start dirty");

    auto read_next_chunk = projections[0].read_next_chunk;
    check(throws_fastxlsx_error([&] {
        const std::string ignored = read_all_chunks(read_next_chunk);
        (void)ignored;
    }), "failing shared-string sheetData projection should propagate provider errors");
    check(data.dirty(),
        "failing shared-string sheetData projection should keep session dirty");
}

void test_dirty_worksheet_projection_provider_failure_keeps_session_dirty()
{
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    materialize_session(registry, "Data")
        .set_cell(1, 1, fastxlsx::CellValue::text("known"));
    fastxlsx::detail::MaterializedWorksheetSession& data =
        *registry.try_session("Data");
    data.set_cell(2, 1, fastxlsx::CellValue::text("missing"));

    auto shared_string_index_provider =
        std::make_shared<fastxlsx::detail::CellStoreSharedStringIndexProvider>(
            [](std::string_view text) -> std::uint32_t {
                if (text == "known") {
                    return 0;
                }
                throw fastxlsx::FastXlsxError(
                    "missing shared string index in worksheet projection test");
            });

    const std::vector<fastxlsx::detail::MaterializedWorksheetProjection>
        projections =
            registry.dirty_worksheet_chunk_sources(shared_string_index_provider);

    check(projections.size() == 1,
        "failing shared-string worksheet projection should still be created");
    check(data.dirty(),
        "failing shared-string worksheet projection should start dirty");

    auto read_next_chunk = projections[0].read_next_chunk;
    check(throws_fastxlsx_error([&] {
        const std::string ignored = read_all_chunks(read_next_chunk);
        (void)ignored;
    }), "failing shared-string worksheet projection should propagate provider errors");
    check(data.dirty(),
        "failing shared-string worksheet projection should keep session dirty");
}

void test_materialized_flush_target_validation_accepts_current_names()
{
    fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data"});
    const std::vector<fastxlsx::detail::MaterializedWorksheetSheetDataProjection> projections {
        fastxlsx::detail::MaterializedWorksheetSheetDataProjection {
            "Data",
            [](std::string&) { return false; },
            "A1",
        },
    };

    fastxlsx::detail::validate_workbook_editor_materialized_flush_targets(
        catalog, projections);
}

void test_materialized_flush_target_validation_uses_current_catalog_names()
{
    fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data"});
    catalog.record_rename("Data", "Renamed");

    const std::vector<fastxlsx::detail::MaterializedWorksheetSheetDataProjection>
        renamed_projection {
            fastxlsx::detail::MaterializedWorksheetSheetDataProjection {
                "Renamed",
                [](std::string&) { return false; },
                "A1",
            },
        };
    fastxlsx::detail::validate_workbook_editor_materialized_flush_targets(
        catalog, renamed_projection);

    const std::vector<fastxlsx::detail::MaterializedWorksheetSheetDataProjection>
        stale_source_projection {
            fastxlsx::detail::MaterializedWorksheetSheetDataProjection {
                "Data",
                [](std::string&) { return false; },
                "A1",
            },
        };
    check(throws_fastxlsx_error([&] {
        fastxlsx::detail::validate_workbook_editor_materialized_flush_targets(
            catalog, stale_source_projection);
    }), "flush target validation should reject stale source names after rename");
}

void test_materialized_flush_target_validation_rejects_missing_names()
{
    fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data"});
    const std::vector<fastxlsx::detail::MaterializedWorksheetSheetDataProjection> projections {
        fastxlsx::detail::MaterializedWorksheetSheetDataProjection {
            "Missing",
            [](std::string&) { return false; },
            "A1",
        },
    };

    check(throws_fastxlsx_error([&] {
        fastxlsx::detail::validate_workbook_editor_materialized_flush_targets(
            catalog, projections);
    }), "flush target validation should reject sheets outside current catalog");
}

} // namespace

int main()
{
    try {
        test_pending_materialized_names_follow_current_catalog_order();
        test_pending_materialized_names_ignore_stale_source_names();
        test_dirty_sheet_data_projections_include_only_dirty_sessions_and_dimensions();
        test_dirty_sheet_data_projection_uses_shared_string_index_provider();
        test_dirty_worksheet_projection_uses_shared_string_index_provider();
        test_dirty_sheet_data_projection_provider_failure_keeps_session_dirty();
        test_dirty_worksheet_projection_provider_failure_keeps_session_dirty();
        test_materialized_flush_target_validation_accepts_current_names();
        test_materialized_flush_target_validation_uses_current_catalog_names();
        test_materialized_flush_target_validation_rejects_missing_names();
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
    return 0;
}
