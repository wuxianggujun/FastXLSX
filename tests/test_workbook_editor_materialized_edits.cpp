#include "../src/workbook_editor_materialized_edits.hpp"
#include "../src/package_editor.hpp"
#include "zip_test_utils.hpp"

#include <fastxlsx/cell_value.hpp>
#include <fastxlsx/detail/cell_store.hpp>
#include <fastxlsx/workbook.hpp>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
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

struct MaterializedFlushSourcePackage {
    std::filesystem::path path;
    std::string worksheet;
    std::string shared_strings;
};

MaterializedFlushSourcePackage write_materialized_flush_source_package(
    std::string_view name,
    bool with_shared_strings = false)
{
    MaterializedFlushSourcePackage source;
    source.path = fastxlsx::test::artifact_path(name);
    source.worksheet = R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<dimension ref="A1"/><sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData></worksheet>)";
    if (with_shared_strings) {
        source.shared_strings = R"(<?xml version="1.0" encoding="UTF-8"?>)"
            R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
            R"(<si><t>existing</t></si></sst>)";
    }

    std::string content_types = R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)";
    if (with_shared_strings) {
        content_types +=
            R"(<Override PartName="/xl/sharedStrings.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml"/>)";
    }
    content_types += R"(</Types>)";

    std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)";
    if (with_shared_strings) {
        workbook_relationships +=
            R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>)";
    }
    workbook_relationships += R"(</Relationships>)";

    std::map<std::string, std::string> entries;
    entries.emplace("[Content_Types].xml", std::move(content_types));
    entries.emplace("_rels/.rels",
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)");
    entries.emplace("xl/workbook.xml",
        R"(<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" )"
        R"(xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Data" sheetId="1" r:id="rId1"/></sheets></workbook>)");
    entries.emplace("xl/_rels/workbook.xml.rels", std::move(workbook_relationships));
    entries.emplace("xl/worksheets/sheet1.xml", source.worksheet);
    if (with_shared_strings) {
        entries.emplace("xl/sharedStrings.xml", source.shared_strings);
    }
    fastxlsx::test::write_stored_zip_entries(source.path, entries);
    return source;
}

std::string read_stored_package_entry(
    const std::filesystem::path& path,
    std::string_view entry_name)
{
    const std::map<std::string, std::string> entries =
        fastxlsx::test::read_stored_zip_entries(path);
    const auto found = entries.find(std::string(entry_name));
    if (found == entries.end()) {
        throw TestFailure("stored package entry not found");
    }
    return found->second;
}

bool stored_package_has_entry(
    const std::filesystem::path& path,
    std::string_view entry_name)
{
    const std::map<std::string, std::string> entries =
        fastxlsx::test::read_stored_zip_entries(path);
    return entries.find(std::string(entry_name)) != entries.end();
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

void test_dirty_sheet_data_projection_provider_skips_non_text_records()
{
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    materialize_session(registry, "Data")
        .set_cell(1, 1, fastxlsx::CellValue::number(11.0));
    fastxlsx::detail::MaterializedWorksheetSession& data =
        *registry.try_session("Data");
    data.set_cell(2, 2, fastxlsx::CellValue::boolean(false));

    bool provider_called = false;
    auto shared_string_index_provider =
        std::make_shared<fastxlsx::detail::CellStoreSharedStringIndexProvider>(
            [&provider_called](std::string_view) -> std::uint32_t {
                provider_called = true;
                return 99;
            });

    const std::vector<fastxlsx::detail::MaterializedWorksheetSheetDataProjection>
        projections =
            registry.dirty_sheet_data_chunk_sources(shared_string_index_provider);

    check(projections.size() == 1,
        "non-text sheetData projection should include dirty session");
    check(projections[0].dimension_reference == "A1:B2",
        "non-text sheetData projection should expose sparse dimensions");

    auto read_next_chunk = projections[0].read_next_chunk;
    const std::string xml = read_all_chunks(read_next_chunk);
    check(!provider_called,
        "non-text sheetData projection should not call shared string provider");
    check(xml.find(R"(<c r="A1"><v>11</v></c>)") != std::string::npos,
        "non-text sheetData projection should emit numeric cells");
    check(xml.find(R"(<c r="B2" t="b"><v>0</v></c>)") != std::string::npos,
        "non-text sheetData projection should emit boolean cells");
    check(xml.find(R"(t="s")") == std::string::npos &&
            xml.find("inlineStr") == std::string::npos,
        "non-text sheetData projection should not emit text cell encodings");
    check(data.dirty(),
        "non-text sheetData projection should not clear dirty state");
}

void test_dirty_worksheet_projection_provider_skips_non_text_records()
{
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    materialize_session(registry, "Data")
        .set_cell(1, 1, fastxlsx::CellValue::number(12.0));
    fastxlsx::detail::MaterializedWorksheetSession& data =
        *registry.try_session("Data");
    data.set_cell(3, 3, fastxlsx::CellValue::boolean(true));

    bool provider_called = false;
    auto shared_string_index_provider =
        std::make_shared<fastxlsx::detail::CellStoreSharedStringIndexProvider>(
            [&provider_called](std::string_view) -> std::uint32_t {
                provider_called = true;
                return 99;
            });

    const std::vector<fastxlsx::detail::MaterializedWorksheetProjection>
        projections =
            registry.dirty_worksheet_chunk_sources(shared_string_index_provider);

    check(projections.size() == 1,
        "non-text worksheet projection should include dirty session");

    auto read_next_chunk = projections[0].read_next_chunk;
    const std::string xml = read_all_chunks(read_next_chunk);
    check(!provider_called,
        "non-text worksheet projection should not call shared string provider");
    check(xml.find(R"(<dimension ref="A1:C3"/>)") != std::string::npos,
        "non-text worksheet projection should emit sparse dimensions");
    check(xml.find(R"(<c r="A1"><v>12</v></c>)") != std::string::npos,
        "non-text worksheet projection should emit numeric cells");
    check(xml.find(R"(<c r="C3" t="b"><v>1</v></c>)") != std::string::npos,
        "non-text worksheet projection should emit boolean cells");
    check(xml.find(R"(t="s")") == std::string::npos &&
            xml.find("inlineStr") == std::string::npos,
        "non-text worksheet projection should not emit text cell encodings");
    check(data.dirty(),
        "non-text worksheet projection should not clear dirty state");
}

void test_dirty_empty_projection_after_erasing_all_source_cells()
{
    fastxlsx::detail::CellStore source_store;
    source_store.set_cell(1, 1, fastxlsx::CellValue::text("source-a1"));
    source_store.set_cell(3, 3, fastxlsx::CellValue::number(3.0));

    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        registry.materialize("Data", std::move(source_store));
    check(!data.dirty(),
        "source-backed materialized session should start clean before erase");

    data.erase_cells();
    check(data.dirty(),
        "erasing all source-backed materialized cells should dirty the session");
    check(data.cell_count() == 0,
        "erasing all source-backed materialized cells should empty the sparse store");

    const std::vector<fastxlsx::detail::MaterializedWorksheetSheetDataProjection>
        sheet_data_projections = registry.dirty_sheet_data_chunk_sources();
    check(sheet_data_projections.size() == 1,
        "empty dirty sheetData projection should include dirty session");
    check(sheet_data_projections[0].planned_name == "Data",
        "empty dirty sheetData projection should preserve planned name");
    check(sheet_data_projections[0].dimension_reference == "A1",
        "empty dirty sheetData projection should expose A1 dimension");

    auto sheet_data_read_next_chunk = sheet_data_projections[0].read_next_chunk;
    const std::string sheet_data_xml =
        read_all_chunks(sheet_data_read_next_chunk);
    check(sheet_data_xml == "<sheetData></sheetData>",
        "empty dirty sheetData projection should emit empty sheetData");
    check(data.dirty(),
        "consuming empty sheetData projection should not clear dirty state");

    const std::vector<fastxlsx::detail::MaterializedWorksheetProjection>
        worksheet_projections = registry.dirty_worksheet_chunk_sources();
    check(worksheet_projections.size() == 1,
        "empty dirty worksheet projection should include dirty session");
    check(worksheet_projections[0].planned_name == "Data",
        "empty dirty worksheet projection should preserve planned name");

    auto worksheet_read_next_chunk = worksheet_projections[0].read_next_chunk;
    const std::string worksheet_xml = read_all_chunks(worksheet_read_next_chunk);
    check(worksheet_xml
            == R"(<?xml version="1.0" encoding="UTF-8"?>)"
               R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
               R"(<dimension ref="A1"/><sheetData></sheetData></worksheet>)",
        "empty dirty worksheet projection should emit minimal worksheet XML");
    check(data.dirty(),
        "consuming empty worksheet projection should not clear dirty state");
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

void test_materialized_flush_to_patch_plan_clears_dirty_sessions()
{
    const MaterializedFlushSourcePackage source =
        write_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-source.xlsx");
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(1, 1, fastxlsx::CellValue::text("inline-a1"));
    data.set_cell(2, 2, fastxlsx::CellValue::number(22.0));
    check(data.dirty(), "materialized flush test should start with a dirty session");

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data"});
    const fastxlsx::detail::WorkbookEditorMaterializedFlushResult result =
        fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
            editor, registry, catalog);

    check(result.flushed_worksheet_count == 1,
        "materialized flush should report one flushed worksheet");
    check(!data.dirty(),
        "materialized flush should clear the flushed session dirty flag");
    check(registry.dirty_session_count() == 0,
        "materialized flush should clear registry dirty diagnostics");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-output.xlsx");
    editor.save_as(output);
    const std::string worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    check(worksheet.find(R"(<dimension ref="A1:B2"/>)") != std::string::npos,
        "materialized flush should update worksheet dimensions from sparse cells");
    check(worksheet.find(R"(<c r="A1" t="inlineStr"><is><t>inline-a1</t></is></c>)")
            != std::string::npos,
        "materialized flush should project text as inline strings without sharedStrings");
    check(worksheet.find(R"(<c r="B2"><v>22</v></c>)") != std::string::npos,
        "materialized flush should project numeric sparse cells");
    check(!stored_package_has_entry(output, "xl/sharedStrings.xml"),
        "materialized flush without source sharedStrings should not create sharedStrings");
}

void test_materialized_flush_rejects_stale_targets_without_clearing_dirty()
{
    const MaterializedFlushSourcePackage source =
        write_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-stale-source.xlsx");
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& missing =
        materialize_session(registry, "Missing");
    missing.set_cell(1, 1, fastxlsx::CellValue::text("should-not-flush"));

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data"});
    check(throws_fastxlsx_error([&] {
        const fastxlsx::detail::WorkbookEditorMaterializedFlushResult ignored =
            fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
                editor, registry, catalog);
        (void)ignored;
    }), "materialized flush should reject dirty sessions outside the current catalog");
    check(missing.dirty(),
        "rejected materialized flush should keep the stale session dirty");
    check(registry.dirty_session_count() == 1,
        "rejected materialized flush should preserve dirty diagnostics");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-stale-output.xlsx");
    editor.save_as(output);
    const std::string worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    check(worksheet == source.worksheet,
        "rejected materialized flush should leave PackageEditor output unmodified");
}

void test_materialized_flush_appends_shared_strings_projection()
{
    const MaterializedFlushSourcePackage source =
        write_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-shared-source.xlsx",
            true);
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    data.set_cell(1, 2, fastxlsx::CellValue::text("new-shared"));
    data.set_cell(2, 1, fastxlsx::CellValue::number(9.0));

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data"});
    const fastxlsx::detail::WorkbookEditorMaterializedFlushResult result =
        fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
            editor, registry, catalog);

    check(result.flushed_worksheet_count == 1,
        "sharedStrings materialized flush should report one worksheet");
    check(!data.dirty(),
        "sharedStrings materialized flush should clear the dirty session");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-shared-output.xlsx");
    editor.save_as(output);
    const std::string worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    const std::string shared_strings =
        read_stored_package_entry(output, "xl/sharedStrings.xml");

    check(worksheet.find(R"(<dimension ref="A1:B2"/>)") != std::string::npos,
        "sharedStrings materialized flush should update sparse dimensions");
    check(worksheet.find(R"(<c r="A1" t="s"><v>0</v></c>)") != std::string::npos,
        "sharedStrings materialized flush should reuse source string index");
    check(worksheet.find(R"(<c r="B1" t="s"><v>1</v></c>)") != std::string::npos,
        "sharedStrings materialized flush should reference appended string index");
    check(worksheet.find(R"(<c r="A2"><v>9</v></c>)") != std::string::npos,
        "sharedStrings materialized flush should keep numeric cells value-only");
    check(worksheet.find("inlineStr") == std::string::npos,
        "sharedStrings materialized flush should not emit inline text fallback");
    check(shared_strings.find(R"(count="2")") != std::string::npos &&
            shared_strings.find(R"(uniqueCount="2")") != std::string::npos,
        "sharedStrings materialized flush should update source table counts");
    check(shared_strings.find(R"(<si><t>existing</t></si><si><t>new-shared</t></si>)")
            != std::string::npos,
        "sharedStrings materialized flush should append only missing text");
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
        test_dirty_sheet_data_projection_provider_skips_non_text_records();
        test_dirty_worksheet_projection_provider_skips_non_text_records();
        test_dirty_empty_projection_after_erasing_all_source_cells();
        test_materialized_flush_target_validation_accepts_current_names();
        test_materialized_flush_target_validation_uses_current_catalog_names();
        test_materialized_flush_target_validation_rejects_missing_names();
        test_materialized_flush_to_patch_plan_clears_dirty_sessions();
        test_materialized_flush_rejects_stale_targets_without_clearing_dirty();
        test_materialized_flush_appends_shared_strings_projection();
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
    return 0;
}
