#include "test_workbook_editor_source_success_common.hpp"

void test_public_worksheet_editor_materializes_source_max_coordinate_and_erases_edge()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-source-max-coordinate-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-noop-output.xlsx");
    const std::filesystem::path erase_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-erase-output.xlsx");

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    check(entries.find("xl/sharedStrings.xml") == entries.end(),
        "supported source values fixture should not require a sharedStrings part");
    check_not_contains(entries.at("xl/_rels/workbook.xml.rels"),
        "relationships/sharedStrings",
        "supported source values fixture should not require a sharedStrings relationship");
    check_not_contains(entries.at("[Content_Types].xml"),
        "spreadsheetml.sharedStrings+xml",
        "supported source values fixture should not require a sharedStrings content type");
    entries.at("xl/worksheets/sheet1.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
          R"(<dimension ref="A1:XFD1048576"/>)"
          R"(<sheetData>)"
          R"(<row r="1">)"
          R"(<c r="A1" t="inlineStr"><is><t>source-max-a1</t></is></c>)"
          R"(<c r="B1"><v>1</v></c>)"
          R"(</row>)"
          R"(<row r="2">)"
          R"(<c r="A2" t="inlineStr"><is><t>source-max-a2</t></is></c>)"
          R"(</row>)"
          R"(<row r="1048576">)"
          R"(<c r="XFD1048576" t="inlineStr"><is><t>source-max-edge</t></is></c>)"
          R"(</row>)"
          R"(</sheetData></worksheet>)";
    write_stored_zip_entries(source, entries);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);

    check(sheet.cell_count() == 4,
        "source max-coordinate materialization should load sparse source records only");
    check(!sheet.has_pending_changes(),
        "read-only source max-coordinate materialization should start clean");
    check(!editor.has_pending_changes(),
        "read-only source max-coordinate materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "read-only source max-coordinate materialization should not queue public edits");
    check(editor.pending_materialized_worksheet_names().empty(),
        "read-only source max-coordinate materialization should not expose dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "read-only source max-coordinate materialization should not expose dirty cell count");
    {
        const fastxlsx::CellValue by_position = sheet.get_cell(1048576, 16384);
        const fastxlsx::CellValue by_a1 = sheet.get_cell("XFD1048576");
        check(by_position.kind() == fastxlsx::CellValueKind::Text &&
                by_position.text_value() == "source-max-edge",
            "source max-coordinate materialization should read through row/column overloads");
        check(by_a1.kind() == fastxlsx::CellValueKind::Text &&
                by_a1.text_value() == "source-max-edge",
            "source max-coordinate materialization should read through A1 overloads");
    }
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.size() == 1,
            "source max-coordinate range snapshot should expose the edge record");
        if (edge_cells.size() == 1) {
            check(edge_cells[0].reference.row == 1048576 &&
                    edge_cells[0].reference.column == 16384,
                "source max-coordinate range snapshot should preserve legal boundary coordinates");
            check(edge_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    edge_cells[0].value.text_value() == "source-max-edge",
                "source max-coordinate range snapshot should preserve source text");
        }
    }

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "no-op save_as after source max-coordinate materialization should keep the handle clean");
    check(!editor.has_pending_changes(),
        "no-op save_as after source max-coordinate materialization should keep the editor clean");
    check(editor.pending_change_count() == 0,
        "no-op save_as after source max-coordinate materialization should not create public edits");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "no-op save_as after source max-coordinate materialization should copy source entries");

    sheet.erase_cell("XFD1048576");
    check(!editor.last_edit_error().has_value(),
        "source max-coordinate erase should not create edit diagnostics");
    check(sheet.has_pending_changes(),
        "source max-coordinate erase should dirty the materialized handle");
    check(sheet.cell_count() == 3,
        "source max-coordinate erase should shrink the sparse record count");
    check(!sheet.try_cell(1048576, 16384).has_value(),
        "source max-coordinate erase should remove row/column readback");
    check(threw_fastxlsx_error([&] {
        (void)sheet.get_cell("XFD1048576");
    }), "source max-coordinate get_cell should throw after erase");
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.empty(),
            "source max-coordinate range snapshot should be empty after erase");
    }
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "source max-coordinate erase dirty diagnostics should use source sheet name");
    }
    check(editor.pending_materialized_cell_count() == 3,
        "source max-coordinate erase dirty diagnostics should report remaining sparse records");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "source max-coordinate erase should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "source max-coordinate erase summary should use source names");
            check(!summary.renamed,
                "source max-coordinate erase summary should not be marked renamed");
            check(summary.materialized_dirty && summary.materialized_cell_count == 3,
                "source max-coordinate erase summary should report the shrunken sparse store");
            check(!summary.sheet_data_replaced,
                "source max-coordinate erase summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(erase_output);
    check(!sheet.has_pending_changes(),
        "save_as after source max-coordinate erase should clean the handle");
    check(editor.pending_change_count() == 1,
        "save_as after source max-coordinate erase should count one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "save_as after source max-coordinate erase should clear dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "save_as after source max-coordinate erase should clear dirty cell count");
    check(editor.pending_worksheet_edits().empty(),
        "save_as after source max-coordinate erase should clear summaries");

    const auto erase_entries = fastxlsx::test::read_zip_entries(erase_output);
    const std::string worksheet_xml = erase_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "source max-coordinate erase output should shrink dimension to remaining source records");
    check_not_contains(worksheet_xml, "XFD1048576",
        "source max-coordinate erase output should omit the erased edge reference");
    check_not_contains(worksheet_xml, "source-max-edge",
        "source max-coordinate erase output should omit the erased edge text");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>source-max-a1</t></is></c>)",
        "source max-coordinate erase output should preserve source A1");
    check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "source max-coordinate erase output should preserve source B1");
    check_contains(worksheet_xml,
        R"(<c r="A2" t="inlineStr"><is><t>source-max-a2</t></is></c>)",
        "source max-coordinate erase output should preserve source A2");
    check_contains(erase_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "source max-coordinate erase output should preserve untouched sheets");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "source-max-edge",
        "source max-coordinate erase should not mutate the source package bytes");
}

void test_public_worksheet_editor_materializes_source_max_coordinate_formula_and_erases_edge()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-source-max-coordinate-formula-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-formula-noop-output.xlsx");
    const std::filesystem::path erase_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-formula-erase-output.xlsx");

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    entries.at("xl/worksheets/sheet1.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
          R"(<dimension ref="A1:XFD1048576"/>)"
          R"(<sheetData>)"
          R"(<row r="1">)"
          R"(<c r="A1" t="inlineStr"><is><t>source-formula-a1</t></is></c>)"
          R"(<c r="B1"><v>1</v></c>)"
          R"(</row>)"
          R"(<row r="2">)"
          R"(<c r="A2" t="inlineStr"><is><t>source-formula-a2</t></is></c>)"
          R"(</row>)"
          R"(<row r="1048576">)"
          R"(<c r="XFD1048576"><f>SUM(A1:B1)&amp;"&lt;source-edge&gt;"</f><v>12345</v></c>)"
          R"(</row>)"
          R"(</sheetData></worksheet>)";
    write_stored_zip_entries(source, entries);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "<v>12345</v>",
        "source max-coordinate formula fixture should contain a stale cached value");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);

    check(sheet.cell_count() == 4,
        "source max-coordinate formula materialization should load sparse source records only");
    check(!sheet.has_pending_changes(),
        "read-only source max-coordinate formula materialization should start clean");
    check(!editor.has_pending_changes(),
        "read-only source max-coordinate formula materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "read-only source max-coordinate formula materialization should not queue public edits");
    check(editor.pending_materialized_worksheet_names().empty(),
        "read-only source max-coordinate formula materialization should not expose dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "read-only source max-coordinate formula materialization should not expose dirty cell count");
    {
        const fastxlsx::CellValue by_position = sheet.get_cell(1048576, 16384);
        const fastxlsx::CellValue by_a1 = sheet.get_cell("XFD1048576");
        check(by_position.kind() == fastxlsx::CellValueKind::Formula &&
                by_position.text_value() == R"(SUM(A1:B1)&"<source-edge>")",
            "source max-coordinate formula materialization should ignore stale cached scalar values");
        check(by_a1.kind() == fastxlsx::CellValueKind::Formula &&
                by_a1.text_value() == R"(SUM(A1:B1)&"<source-edge>")",
            "source max-coordinate formula materialization should read formulas through A1 overloads");
    }
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.size() == 1,
            "source max-coordinate formula range snapshot should expose the edge record");
        if (edge_cells.size() == 1) {
            check(edge_cells[0].reference.row == 1048576 &&
                    edge_cells[0].reference.column == 16384,
                "source max-coordinate formula range snapshot should preserve legal boundary coordinates");
            check(edge_cells[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    edge_cells[0].value.text_value() == R"(SUM(A1:B1)&"<source-edge>")",
                "source max-coordinate formula range snapshot should preserve source formula text");
        }
    }

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "no-op save_as after source max-coordinate formula materialization should keep the handle clean");
    check(!editor.has_pending_changes(),
        "no-op save_as after source max-coordinate formula materialization should keep the editor clean");
    check(editor.pending_change_count() == 0,
        "no-op save_as after source max-coordinate formula materialization should not create public edits");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "no-op save_as after source max-coordinate formula materialization should copy source entries");

    sheet.erase_cell("XFD1048576");
    check(!editor.last_edit_error().has_value(),
        "source max-coordinate formula erase should not create edit diagnostics");
    check(sheet.has_pending_changes(),
        "source max-coordinate formula erase should dirty the materialized handle");
    check(sheet.cell_count() == 3,
        "source max-coordinate formula erase should shrink the sparse record count");
    check(!sheet.try_cell(1048576, 16384).has_value(),
        "source max-coordinate formula erase should remove row/column readback");
    check(threw_fastxlsx_error([&] {
        (void)sheet.get_cell("XFD1048576");
    }), "source max-coordinate formula get_cell should throw after erase");
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.empty(),
            "source max-coordinate formula range snapshot should be empty after erase");
    }
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "source max-coordinate formula erase dirty diagnostics should use source sheet name");
    }
    check(editor.pending_materialized_cell_count() == 3,
        "source max-coordinate formula erase dirty diagnostics should report remaining sparse records");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "source max-coordinate formula erase should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "source max-coordinate formula erase summary should use source names");
            check(!summary.renamed,
                "source max-coordinate formula erase summary should not be marked renamed");
            check(summary.materialized_dirty && summary.materialized_cell_count == 3,
                "source max-coordinate formula erase summary should report the shrunken sparse store");
            check(!summary.sheet_data_replaced,
                "source max-coordinate formula erase summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(erase_output);
    check(!sheet.has_pending_changes(),
        "save_as after source max-coordinate formula erase should clean the handle");
    check(editor.pending_change_count() == 1,
        "save_as after source max-coordinate formula erase should count one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "save_as after source max-coordinate formula erase should clear dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "save_as after source max-coordinate formula erase should clear dirty cell count");
    check(editor.pending_worksheet_edits().empty(),
        "save_as after source max-coordinate formula erase should clear summaries");

    const auto erase_entries = fastxlsx::test::read_zip_entries(erase_output);
    const std::string worksheet_xml = erase_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "source max-coordinate formula erase output should shrink dimension to remaining source records");
    check_not_contains(worksheet_xml, "XFD1048576",
        "source max-coordinate formula erase output should omit the erased edge reference");
    check_not_contains(worksheet_xml, "SUM(A1:B1)",
        "source max-coordinate formula erase output should omit the erased edge formula");
    check_not_contains(worksheet_xml, "12345",
        "source max-coordinate formula erase output should omit the stale cached scalar value");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>source-formula-a1</t></is></c>)",
        "source max-coordinate formula erase output should preserve source A1");
    check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "source max-coordinate formula erase output should preserve source B1");
    check_contains(worksheet_xml,
        R"(<c r="A2" t="inlineStr"><is><t>source-formula-a2</t></is></c>)",
        "source max-coordinate formula erase output should preserve source A2");
    check_contains(erase_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "source max-coordinate formula erase output should preserve untouched sheets");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "<v>12345</v>",
        "source max-coordinate formula erase should not mutate the source package bytes");
}

void test_public_worksheet_editor_materializes_source_max_coordinate_shared_string_and_erases_edge()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-sharedstring-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-sharedstring-noop-output.xlsx");
    const std::filesystem::path erase_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-sharedstring-erase-output.xlsx");

    {
        fastxlsx::WorkbookWriterOptions writer_options;
        writer_options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, writer_options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("source-shared-a1"),
            fastxlsx::CellView::number(1.0),
            fastxlsx::CellView::text("source-shared-edge & <max>")});
        data.append_row({fastxlsx::CellView::text("source-shared-a2")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-shared-edge")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    entries.at("xl/sharedStrings.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="4" uniqueCount="4">)"
          R"(<si><t>source-shared-a1</t></si>)"
          R"(<si><t>source-shared-edge &amp; &lt;max&gt;</t></si>)"
          R"(<si><t>source-shared-a2</t></si>)"
          R"(<si><t>keep-shared-edge</t></si>)"
          R"(</sst>)";
    entries.at("xl/worksheets/sheet1.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
          R"(<dimension ref="A1:XFD1048576"/>)"
          R"(<sheetData>)"
          R"(<row r="1">)"
          R"(<c r="A1" t="s"><v>0</v></c>)"
          R"(<c r="B1"><v>1</v></c>)"
          R"(</row>)"
          R"(<row r="2">)"
          R"(<c r="A2" t="s"><v>2</v></c>)"
          R"(</row>)"
          R"(<row r="1048576">)"
          R"(<c r="XFD1048576" t="s"><v>1</v></c>)"
          R"(</row>)"
          R"(</sheetData></worksheet>)";
    write_stored_zip_entries(source, entries);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string shared_strings_before =
        source_entries.at("xl/sharedStrings.xml");
    check_contains(shared_strings_before, "source-shared-edge &amp; &lt;max&gt;",
        "source max-coordinate shared string fixture should contain the edge text");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="XFD1048576" t="s"><v>1</v></c>)",
        "source max-coordinate shared string fixture should store the edge cell as t=s");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);

    check(sheet.cell_count() == 4,
        "source max-coordinate shared string materialization should load sparse source records only");
    check(!sheet.has_pending_changes(),
        "read-only source max-coordinate shared string materialization should start clean");
    check(!editor.has_pending_changes(),
        "read-only source max-coordinate shared string materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "read-only source max-coordinate shared string materialization should not queue public edits");
    check(editor.pending_materialized_worksheet_names().empty(),
        "read-only source max-coordinate shared string materialization should not expose dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "read-only source max-coordinate shared string materialization should not expose dirty cell count");
    {
        const fastxlsx::CellValue by_position = sheet.get_cell(1048576, 16384);
        const fastxlsx::CellValue by_a1 = sheet.get_cell("XFD1048576");
        check(by_position.kind() == fastxlsx::CellValueKind::Text &&
                by_position.text_value() == "source-shared-edge & <max>",
            "source max-coordinate shared string materialization should decode XML entities");
        check(by_a1.kind() == fastxlsx::CellValueKind::Text &&
                by_a1.text_value() == "source-shared-edge & <max>",
            "source max-coordinate shared string materialization should read text through A1 overloads");
    }
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.size() == 1,
            "source max-coordinate shared string range snapshot should expose the edge record");
        if (edge_cells.size() == 1) {
            check(edge_cells[0].reference.row == 1048576 &&
                    edge_cells[0].reference.column == 16384,
                "source max-coordinate shared string range snapshot should preserve legal boundary coordinates");
            check(edge_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    edge_cells[0].value.text_value() == "source-shared-edge & <max>",
                "source max-coordinate shared string range snapshot should preserve source text");
        }
    }

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "no-op save_as after source max-coordinate shared string materialization should keep the handle clean");
    check(!editor.has_pending_changes(),
        "no-op save_as after source max-coordinate shared string materialization should keep the editor clean");
    check(editor.pending_change_count() == 0,
        "no-op save_as after source max-coordinate shared string materialization should not create public edits");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "no-op save_as after source max-coordinate shared string materialization should copy source entries");

    sheet.erase_cell("XFD1048576");
    check(!editor.last_edit_error().has_value(),
        "source max-coordinate shared string erase should not create edit diagnostics");
    check(sheet.has_pending_changes(),
        "source max-coordinate shared string erase should dirty the materialized handle");
    check(sheet.cell_count() == 3,
        "source max-coordinate shared string erase should shrink the sparse record count");
    check(!sheet.try_cell(1048576, 16384).has_value(),
        "source max-coordinate shared string erase should remove row/column readback");
    check(threw_fastxlsx_error([&] {
        (void)sheet.get_cell("XFD1048576");
    }), "source max-coordinate shared string get_cell should throw after erase");
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.empty(),
            "source max-coordinate shared string range snapshot should be empty after erase");
    }
    {
        const std::vector<std::string> names =
            editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "source max-coordinate shared string erase dirty diagnostics should use source sheet name");
    }
    check(editor.pending_materialized_cell_count() == 3,
        "source max-coordinate shared string erase dirty diagnostics should report remaining sparse records");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "source max-coordinate shared string erase should create one dirty summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data" && summary.planned_name == "Data",
                "source max-coordinate shared string erase summary should use source names");
            check(!summary.renamed,
                "source max-coordinate shared string erase summary should not be marked renamed");
            check(summary.materialized_dirty && summary.materialized_cell_count == 3,
                "source max-coordinate shared string erase summary should report the shrunken sparse store");
            check(!summary.sheet_data_replaced,
                "source max-coordinate shared string erase summary should not invent replacement diagnostics");
        }
    }

    editor.save_as(erase_output);
    check(!sheet.has_pending_changes(),
        "save_as after source max-coordinate shared string erase should clean the handle");
    check(editor.pending_change_count() == 1,
        "save_as after source max-coordinate shared string erase should count one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "save_as after source max-coordinate shared string erase should clear dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "save_as after source max-coordinate shared string erase should clear dirty cell count");
    check(editor.pending_worksheet_edits().empty(),
        "save_as after source max-coordinate shared string erase should clear summaries");

    const auto erase_entries = fastxlsx::test::read_zip_entries(erase_output);
    const std::string worksheet_xml = erase_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "source max-coordinate shared string erase output should shrink dimension to remaining source records");
    check_not_contains(worksheet_xml, "XFD1048576",
        "source max-coordinate shared string erase output should omit the erased edge reference");
    check_not_contains(worksheet_xml, "source-shared-edge",
        "source max-coordinate shared string erase output should omit the erased edge text");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="s"><v>0</v></c>)",
        "source max-coordinate shared string erase output should preserve source A1 as shared string index");
    check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "source max-coordinate shared string erase output should preserve source B1");
    check_contains(worksheet_xml,
        R"(<c r="A2" t="s"><v>2</v></c>)",
        "source max-coordinate shared string erase output should preserve source A2 as shared string index");
    check(erase_entries.find("xl/sharedStrings.xml") != erase_entries.end()
            && erase_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "source max-coordinate shared string erase output should preserve source sharedStrings bytes");
    check(erase_entries.at("xl/worksheets/sheet2.xml") ==
            source_entries.at("xl/worksheets/sheet2.xml"),
        "source max-coordinate shared string erase output should preserve untouched sheets byte-for-byte");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="XFD1048576" t="s"><v>1</v></c>)",
        "source max-coordinate shared string erase should not mutate the source package bytes");
}

void test_public_worksheet_editor_materializes_source_max_coordinate_scalar_values_and_erases_edge()
{
    struct SourceMaxCoordinateScalarCase {
        std::string_view name;
        std::string_view edge_cell_xml;
        fastxlsx::CellValueKind expected_kind;
        double expected_number = 0.0;
        bool expected_boolean = false;
        std::string_view absent_payload;
    };

    const std::array<SourceMaxCoordinateScalarCase, 3> cases {{
        {"number",
            R"(<c r="XFD1048576"><v>9000.25</v></c>)",
            fastxlsx::CellValueKind::Number,
            9000.25,
            false,
            "9000.25"},
        {"boolean-false",
            R"(<c r="XFD1048576" t="b"><v>0</v></c>)",
            fastxlsx::CellValueKind::Boolean,
            0.0,
            false,
            R"(t="b")"},
        {"blank",
            R"(<c r="XFD1048576"/>)",
            fastxlsx::CellValueKind::Blank,
            0.0,
            false,
            R"(XFD1048576)"},
    }};

    for (const SourceMaxCoordinateScalarCase& case_info : cases) {
        const std::filesystem::path source = artifact(
            "fastxlsx-workbook-editor-public-source-max-coordinate-scalar-"
            + std::string(case_info.name) + "-source.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-source-max-coordinate-scalar-"
            + std::string(case_info.name) + "-noop-output.xlsx");
        const std::filesystem::path erase_output = artifact(
            "fastxlsx-workbook-editor-public-source-max-coordinate-scalar-"
            + std::string(case_info.name) + "-erase-output.xlsx");

        {
            fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
            fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
            data.append_row({fastxlsx::CellView::text("placeholder")});
            fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
            untouched.append_row({fastxlsx::CellView::text("keep-scalar-edge")});
            writer.close();
        }

        std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
        std::string worksheet_xml =
            std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
            + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
              R"(<dimension ref="A1:XFD1048576"/>)"
              R"(<sheetData>)"
              R"(<row r="1">)"
              R"(<c r="A1" t="inlineStr"><is><t>source-scalar-a1</t></is></c>)"
              R"(<c r="B1"><v>1</v></c>)"
              R"(</row>)"
              R"(<row r="2">)"
              R"(<c r="A2" t="inlineStr"><is><t>source-scalar-a2</t></is></c>)"
              R"(</row>)"
              R"(<row r="1048576">)";
        worksheet_xml.append(case_info.edge_cell_xml.data(), case_info.edge_cell_xml.size());
        worksheet_xml += R"(</row></sheetData></worksheet>)";
        entries.at("xl/worksheets/sheet1.xml") = worksheet_xml;
        write_stored_zip_entries(source, entries);
        const auto source_entries = fastxlsx::test::read_zip_entries(source);
        check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
            case_info.edge_cell_xml,
            "source max-coordinate scalar fixture should contain the edge cell");

        fastxlsx::WorksheetEditorOptions options;
        options.max_cells = 8;

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);

        check(sheet.cell_count() == 4,
            "source max-coordinate scalar materialization should load sparse source records only");
        check(!sheet.has_pending_changes(),
            "read-only source max-coordinate scalar materialization should start clean");
        check(!editor.has_pending_changes(),
            "read-only source max-coordinate scalar materialization should not dirty WorkbookEditor");
        check(editor.pending_change_count() == 0,
            "read-only source max-coordinate scalar materialization should not queue public edits");
        check(editor.pending_materialized_worksheet_names().empty(),
            "read-only source max-coordinate scalar materialization should not expose dirty names");
        check(editor.pending_materialized_cell_count() == 0,
            "read-only source max-coordinate scalar materialization should not expose dirty cell count");

        const fastxlsx::CellValue by_position = sheet.get_cell(1048576, 16384);
        const fastxlsx::CellValue by_a1 = sheet.get_cell("XFD1048576");
        if (case_info.expected_kind == fastxlsx::CellValueKind::Number) {
            check(by_position.kind() == fastxlsx::CellValueKind::Number &&
                    by_position.number_value() == case_info.expected_number,
                "source max-coordinate number should materialize through row/column overloads");
            check(by_a1.kind() == fastxlsx::CellValueKind::Number &&
                    by_a1.number_value() == case_info.expected_number,
                "source max-coordinate number should materialize through A1 overloads");
        } else if (case_info.expected_kind == fastxlsx::CellValueKind::Boolean) {
            check(by_position.kind() == fastxlsx::CellValueKind::Boolean &&
                    by_position.boolean_value() == case_info.expected_boolean,
                "source max-coordinate boolean should materialize through row/column overloads");
            check(by_a1.kind() == fastxlsx::CellValueKind::Boolean &&
                    by_a1.boolean_value() == case_info.expected_boolean,
                "source max-coordinate boolean should materialize through A1 overloads");
        } else {
            check(by_position.kind() == fastxlsx::CellValueKind::Blank,
                "source max-coordinate blank should materialize through row/column overloads");
            check(by_a1.kind() == fastxlsx::CellValueKind::Blank,
                "source max-coordinate blank should materialize through A1 overloads");
        }
        {
            const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
                sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
            check(edge_cells.size() == 1,
                "source max-coordinate scalar range snapshot should expose the edge record");
            if (edge_cells.size() == 1) {
                check(edge_cells[0].reference.row == 1048576 &&
                        edge_cells[0].reference.column == 16384,
                    "source max-coordinate scalar range snapshot should preserve legal boundary coordinates");
                check(edge_cells[0].value.kind() == case_info.expected_kind,
                    "source max-coordinate scalar range snapshot should preserve the source value kind");
            }
        }

        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "no-op save_as after source max-coordinate scalar materialization should keep the handle clean");
        check(!editor.has_pending_changes(),
            "no-op save_as after source max-coordinate scalar materialization should keep the editor clean");
        check(editor.pending_change_count() == 0,
            "no-op save_as after source max-coordinate scalar materialization should not create public edits");
        check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
            "no-op save_as after source max-coordinate scalar materialization should copy source entries");

        sheet.erase_cell("XFD1048576");
        check(!editor.last_edit_error().has_value(),
            "source max-coordinate scalar erase should not create edit diagnostics");
        check(sheet.has_pending_changes(),
            "source max-coordinate scalar erase should dirty the materialized handle");
        check(sheet.cell_count() == 3,
            "source max-coordinate scalar erase should shrink the sparse record count");
        check(!sheet.try_cell(1048576, 16384).has_value(),
            "source max-coordinate scalar erase should remove row/column readback");
        check(threw_fastxlsx_error([&] {
            (void)sheet.get_cell("XFD1048576");
        }), "source max-coordinate scalar get_cell should throw after erase");
        {
            const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
                sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
            check(edge_cells.empty(),
                "source max-coordinate scalar range snapshot should be empty after erase");
        }
        {
            const std::vector<std::string> names =
                editor.pending_materialized_worksheet_names();
            check(names.size() == 1 && names[0] == "Data",
                "source max-coordinate scalar erase dirty diagnostics should use source sheet name");
        }
        check(editor.pending_materialized_cell_count() == 3,
            "source max-coordinate scalar erase dirty diagnostics should report remaining sparse records");

        editor.save_as(erase_output);
        check(!sheet.has_pending_changes(),
            "save_as after source max-coordinate scalar erase should clean the handle");
        check(editor.pending_change_count() == 1,
            "save_as after source max-coordinate scalar erase should count one materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty(),
            "save_as after source max-coordinate scalar erase should clear dirty names");
        check(editor.pending_materialized_cell_count() == 0,
            "save_as after source max-coordinate scalar erase should clear dirty cell count");
        check(editor.pending_worksheet_edits().empty(),
            "save_as after source max-coordinate scalar erase should clear summaries");

        const auto erase_entries = fastxlsx::test::read_zip_entries(erase_output);
        const std::string erase_worksheet_xml = erase_entries.at("xl/worksheets/sheet1.xml");
        check_contains(erase_worksheet_xml, R"(<dimension ref="A1:B2"/>)",
            "source max-coordinate scalar erase output should shrink dimension to remaining source records");
        check_not_contains(erase_worksheet_xml, "XFD1048576",
            "source max-coordinate scalar erase output should omit the erased edge reference");
        if (case_info.name != "blank") {
            check_not_contains(erase_worksheet_xml, case_info.absent_payload,
                "source max-coordinate scalar erase output should omit the erased edge payload");
        }
        check_contains(erase_worksheet_xml,
            R"(<c r="A1" t="inlineStr"><is><t>source-scalar-a1</t></is></c>)",
            "source max-coordinate scalar erase output should preserve source A1");
        check_contains(erase_worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
            "source max-coordinate scalar erase output should preserve source B1");
        check_contains(erase_worksheet_xml,
            R"(<c r="A2" t="inlineStr"><is><t>source-scalar-a2</t></is></c>)",
            "source max-coordinate scalar erase output should preserve source A2");
        check(erase_entries.at("xl/worksheets/sheet2.xml") ==
                source_entries.at("xl/worksheets/sheet2.xml"),
            "source max-coordinate scalar erase output should preserve untouched sheets byte-for-byte");
        check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
            case_info.edge_cell_xml,
            "source max-coordinate scalar erase should not mutate the source package bytes");
    }
}

void test_public_worksheet_editor_materializes_source_max_coordinate_empty_inline_strings_and_erases_edge()
{
    struct SourceMaxCoordinateInlineCase {
        std::string_view name;
        std::string_view edge_cell_xml;
        fastxlsx::CellValueKind expected_kind;
        std::string_view absent_payload;
    };

    const std::array<SourceMaxCoordinateInlineCase, 2> cases {{
        {"empty-text",
            R"(<c r="XFD1048576" t="inlineStr"><is><t></t></is></c>)",
            fastxlsx::CellValueKind::Text,
            R"(<t></t>)"},
        {"inline-without-text",
            R"(<c r="XFD1048576" t="inlineStr"><is/></c>)",
            fastxlsx::CellValueKind::Blank,
            R"(<is/>)"},
    }};

    for (const SourceMaxCoordinateInlineCase& case_info : cases) {
        const std::filesystem::path source = artifact(
            "fastxlsx-workbook-editor-public-source-max-coordinate-empty-inline-"
            + std::string(case_info.name) + "-source.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-source-max-coordinate-empty-inline-"
            + std::string(case_info.name) + "-noop-output.xlsx");
        const std::filesystem::path erase_output = artifact(
            "fastxlsx-workbook-editor-public-source-max-coordinate-empty-inline-"
            + std::string(case_info.name) + "-erase-output.xlsx");

        {
            fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
            fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
            data.append_row({fastxlsx::CellView::text("placeholder")});
            fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
            untouched.append_row({fastxlsx::CellView::text("keep-empty-inline-edge")});
            writer.close();
        }

        std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
        std::string worksheet_xml =
            std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
            + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
              R"(<dimension ref="A1:XFD1048576"/>)"
              R"(<sheetData>)"
              R"(<row r="1">)"
              R"(<c r="A1" t="inlineStr"><is><t>source-empty-inline-a1</t></is></c>)"
              R"(<c r="B1"><v>1</v></c>)"
              R"(</row>)"
              R"(<row r="2">)"
              R"(<c r="A2" t="inlineStr"><is><t>source-empty-inline-a2</t></is></c>)"
              R"(</row>)"
              R"(<row r="1048576">)";
        worksheet_xml.append(case_info.edge_cell_xml.data(), case_info.edge_cell_xml.size());
        worksheet_xml += R"(</row></sheetData></worksheet>)";
        entries.at("xl/worksheets/sheet1.xml") = worksheet_xml;
        write_stored_zip_entries(source, entries);
        const auto source_entries = fastxlsx::test::read_zip_entries(source);
        check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
            case_info.edge_cell_xml,
            "source max-coordinate empty inline fixture should contain the edge cell");

        fastxlsx::WorksheetEditorOptions options;
        options.max_cells = 8;

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);

        check(sheet.cell_count() == 4,
            "source max-coordinate empty inline materialization should load sparse source records only");
        check(!sheet.has_pending_changes(),
            "read-only source max-coordinate empty inline materialization should start clean");
        check(!editor.has_pending_changes(),
            "read-only source max-coordinate empty inline materialization should not dirty WorkbookEditor");
        check(editor.pending_change_count() == 0,
            "read-only source max-coordinate empty inline materialization should not queue public edits");
        check(editor.pending_materialized_worksheet_names().empty(),
            "read-only source max-coordinate empty inline materialization should not expose dirty names");
        check(editor.pending_materialized_cell_count() == 0,
            "read-only source max-coordinate empty inline materialization should not expose dirty cell count");

        const fastxlsx::CellValue by_position = sheet.get_cell(1048576, 16384);
        const fastxlsx::CellValue by_a1 = sheet.get_cell("XFD1048576");
        if (case_info.expected_kind == fastxlsx::CellValueKind::Text) {
            check(by_position.kind() == fastxlsx::CellValueKind::Text &&
                    by_position.text_value().empty(),
                "source max-coordinate empty inline text should materialize through row/column overloads");
            check(by_a1.kind() == fastxlsx::CellValueKind::Text &&
                    by_a1.text_value().empty(),
                "source max-coordinate empty inline text should materialize through A1 overloads");
        } else {
            check(by_position.kind() == fastxlsx::CellValueKind::Blank,
                "source max-coordinate inline string without text should materialize through row/column overloads");
            check(by_a1.kind() == fastxlsx::CellValueKind::Blank,
                "source max-coordinate inline string without text should materialize through A1 overloads");
        }
        {
            const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
                sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
            check(edge_cells.size() == 1,
                "source max-coordinate empty inline range snapshot should expose the edge record");
            if (edge_cells.size() == 1) {
                check(edge_cells[0].reference.row == 1048576 &&
                        edge_cells[0].reference.column == 16384,
                    "source max-coordinate empty inline range snapshot should preserve legal boundary coordinates");
                check(edge_cells[0].value.kind() == case_info.expected_kind,
                    "source max-coordinate empty inline range snapshot should preserve the source value kind");
                if (case_info.expected_kind == fastxlsx::CellValueKind::Text) {
                    check(edge_cells[0].value.text_value().empty(),
                        "source max-coordinate empty inline range snapshot should preserve empty text");
                }
            }
        }

        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "no-op save_as after source max-coordinate empty inline materialization should keep the handle clean");
        check(!editor.has_pending_changes(),
            "no-op save_as after source max-coordinate empty inline materialization should keep the editor clean");
        check(editor.pending_change_count() == 0,
            "no-op save_as after source max-coordinate empty inline materialization should not create public edits");
        check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
            "no-op save_as after source max-coordinate empty inline materialization should copy source entries");

        sheet.erase_cell("XFD1048576");
        check(!editor.last_edit_error().has_value(),
            "source max-coordinate empty inline erase should not create edit diagnostics");
        check(sheet.has_pending_changes(),
            "source max-coordinate empty inline erase should dirty the materialized handle");
        check(sheet.cell_count() == 3,
            "source max-coordinate empty inline erase should shrink the sparse record count");
        check(!sheet.try_cell(1048576, 16384).has_value(),
            "source max-coordinate empty inline erase should remove row/column readback");
        check(threw_fastxlsx_error([&] {
            (void)sheet.get_cell("XFD1048576");
        }), "source max-coordinate empty inline get_cell should throw after erase");
        {
            const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
                sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
            check(edge_cells.empty(),
                "source max-coordinate empty inline range snapshot should be empty after erase");
        }
        {
            const std::vector<std::string> names =
                editor.pending_materialized_worksheet_names();
            check(names.size() == 1 && names[0] == "Data",
                "source max-coordinate empty inline erase dirty diagnostics should use source sheet name");
        }
        check(editor.pending_materialized_cell_count() == 3,
            "source max-coordinate empty inline erase dirty diagnostics should report remaining sparse records");

        editor.save_as(erase_output);
        check(!sheet.has_pending_changes(),
            "save_as after source max-coordinate empty inline erase should clean the handle");
        check(editor.pending_change_count() == 1,
            "save_as after source max-coordinate empty inline erase should count one materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty(),
            "save_as after source max-coordinate empty inline erase should clear dirty names");
        check(editor.pending_materialized_cell_count() == 0,
            "save_as after source max-coordinate empty inline erase should clear dirty cell count");
        check(editor.pending_worksheet_edits().empty(),
            "save_as after source max-coordinate empty inline erase should clear summaries");

        const auto erase_entries = fastxlsx::test::read_zip_entries(erase_output);
        const std::string erase_worksheet_xml = erase_entries.at("xl/worksheets/sheet1.xml");
        check_contains(erase_worksheet_xml, R"(<dimension ref="A1:B2"/>)",
            "source max-coordinate empty inline erase output should shrink dimension to remaining source records");
        check_not_contains(erase_worksheet_xml, "XFD1048576",
            "source max-coordinate empty inline erase output should omit the erased edge reference");
        check_not_contains(erase_worksheet_xml, case_info.absent_payload,
            "source max-coordinate empty inline erase output should omit the erased edge payload");
        check_contains(erase_worksheet_xml,
            R"(<c r="A1" t="inlineStr"><is><t>source-empty-inline-a1</t></is></c>)",
            "source max-coordinate empty inline erase output should preserve source A1");
        check_contains(erase_worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
            "source max-coordinate empty inline erase output should preserve source B1");
        check_contains(erase_worksheet_xml,
            R"(<c r="A2" t="inlineStr"><is><t>source-empty-inline-a2</t></is></c>)",
            "source max-coordinate empty inline erase output should preserve source A2");
        check(erase_entries.at("xl/worksheets/sheet2.xml") ==
                source_entries.at("xl/worksheets/sheet2.xml"),
            "source max-coordinate empty inline erase output should preserve untouched sheets byte-for-byte");
        check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
            case_info.edge_cell_xml,
            "source max-coordinate empty inline erase should not mutate the source package bytes");
    }
}

void test_public_worksheet_editor_materializes_source_max_coordinate_rich_shared_string_and_erases_edge()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-rich-shared-string-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-rich-shared-string-noop-output.xlsx");
    const std::filesystem::path erase_output =
        artifact("fastxlsx-workbook-editor-public-source-max-coordinate-rich-shared-string-erase-output.xlsx");

    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::number(7.0)});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    const std::string rich_shared_strings =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="3" uniqueCount="3">)"
        R"(<si><t>source-rich-a1</t></si>)"
        R"(<si><r><t>rich-</t></r><r><t>A&amp;B </t></r><r><t>&lt;edge&gt;</t></r><rPh sb="0" eb="1"><t>ignored-phonetic</t></rPh><phoneticPr fontId="1"/><extLst><ext uri="{fastxlsx-test}"><t>ignored-ext</t></ext></extLst></si>)"
        R"(<si><t>source-rich-a2</t></si>)"
        R"(</sst>)";
    entries.at("xl/sharedStrings.xml") = rich_shared_strings;
    entries.at("xl/worksheets/sheet1.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
          R"(<dimension ref="A1:XFD1048576"/>)"
          R"(<sheetData>)"
          R"(<row r="1">)"
          R"(<c r="A1" t="s"><v>0</v></c>)"
          R"(<c r="B1"><v>1</v></c>)"
          R"(</row>)"
          R"(<row r="2">)"
          R"(<c r="A2" t="s"><v>2</v></c>)"
          R"(</row>)"
          R"(<row r="1048576">)"
          R"(<c r="XFD1048576" t="s"><v>1</v></c>)"
          R"(</row></sheetData></worksheet>)";
    write_stored_zip_entries(source, entries);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string shared_strings_before = source_entries.at("xl/sharedStrings.xml");
    check_contains(shared_strings_before, R"(<rPh sb="0" eb="1"><t>ignored-phonetic</t></rPh>)",
        "source rich shared string fixture should contain ignored phonetic text");
    check_contains(shared_strings_before, R"(<ext uri="{fastxlsx-test}"><t>ignored-ext</t></ext>)",
        "source rich shared string fixture should contain ignored extension text");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="XFD1048576" t="s"><v>1</v></c>)",
        "source max-coordinate rich shared string fixture should store the edge as t=s");

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 8;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);

    check(sheet.cell_count() == 4,
        "source max-coordinate rich shared string materialization should load sparse source records only");
    check(!sheet.has_pending_changes(),
        "read-only source max-coordinate rich shared string materialization should start clean");
    check(!editor.has_pending_changes(),
        "read-only source max-coordinate rich shared string materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "read-only source max-coordinate rich shared string materialization should not queue public edits");
    check(editor.pending_materialized_worksheet_names().empty(),
        "read-only source max-coordinate rich shared string materialization should not expose dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "read-only source max-coordinate rich shared string materialization should not expose dirty cell count");

    const fastxlsx::CellValue by_position = sheet.get_cell(1048576, 16384);
    const fastxlsx::CellValue by_a1 = sheet.get_cell("XFD1048576");
    check(by_position.kind() == fastxlsx::CellValueKind::Text &&
            by_position.text_value() == "rich-A&B <edge>",
        "source max-coordinate rich shared string should flatten runs through row/column overloads");
    check(by_a1.kind() == fastxlsx::CellValueKind::Text &&
            by_a1.text_value() == "rich-A&B <edge>",
        "source max-coordinate rich shared string should flatten runs through A1 overloads");
    check(sheet.get_cell("A1").text_value() == "source-rich-a1",
        "source rich shared string fixture should materialize A1 beside the edge");
    check(sheet.get_cell("A2").text_value() == "source-rich-a2",
        "source rich shared string fixture should materialize A2 beside the edge");
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.size() == 1,
            "source max-coordinate rich shared string range snapshot should expose the edge record");
        if (edge_cells.size() == 1) {
            check(edge_cells[0].reference.row == 1048576 &&
                    edge_cells[0].reference.column == 16384,
                "source max-coordinate rich shared string range snapshot should preserve legal boundary coordinates");
            check(edge_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    edge_cells[0].value.text_value() == "rich-A&B <edge>",
                "source max-coordinate rich shared string range snapshot should preserve flattened text");
        }
    }

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "no-op save_as after source max-coordinate rich shared string materialization should keep the handle clean");
    check(!editor.has_pending_changes(),
        "no-op save_as after source max-coordinate rich shared string materialization should keep the editor clean");
    check(editor.pending_change_count() == 0,
        "no-op save_as after source max-coordinate rich shared string materialization should not create public edits");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "no-op save_as after source max-coordinate rich shared string materialization should copy source entries");

    sheet.erase_cell("XFD1048576");
    check(!editor.last_edit_error().has_value(),
        "source max-coordinate rich shared string erase should not create edit diagnostics");
    check(sheet.has_pending_changes(),
        "source max-coordinate rich shared string erase should dirty the materialized handle");
    check(sheet.cell_count() == 3,
        "source max-coordinate rich shared string erase should shrink the sparse record count");
    check(!sheet.try_cell(1048576, 16384).has_value(),
        "source max-coordinate rich shared string erase should remove row/column readback");
    check(threw_fastxlsx_error([&] {
        (void)sheet.get_cell("XFD1048576");
    }), "source max-coordinate rich shared string get_cell should throw after erase");
    {
        const std::vector<fastxlsx::WorksheetCellSnapshot> edge_cells =
            sheet.sparse_cells(fastxlsx::CellRange {1048576, 16384, 1048576, 16384});
        check(edge_cells.empty(),
            "source max-coordinate rich shared string range snapshot should be empty after erase");
    }
    {
        const std::vector<std::string> names = editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "source max-coordinate rich shared string erase dirty diagnostics should use source sheet name");
    }
    check(editor.pending_materialized_cell_count() == 3,
        "source max-coordinate rich shared string erase dirty diagnostics should report remaining sparse records");

    editor.save_as(erase_output);
    check(!sheet.has_pending_changes(),
        "save_as after source max-coordinate rich shared string erase should clean the handle");
    check(editor.pending_change_count() == 1,
        "save_as after source max-coordinate rich shared string erase should count one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "save_as after source max-coordinate rich shared string erase should clear dirty names");
    check(editor.pending_materialized_cell_count() == 0,
        "save_as after source max-coordinate rich shared string erase should clear dirty cell count");
    check(editor.pending_worksheet_edits().empty(),
        "save_as after source max-coordinate rich shared string erase should clear summaries");

    const auto erase_entries = fastxlsx::test::read_zip_entries(erase_output);
    const std::string erase_worksheet_xml = erase_entries.at("xl/worksheets/sheet1.xml");
    check_contains(erase_worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "source max-coordinate rich shared string erase output should shrink dimension to remaining source records");
    check_not_contains(erase_worksheet_xml, "XFD1048576",
        "source max-coordinate rich shared string erase output should omit the erased edge reference");
    check_not_contains(erase_worksheet_xml, "rich-A&amp;B",
        "source max-coordinate rich shared string erase output should omit the erased flattened text");
    check_contains(erase_worksheet_xml,
        R"(<c r="A1" t="s"><v>0</v></c>)",
        "source max-coordinate rich shared string erase output should project source A1 as shared string index");
    check_contains(erase_worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "source max-coordinate rich shared string erase output should preserve source B1");
    check_contains(erase_worksheet_xml,
        R"(<c r="A2" t="s"><v>2</v></c>)",
        "source max-coordinate rich shared string erase output should project source A2 as shared string index");
    check(erase_entries.find("xl/sharedStrings.xml") != erase_entries.end() &&
            erase_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "source max-coordinate rich shared string erase output should preserve source sharedStrings bytes");
    check(erase_entries.at("xl/worksheets/sheet2.xml") ==
            source_entries.at("xl/worksheets/sheet2.xml"),
        "source max-coordinate rich shared string erase output should preserve untouched sheets byte-for-byte");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="XFD1048576" t="s"><v>1</v></c>)",
        "source max-coordinate rich shared string erase should not mutate the source package bytes");
}

} // namespace

int main()
{
    try {
        test_public_worksheet_editor_materializes_source_max_coordinate_and_erases_edge();
        test_public_worksheet_editor_materializes_source_max_coordinate_formula_and_erases_edge();
        test_public_worksheet_editor_materializes_source_max_coordinate_shared_string_and_erases_edge();
        test_public_worksheet_editor_materializes_source_max_coordinate_scalar_values_and_erases_edge();
        test_public_worksheet_editor_materializes_source_max_coordinate_empty_inline_strings_and_erases_edge();
        test_public_worksheet_editor_materializes_source_max_coordinate_rich_shared_string_and_erases_edge();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor source-success max-coordinate check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor source-success max-coordinate tests passed\n");
    return 0;
}
