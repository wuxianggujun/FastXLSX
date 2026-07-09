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
    if (with_shared_strings) {
        source.worksheet = R"(<?xml version="1.0" encoding="UTF-8"?>)"
            R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
            R"(<dimension ref="A1"/><sheetData><row r="1"><c r="A1" t="s"><v>0</v></c></row></sheetData></worksheet>)";
        source.shared_strings = R"(<?xml version="1.0" encoding="UTF-8"?>)"
            R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
            R"(<si><t>existing</t></si></sst>)";
    } else {
        source.worksheet = R"(<?xml version="1.0" encoding="UTF-8"?>)"
            R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
            R"(<dimension ref="A1"/><sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData></worksheet>)";
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

std::map<std::string, std::string> read_stored_package_entries(
    const std::filesystem::path& path)
{
    return fastxlsx::test::read_stored_zip_entries(path);
}

void check_materialized_flush_noop_save_is_stable(
    fastxlsx::detail::PackageEditor& editor,
    fastxlsx::detail::MaterializedWorksheetSessionRegistry& registry,
    const std::filesystem::path& output,
    const std::filesystem::path& noop_output,
    const char* byte_stable_message,
    const char* clean_registry_message)
{
    const std::map<std::string, std::string> output_entries =
        read_stored_package_entries(output);

    editor.save_as(noop_output);
    check(read_stored_package_entries(noop_output) == output_entries,
        byte_stable_message);
    check(registry.dirty_session_count() == 0,
        clean_registry_message);
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
    check(registry.dirty_session_count() == 1,
        "failing shared-string sheetData projection should keep dirty diagnostics");

    auto recovery_shared_string_index_provider =
        std::make_shared<fastxlsx::detail::CellStoreSharedStringIndexProvider>(
            [](std::string_view text) -> std::uint32_t {
                if (text == "known") {
                    return 0;
                }
                if (text == "missing") {
                    return 3;
                }
                throw fastxlsx::FastXlsxError(
                    "unexpected shared string text in sheetData projection retry");
            });

    const std::vector<fastxlsx::detail::MaterializedWorksheetSheetDataProjection>
        retry_projections = registry.dirty_sheet_data_chunk_sources(
            recovery_shared_string_index_provider);
    check(retry_projections.size() == 1,
        "retry shared-string sheetData projection should still include dirty session");
    auto retry_read_next_chunk = retry_projections[0].read_next_chunk;
    const std::string retry_xml = read_all_chunks(retry_read_next_chunk);
    check(retry_xml.find(R"(<c r="A1" t="s"><v>0</v></c>)") != std::string::npos,
        "retry shared-string sheetData projection should reuse known string index");
    check(retry_xml.find(R"(<c r="B1" t="s"><v>3</v></c>)") != std::string::npos,
        "retry shared-string sheetData projection should use recovered string index");
    check(data.dirty(),
        "retry shared-string sheetData projection should keep session dirty");
    check(registry.dirty_session_count() == 1,
        "retry shared-string sheetData projection should keep dirty diagnostics");
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
    check(registry.dirty_session_count() == 1,
        "failing shared-string worksheet projection should keep dirty diagnostics");

    auto recovery_shared_string_index_provider =
        std::make_shared<fastxlsx::detail::CellStoreSharedStringIndexProvider>(
            [](std::string_view text) -> std::uint32_t {
                if (text == "known") {
                    return 0;
                }
                if (text == "missing") {
                    return 4;
                }
                throw fastxlsx::FastXlsxError(
                    "unexpected shared string text in worksheet projection retry");
            });

    const std::vector<fastxlsx::detail::MaterializedWorksheetProjection>
        retry_projections = registry.dirty_worksheet_chunk_sources(
            recovery_shared_string_index_provider);
    check(retry_projections.size() == 1,
        "retry shared-string worksheet projection should still include dirty session");
    auto retry_read_next_chunk = retry_projections[0].read_next_chunk;
    const std::string retry_xml = read_all_chunks(retry_read_next_chunk);
    check(retry_xml.find(R"(<dimension ref="A1:A2"/>)") != std::string::npos,
        "retry shared-string worksheet projection should keep sparse dimensions");
    check(retry_xml.find(R"(<c r="A1" t="s"><v>0</v></c>)") != std::string::npos,
        "retry shared-string worksheet projection should reuse known string index");
    check(retry_xml.find(R"(<c r="A2" t="s"><v>4</v></c>)") != std::string::npos,
        "retry shared-string worksheet projection should use recovered string index");
    check(data.dirty(),
        "retry shared-string worksheet projection should keep session dirty");
    check(registry.dirty_session_count() == 1,
        "retry shared-string worksheet projection should keep dirty diagnostics");
}

void test_dirty_sheet_data_projection_provider_skips_non_text_records()
{
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    materialize_session(registry, "Data")
        .set_cell(1, 1, fastxlsx::CellValue::number(11.0));
    fastxlsx::detail::MaterializedWorksheetSession& data =
        *registry.try_session("Data");
    data.set_cell(2, 2, fastxlsx::CellValue::boolean(false));
    data.set_cell(3, 3, fastxlsx::CellValue::formula("A1&B2<C3"));
    data.set_cell(4, 4, fastxlsx::CellValue::error("#VALUE<&"));
    data.set_cell(5, 5, fastxlsx::CellValue::blank());

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
    check(projections[0].dimension_reference == "A1:E5",
        "non-text sheetData projection should expose sparse dimensions");

    auto read_next_chunk = projections[0].read_next_chunk;
    const std::string xml = read_all_chunks(read_next_chunk);
    check(!provider_called,
        "non-text sheetData projection should not call shared string provider");
    check(xml.find(R"(<c r="A1"><v>11</v></c>)") != std::string::npos,
        "non-text sheetData projection should emit numeric cells");
    check(xml.find(R"(<c r="B2" t="b"><v>0</v></c>)") != std::string::npos,
        "non-text sheetData projection should emit boolean cells");
    check(xml.find(R"(<c r="C3"><f>A1&amp;B2&lt;C3</f></c>)") != std::string::npos,
        "non-text sheetData projection should emit escaped formula cells");
    check(xml.find(R"(<c r="D4" t="e"><v>#VALUE&lt;&amp;</v></c>)") != std::string::npos,
        "non-text sheetData projection should emit escaped error cells");
    check(xml.find(R"(<c r="E5"/>)") != std::string::npos,
        "non-text sheetData projection should emit blank cells");
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
    data.set_cell(4, 4, fastxlsx::CellValue::formula("A1&C3<D4"));
    data.set_cell(5, 5, fastxlsx::CellValue::error("#N/A<&"));
    data.set_cell(2, 2, fastxlsx::CellValue::blank());

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
    check(xml.find(R"(<dimension ref="A1:E5"/>)") != std::string::npos,
        "non-text worksheet projection should emit sparse dimensions");
    check(xml.find(R"(<c r="A1"><v>12</v></c>)") != std::string::npos,
        "non-text worksheet projection should emit numeric cells");
    check(xml.find(R"(<c r="B2"/>)") != std::string::npos,
        "non-text worksheet projection should emit blank cells");
    check(xml.find(R"(<c r="C3" t="b"><v>1</v></c>)") != std::string::npos,
        "non-text worksheet projection should emit boolean cells");
    check(xml.find(R"(<c r="D4"><f>A1&amp;C3&lt;D4</f></c>)") != std::string::npos,
        "non-text worksheet projection should emit escaped formula cells");
    check(xml.find(R"(<c r="E5" t="e"><v>#N/A&lt;&amp;</v></c>)") != std::string::npos,
        "non-text worksheet projection should emit escaped error cells");
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
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(1, 1, fastxlsx::CellValue::text("inline-a1"));
    data.set_cell(2, 2, fastxlsx::CellValue::number(22.0));
    data.set_cell(3, 3, fastxlsx::CellValue::boolean(true));
    data.set_cell(4, 4, fastxlsx::CellValue::formula("A1&B2<C3"));
    data.set_cell(5, 5, fastxlsx::CellValue::error("#VALUE<&"));
    data.set_cell(6, 6, fastxlsx::CellValue::blank());
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
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-noop-output.xlsx");
    editor.save_as(output);
    const std::string worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    check(worksheet.find(R"(<dimension ref="A1:F6"/>)") != std::string::npos,
        "materialized flush should update worksheet dimensions from sparse cells");
    check(worksheet.find(R"(<c r="A1" t="inlineStr"><is><t>inline-a1</t></is></c>)")
            != std::string::npos,
        "materialized flush should project text as inline strings without sharedStrings");
    check(worksheet.find(R"(<c r="B2"><v>22</v></c>)") != std::string::npos,
        "materialized flush should project numeric sparse cells");
    check(worksheet.find(R"(<c r="C3" t="b"><v>1</v></c>)") != std::string::npos,
        "materialized flush should project boolean sparse cells");
    check(worksheet.find(R"(<c r="D4"><f>A1&amp;B2&lt;C3</f></c>)") != std::string::npos,
        "materialized flush should project escaped formula sparse cells");
    check(worksheet.find(R"(<c r="E5" t="e"><v>#VALUE&lt;&amp;</v></c>)") != std::string::npos,
        "materialized flush should project escaped error sparse cells");
    check(worksheet.find(R"(<c r="F6"/>)") != std::string::npos,
        "materialized flush should project blank sparse cells");
    check(!stored_package_has_entry(output, "xl/sharedStrings.xml"),
        "materialized flush without source sharedStrings should not create sharedStrings");
    check_materialized_flush_noop_save_is_stable(
        editor,
        registry,
        output,
        noop_output,
        "materialized flush no-op save should keep output byte-stable",
        "materialized flush no-op save should keep registry clean");
    check(!data.dirty(),
        "materialized flush no-op save should keep the flushed session clean");
    check(read_stored_package_entries(source.path) == source_entries,
        "materialized flush no-op save should not mutate the source package");
}

void test_materialized_flush_rejects_stale_targets_without_clearing_dirty()
{
    const MaterializedFlushSourcePackage source =
        write_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-stale-source.xlsx");
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);
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
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-stale-noop-output.xlsx");
    editor.save_as(output);
    const std::string worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    check(worksheet == source.worksheet,
        "rejected materialized flush should leave PackageEditor output unmodified");
    check(read_stored_package_entries(output) == source_entries,
        "rejected materialized flush should keep the whole output package source-copy");
    check(missing.dirty(),
        "rejected materialized flush save should keep the stale session dirty");
    check(registry.dirty_session_count() == 1,
        "rejected materialized flush save should preserve dirty diagnostics");

    editor.save_as(noop_output);
    check(read_stored_package_entries(noop_output) == source_entries,
        "rejected materialized flush no-op save should stay source-copy");
    check(read_stored_package_entries(source.path) == source_entries,
        "rejected materialized flush no-op save should not mutate the source package");
    check(missing.dirty(),
        "rejected materialized flush no-op save should keep the stale session dirty");
    check(registry.dirty_session_count() == 1,
        "rejected materialized flush no-op save should preserve dirty diagnostics");
}

void test_materialized_flush_appends_shared_strings_projection()
{
    const MaterializedFlushSourcePackage source =
        write_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-shared-source.xlsx",
            true);
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    data.set_cell(1, 2, fastxlsx::CellValue::text("new-shared"));
    data.set_cell(1, 3, fastxlsx::CellValue::text("escaped <&> shared"));
    data.set_cell(1, 4, fastxlsx::CellValue::text("  spaced shared  "));
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
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-shared-noop-output.xlsx");
    editor.save_as(output);
    const std::string worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    const std::string shared_strings =
        read_stored_package_entry(output, "xl/sharedStrings.xml");

    check(worksheet.find(R"(<dimension ref="A1:D2"/>)") != std::string::npos,
        "sharedStrings materialized flush should update sparse dimensions");
    check(worksheet.find(R"(<c r="A1" t="s"><v>0</v></c>)") != std::string::npos,
        "sharedStrings materialized flush should reuse source string index");
    check(worksheet.find(R"(<c r="B1" t="s"><v>1</v></c>)") != std::string::npos,
        "sharedStrings materialized flush should reference appended string index");
    check(worksheet.find(R"(<c r="C1" t="s"><v>2</v></c>)") != std::string::npos,
        "sharedStrings materialized flush should reference escaped appended string index");
    check(worksheet.find(R"(<c r="D1" t="s"><v>3</v></c>)") != std::string::npos,
        "sharedStrings materialized flush should reference whitespace appended string index");
    check(worksheet.find(R"(<c r="A2"><v>9</v></c>)") != std::string::npos,
        "sharedStrings materialized flush should keep numeric cells value-only");
    check(worksheet.find("inlineStr") == std::string::npos,
        "sharedStrings materialized flush should not emit inline text fallback");
    check(shared_strings.find(R"(count="4")") != std::string::npos &&
            shared_strings.find(R"(uniqueCount="4")") != std::string::npos,
        "sharedStrings materialized flush should update source table counts");
    const std::string expected_appended_shared_strings =
        R"(<si><t>existing</t></si>)"
        R"(<si><t>new-shared</t></si>)"
        R"(<si><t>escaped &lt;&amp;&gt; shared</t></si>)"
        R"(<si><t xml:space="preserve">  spaced shared  </t></si>)";
    check(shared_strings.find(expected_appended_shared_strings) != std::string::npos,
        "sharedStrings materialized flush should append missing text with XML escaping and whitespace preservation");
    check_materialized_flush_noop_save_is_stable(
        editor,
        registry,
        output,
        noop_output,
        "sharedStrings materialized flush no-op save should keep output byte-stable",
        "sharedStrings materialized flush no-op save should keep registry clean");
    check(!data.dirty(),
        "sharedStrings materialized flush no-op save should keep the flushed session clean");
    check(read_stored_package_entries(source.path) == source_entries,
        "sharedStrings materialized flush no-op save should not mutate the source package");
}

void test_materialized_flush_reuses_existing_shared_strings_without_rewrite()
{
    const MaterializedFlushSourcePackage source =
        write_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-shared-existing-source.xlsx",
            true);
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(1, 1, fastxlsx::CellValue::text("existing"));

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data"});
    const fastxlsx::detail::WorkbookEditorMaterializedFlushResult result =
        fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
            editor, registry, catalog);

    check(result.flushed_worksheet_count == 1,
        "existing-only sharedStrings flush should report one worksheet");
    check(!data.dirty(),
        "existing-only sharedStrings flush should clear the dirty session");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-shared-existing-output.xlsx");
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-shared-existing-noop-output.xlsx");
    editor.save_as(output);
    const std::string worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    const std::string shared_strings =
        read_stored_package_entry(output, "xl/sharedStrings.xml");

    check(worksheet.find(R"(<c r="A1" t="s"><v>0</v></c>)") != std::string::npos,
        "existing-only sharedStrings flush should reuse the existing string index");
    check(worksheet.find("inlineStr") == std::string::npos,
        "existing-only sharedStrings flush should not fall back to inline text");
    check(shared_strings == source.shared_strings,
        "existing-only sharedStrings flush should not rewrite the sharedStrings part");
    check_materialized_flush_noop_save_is_stable(
        editor,
        registry,
        output,
        noop_output,
        "existing-only sharedStrings flush no-op save should keep output byte-stable",
        "existing-only sharedStrings flush no-op save should keep registry clean");
    check(!data.dirty(),
        "existing-only sharedStrings flush no-op save should keep the flushed session clean");
    check(read_stored_package_entries(source.path) == source_entries,
        "existing-only sharedStrings flush no-op save should not mutate the source package");
}

void test_materialized_flush_falls_back_to_inline_when_shared_strings_append_is_unsupported()
{
    MaterializedFlushSourcePackage source =
        write_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-shared-unsupported-source.xlsx",
            true);
    const std::string unsupported_shared_strings =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1" unsupported="preserve-me">)"
        R"(<si><t>existing</t></si></sst>)";
    fastxlsx::test::rewrite_package_entry_as_stored(
        source.path, "xl/sharedStrings.xml", unsupported_shared_strings);
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    data.set_cell(1, 2, fastxlsx::CellValue::text("fallback <&> text"));
    data.set_cell(1, 3, fastxlsx::CellValue::text("  fallback spaced  "));

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data"});
    const fastxlsx::detail::WorkbookEditorMaterializedFlushResult result =
        fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
            editor, registry, catalog);

    check(result.flushed_worksheet_count == 1,
        "unsupported sharedStrings flush should still flush one worksheet");
    check(!data.dirty(),
        "unsupported sharedStrings flush should clear the dirty session");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-shared-unsupported-output.xlsx");
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-shared-unsupported-noop-output.xlsx");
    editor.save_as(output);
    const std::string worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    const std::string shared_strings =
        read_stored_package_entry(output, "xl/sharedStrings.xml");

    check(worksheet.find(R"(<dimension ref="A1:C1"/>)") != std::string::npos,
        "unsupported sharedStrings flush should update sparse dimensions");
    check(worksheet.find(
              R"(<c r="A1" t="inlineStr"><is><t>existing</t></is></c>)")
            != std::string::npos,
        "unsupported sharedStrings flush should write existing text inline");
    check(worksheet.find(
              R"(<c r="B1" t="inlineStr"><is><t>fallback &lt;&amp;&gt; text</t></is></c>)")
            != std::string::npos,
        "unsupported sharedStrings flush should write escaped dirty text inline");
    check(worksheet.find(
              R"(<c r="C1" t="inlineStr"><is><t xml:space="preserve">  fallback spaced  </t></is></c>)")
            != std::string::npos,
        "unsupported sharedStrings flush should preserve dirty text whitespace inline");
    check(worksheet.find(R"(t="s")") == std::string::npos,
        "unsupported sharedStrings flush should not write shared string indexes");
    check(shared_strings == unsupported_shared_strings,
        "unsupported sharedStrings flush should preserve source sharedStrings bytes");

    check_materialized_flush_noop_save_is_stable(
        editor,
        registry,
        output,
        noop_output,
        "unsupported sharedStrings flush no-op save should keep output byte-stable",
        "unsupported sharedStrings flush no-op save should keep registry clean");
    check(!data.dirty(),
        "unsupported sharedStrings flush no-op save should keep the flushed session clean");
    check(read_stored_package_entries(source.path) == source_entries,
        "unsupported sharedStrings flush no-op save should not mutate the source package");
}

void test_materialized_flush_deduplicates_appended_shared_strings()
{
    const MaterializedFlushSourcePackage source =
        write_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-shared-duplicate-source.xlsx",
            true);
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    data.set_cell(1, 2, fastxlsx::CellValue::text("repeat-new"));
    data.set_cell(2, 2, fastxlsx::CellValue::text("repeat-new"));

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data"});
    const fastxlsx::detail::WorkbookEditorMaterializedFlushResult result =
        fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
            editor, registry, catalog);

    check(result.flushed_worksheet_count == 1,
        "duplicate sharedStrings flush should report one worksheet");
    check(!data.dirty(),
        "duplicate sharedStrings flush should clear the dirty session");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-shared-duplicate-output.xlsx");
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-shared-duplicate-noop-output.xlsx");
    editor.save_as(output);
    const std::string worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    const std::string shared_strings =
        read_stored_package_entry(output, "xl/sharedStrings.xml");

    check(worksheet.find(R"(<c r="A1" t="s"><v>0</v></c>)") != std::string::npos,
        "duplicate sharedStrings flush should preserve the existing string index");
    check(worksheet.find(R"(<c r="B1" t="s"><v>1</v></c>)") != std::string::npos &&
            worksheet.find(R"(<c r="B2" t="s"><v>1</v></c>)") != std::string::npos,
        "duplicate sharedStrings flush should reuse one appended string index");
    check(worksheet.find("inlineStr") == std::string::npos,
        "duplicate sharedStrings flush should not emit inline text fallback");
    check(shared_strings.find(R"(count="3")") != std::string::npos &&
            shared_strings.find(R"(uniqueCount="2")") != std::string::npos,
        "duplicate sharedStrings flush should count references and unique strings separately");
    check(shared_strings.find(R"(<si><t>existing</t></si><si><t>repeat-new</t></si>)")
            != std::string::npos,
        "duplicate sharedStrings flush should append each new text only once");
    check_materialized_flush_noop_save_is_stable(
        editor,
        registry,
        output,
        noop_output,
        "duplicate sharedStrings flush no-op save should keep output byte-stable",
        "duplicate sharedStrings flush no-op save should keep registry clean");
    check(!data.dirty(),
        "duplicate sharedStrings flush no-op save should keep the flushed session clean");
    check(read_stored_package_entries(source.path) == source_entries,
        "duplicate sharedStrings flush no-op save should not mutate the source package");
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
        test_materialized_flush_reuses_existing_shared_strings_without_rewrite();
        test_materialized_flush_falls_back_to_inline_when_shared_strings_append_is_unsupported();
        test_materialized_flush_deduplicates_appended_shared_strings();
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
    return 0;
}
