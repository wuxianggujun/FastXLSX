#include "../src/workbook_editor_materialized_edits.hpp"

#include <fastxlsx/cell_value.hpp>
#include <fastxlsx/detail/cell_store.hpp>
#include <fastxlsx/workbook.hpp>

#include <iostream>
#include <stdexcept>
#include <string>
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
        test_materialized_flush_target_validation_accepts_current_names();
        test_materialized_flush_target_validation_uses_current_catalog_names();
        test_materialized_flush_target_validation_rejects_missing_names();
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
    return 0;
}
