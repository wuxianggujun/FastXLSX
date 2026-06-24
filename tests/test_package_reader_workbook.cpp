#include "test_package_reader_common.hpp"

void test_package_reader_ingests_content_types_and_relationships()
{
    const std::filesystem::path path = output_path("fastxlsx-package-reader-opc.xlsx");

    const std::string content_types = R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName='/xl/drawings/drawing1.xml' ContentType='application/vnd.openxmlformats-officedocument.drawing+xml'/>)"
        R"(</Types>)";
    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>)"
        R"(</Relationships>)";
    const std::string worksheet_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id='rId1' Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing' Target='../drawings/drawing1.xml'/>)"
        R"(<Relationship Id='rId2' Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink' Target='https://example.test/path?a=1&amp;b=2' TargetMode='External'/>)"
        R"(</Relationships>)";

    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", "<workbook/>"},
            {"xl/_rels/workbook.xml.rels", workbook_relationships},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
            {"xl/worksheets/_rels/sheet1.xml.rels", worksheet_relationships},
            {"xl/drawings/drawing1.xml", "<xdr:wsDr/>"},
            {"custom/opaque.bin", "opaque"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);

    check(reader.content_types().defaults().size() == 2,
        "content type defaults should be parsed");
    check(reader.content_types().overrides().size() == 3,
        "content type overrides should be parsed");

    const auto* workbook = reader.part_index().find_part(
        fastxlsx::detail::PartName("/xl/workbook.xml"));
    check(workbook != nullptr, "part index should include workbook");
    check(workbook->content_type
            == "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml",
        "workbook content type should come from override");

    const auto* drawing = reader.part_index().find_part(
        fastxlsx::detail::PartName("/xl/drawings/drawing1.xml"));
    check(drawing != nullptr, "part index should include drawing part");
    check(drawing->content_type == "application/vnd.openxmlformats-officedocument.drawing+xml",
        "drawing content type should come from single-quoted override");

    const auto* unknown = reader.part_index().find_part(
        fastxlsx::detail::PartName("/custom/opaque.bin"));
    check(unknown != nullptr, "part index should include unknown part");
    check(unknown->content_type == "application/octet-stream",
        "unknown part content type should be resolved from default");
    check(unknown->preserve_original && !unknown->dirty && !unknown->generated,
        "unknown part should remain copy-original metadata");

    check(reader.package_relationships().size() == 1,
        "package relationships should be parsed");
    check(reader.package_relationships().find_by_id("rId1")->target == "xl/workbook.xml",
        "package relationship target mismatch");

    const auto* workbook_rels = reader.relationships_for(
        fastxlsx::detail::PartName("/xl/workbook.xml"));
    check(workbook_rels != nullptr, "workbook relationships should be attached to workbook");
    check(workbook_rels->size() == 2, "workbook relationship count mismatch");
    check(workbook_rels->find_by_id("rId1")->target == "worksheets/sheet1.xml",
        "workbook worksheet relationship target mismatch");

    const auto* worksheet_rels = reader.relationships_for(
        fastxlsx::detail::PartName("/xl/worksheets/sheet1.xml"));
    check(worksheet_rels != nullptr, "worksheet relationships should be attached to worksheet");
    check(worksheet_rels->size() == 2, "worksheet relationship count mismatch");
    const auto* hyperlink = worksheet_rels->find_by_id("rId2");
    check(hyperlink != nullptr, "worksheet external hyperlink relationship should exist");
    check(hyperlink->target == "https://example.test/path?a=1&b=2",
        "relationship target XML entity should be unescaped");
    check(hyperlink->target_mode == fastxlsx::detail::Relationship::TargetMode::External,
        "relationship TargetMode should be parsed");

    const fastxlsx::detail::RelationshipGraph graph = reader.relationship_graph();
    check(graph.package_relationships().size() == 1,
        "relationship graph should include package relationships");
    const auto* graph_worksheet_rels =
        graph.relationships_for(fastxlsx::detail::PartName("/xl/worksheets/sheet1.xml"));
    check(graph_worksheet_rels != nullptr,
        "relationship graph should include worksheet relationships");
    check(graph_worksheet_rels->find_by_id("rId2")->target_mode
            == fastxlsx::detail::Relationship::TargetMode::External,
        "relationship graph should preserve external target mode");
}

void test_package_reader_resolves_workbook_sheet_catalog()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-workbook-sheets.xlsx");

    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet space.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet3.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet4.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(</Types>)";
    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/./workbook.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet%20space.xml"/>)"
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="/xl/worksheets/sheet3.xml"/>)"
        R"(<Relationship Id="rId4" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="./worksheets/../worksheets/sheet4.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:rel="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<extLst xmlns:r="urn:fastxlsx:not-relationships"><ext><sheet name="Ignored Outer" sheetId="900" r:id="not-a-sheet-rel"/></ext></extLst>)"
        R"(<extLst><ext><sheets><sheet name="Ignored Decoy Catalog" sheetId="902" rel:id="rId3"/></sheets></ext></extLst>)"
        R"(<sheets>)"
        R"(<extLst><ext><sheet name="Ignored Nested" sheetId="901" rel:id="missingNestedRel"/></ext></extLst>)"
        R"(<sheet name="Sales &amp; QA" sheetId="1" r:id="rId1"/>)"
        R"(<sheet name="Ops &#x2603;" sheetId="2" r:id="rId2"/>)"
        R"(<sheet name="Alt Prefix" sheetId="3" rel:id="rId3"/>)"
        R"(<sheet name="Dot Segments" sheetId="4" r:id="rId4"/>)"
        R"(</sheets>)"
        R"(</workbook>)";

    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", workbook},
            {"xl/_rels/workbook.xml.rels", workbook_relationships},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
            {"xl/worksheets/sheet space.xml", "<worksheet/>"},
            {"xl/worksheets/sheet3.xml", "<worksheet/>"},
            {"xl/worksheets/sheet4.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);
    const auto* package_workbook_relationship =
        reader.package_relationships().find_by_id("rId1");
    check(package_workbook_relationship != nullptr
            && package_workbook_relationship->target == "xl/./workbook.xml",
        "package reader should preserve dot-segment officeDocument target text");
    const std::vector<fastxlsx::detail::WorkbookSheetReference> sheets =
        reader.workbook_sheets();
    check(sheets.size() == 4,
        "workbook sheet catalog should expose only direct workbook sheets");
    check(sheets[0].name == "Sales & QA",
        "workbook sheet catalog should unescape sheet names");
    check(sheets[0].sheet_id == "1",
        "workbook sheet catalog should preserve sheetId");
    check(sheets[0].relationship_id == "rId1",
        "workbook sheet catalog should preserve relationship id");
    check(sheets[0].part_name
            == fastxlsx::detail::PartName("/xl/worksheets/sheet1.xml"),
        "workbook sheet catalog should resolve relative worksheet targets");

    const std::string snowman_sheet_name = std::string("Ops ") + "\xe2\x98\x83";
    check(sheets[1].name == snowman_sheet_name,
        "workbook sheet catalog should decode numeric character references");
    check(sheets[1].part_name
            == fastxlsx::detail::PartName("/xl/worksheets/sheet space.xml"),
        "workbook sheet catalog should decode percent-encoded worksheet targets");
    check(sheets[2].name == "Alt Prefix",
        "workbook sheet catalog should accept alternate relationship namespace prefixes");
    check(sheets[2].relationship_id == "rId3",
        "workbook sheet catalog should preserve alternate-prefix relationship ids");
    check(sheets[2].part_name
            == fastxlsx::detail::PartName("/xl/worksheets/sheet3.xml"),
        "workbook sheet catalog should resolve absolute alternate-prefix worksheet targets");
    check(sheets[3].name == "Dot Segments",
        "workbook sheet catalog should preserve dot-segment sheet names");
    check(sheets[3].relationship_id == "rId4",
        "workbook sheet catalog should preserve dot-segment relationship ids");
    check(sheets[3].part_name
            == fastxlsx::detail::PartName("/xl/worksheets/sheet4.xml"),
        "workbook sheet catalog should normalize dot-segment worksheet targets");
    check(reader.worksheet_part_by_sheet_name("Sales & QA")
            == fastxlsx::detail::PartName("/xl/worksheets/sheet1.xml"),
        "sheet-name lookup should return the matching worksheet part");
    check(reader.worksheet_part_by_sheet_name(snowman_sheet_name)
            == fastxlsx::detail::PartName("/xl/worksheets/sheet space.xml"),
        "sheet-name lookup should support decoded UTF-8 names");
    check(reader.worksheet_part_by_sheet_name("Alt Prefix")
            == fastxlsx::detail::PartName("/xl/worksheets/sheet3.xml"),
        "sheet-name lookup should support alternate prefixes with absolute worksheet targets");
    check(reader.worksheet_part_by_sheet_name("Dot Segments")
            == fastxlsx::detail::PartName("/xl/worksheets/sheet4.xml"),
        "sheet-name lookup should support normalized dot-segment worksheet targets");
    const std::string planned_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Planned &amp; QA" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    const std::vector<fastxlsx::detail::WorkbookSheetReference> planned_sheets =
        reader.workbook_sheets_from_xml(planned_workbook);
    check(planned_sheets.size() == 1,
        "planned workbook sheet catalog should parse caller-provided workbook XML");
    check(planned_sheets[0].name == "Planned & QA",
        "planned workbook sheet catalog should use planned sheet names");
    check(planned_sheets[0].part_name
            == fastxlsx::detail::PartName("/xl/worksheets/sheet1.xml"),
        "planned workbook sheet catalog should reuse source workbook relationships");
    check(reader.worksheet_part_by_sheet_name_from_xml("Planned & QA", planned_workbook)
            == fastxlsx::detail::PartName("/xl/worksheets/sheet1.xml"),
        "planned sheet-name lookup should resolve against caller-provided workbook XML");
    const std::string planned_scoped_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships")"
        R"( xmlns:rel="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheet name="Planned Ignored Outer" sheetId="900" r:id="rId1"/>)"
        R"(<extLst><ext><sheets>)"
        R"(<sheet name="Planned Ignored Decoy Catalog" sheetId="901" rel:id="rId3"/>)"
        R"(</sheets></ext></extLst>)"
        R"(<sheets>)"
        R"(<extLst><ext>)"
        R"(<sheet name="Planned Ignored Nested" sheetId="902" rel:id="rId2"/>)"
        R"(</ext></extLst>)"
        R"(<sheet name="Planned Direct" sheetId="4" r:id="rId4"/>)"
        R"(</sheets>)"
        R"(</workbook>)";
    const std::vector<fastxlsx::detail::WorkbookSheetReference> planned_scoped_sheets =
        reader.workbook_sheets_from_xml(planned_scoped_workbook);
    check(planned_scoped_sheets.size() == 1,
        "planned workbook sheet catalog should expose only direct workbook sheets");
    check(planned_scoped_sheets[0].name == "Planned Direct",
        "planned workbook sheet catalog should ignore decoy sheet tags");
    check(planned_scoped_sheets[0].part_name
            == fastxlsx::detail::PartName("/xl/worksheets/sheet4.xml"),
        "planned workbook sheet catalog should resolve direct scoped worksheet targets");
    check(reader.worksheet_part_by_sheet_name_from_xml("Planned Direct",
              planned_scoped_workbook)
            == fastxlsx::detail::PartName("/xl/worksheets/sheet4.xml"),
        "planned sheet-name lookup should resolve direct scoped workbook sheets");
    const std::string planned_alternate_prefix_workbook =
        R"(<workbook xmlns:rel="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Planned Alt Prefix" sheetId="3" rel:id="rId3"/></sheets>)"
        R"(</workbook>)";
    const std::vector<fastxlsx::detail::WorkbookSheetReference>
        planned_alternate_prefix_sheets =
            reader.workbook_sheets_from_xml(planned_alternate_prefix_workbook);
    check(planned_alternate_prefix_sheets.size() == 1,
        "planned workbook sheet catalog should accept alternate relationship prefixes");
    check(planned_alternate_prefix_sheets[0].name == "Planned Alt Prefix",
        "planned workbook sheet catalog should preserve alternate-prefix sheet names");
    check(planned_alternate_prefix_sheets[0].relationship_id == "rId3",
        "planned workbook sheet catalog should preserve alternate-prefix relationship ids");
    check(planned_alternate_prefix_sheets[0].part_name
            == fastxlsx::detail::PartName("/xl/worksheets/sheet3.xml"),
        "planned workbook sheet catalog should resolve alternate-prefix worksheet targets");
    check(reader.worksheet_part_by_sheet_name_from_xml("Planned Alt Prefix",
              planned_alternate_prefix_workbook)
            == fastxlsx::detail::PartName("/xl/worksheets/sheet3.xml"),
        "planned sheet-name lookup should support alternate relationship prefixes");

    const auto expect_planned_workbook_failure =
        [&](std::string_view workbook_xml, std::string_view expected_diagnostic,
            const char* message) {
            bool planned_failed = false;
            try {
                (void)reader.workbook_sheets_from_xml(workbook_xml);
            } catch (const std::exception& error) {
                planned_failed = true;
                check_contains(error.what(), expected_diagnostic, message);
            }
            check(planned_failed, message);
        };
    const auto expect_planned_sheet_lookup_failure =
        [&](std::string_view sheet_name, std::string_view workbook_xml,
            std::string_view expected_diagnostic, const char* message) {
            bool planned_failed = false;
            try {
                (void)reader.worksheet_part_by_sheet_name_from_xml(sheet_name, workbook_xml);
            } catch (const std::exception& error) {
                planned_failed = true;
                check_contains(error.what(), expected_diagnostic, message);
            }
            check(planned_failed, message);
        };
    expect_planned_sheet_lookup_failure("Planned Ignored Outer", planned_scoped_workbook,
        "workbook sheet name is not present",
        "planned sheet-name lookup should ignore sheet tags outside the sheets catalog");
    expect_planned_sheet_lookup_failure(
        "Planned Ignored Decoy Catalog", planned_scoped_workbook,
        "workbook sheet name is not present",
        "planned sheet-name lookup should ignore non-root workbook sheets catalogs");
    expect_planned_sheet_lookup_failure("Planned Ignored Nested", planned_scoped_workbook,
        "workbook sheet name is not present",
        "planned sheet-name lookup should ignore non-direct sheet tags inside sheets");
    const std::string planned_wrong_namespace_workbook =
        R"(<workbook xmlns:x="urn:fastxlsx:not-relationships">)"
        R"(<sheets><sheet name="Planned Wrong Namespace" sheetId="1" x:id="rId1"/></sheets>)"
        R"(</workbook>)";
    expect_planned_workbook_failure(planned_wrong_namespace_workbook,
        "workbook sheet is missing relationship id",
        "planned workbook sheet catalog should reject wrong-namespace id attributes");
    const std::string planned_unqualified_id_workbook =
        R"(<workbook><sheets>)"
        R"(<sheet name="Planned Plain Id" sheetId="1" id="rId1"/>)"
        R"(</sheets></workbook>)";
    expect_planned_workbook_failure(planned_unqualified_id_workbook,
        "workbook sheet is missing relationship id",
        "planned workbook sheet catalog should reject unqualified id attributes");
    const std::string planned_missing_relationship_id_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Planned Missing Rel" sheetId="1" r:id="missingRel"/></sheets>)"
        R"(</workbook>)";
    expect_planned_workbook_failure(planned_missing_relationship_id_workbook,
        "workbook sheet relationship id is not present in workbook .rels",
        "planned workbook sheet catalog should reject sheet ids absent from workbook relationships");
    expect_planned_sheet_lookup_failure(
        "Planned Missing Rel", planned_missing_relationship_id_workbook,
        "workbook sheet relationship id is not present in workbook .rels",
        "planned sheet-name lookup should reject sheet ids absent from workbook relationships");
    bool ignored_outer_failed = false;
    try {
        (void)reader.worksheet_part_by_sheet_name("Ignored Outer");
    } catch (const std::exception& error) {
        ignored_outer_failed = true;
        check_contains(error.what(), "workbook sheet name is not present",
            "sheet-name lookup should preserve missing-sheet diagnostic for ignored outer sheets");
    }
    check(ignored_outer_failed,
        "sheet-name lookup should ignore sheet tags outside the sheets catalog");
    bool ignored_decoy_catalog_failed = false;
    try {
        (void)reader.worksheet_part_by_sheet_name("Ignored Decoy Catalog");
    } catch (const std::exception& error) {
        ignored_decoy_catalog_failed = true;
        check_contains(error.what(), "workbook sheet name is not present",
            "sheet-name lookup should preserve missing-sheet diagnostic for decoy catalogs");
    }
    check(ignored_decoy_catalog_failed,
        "sheet-name lookup should ignore non-root workbook sheets catalogs");
    bool ignored_nested_failed = false;
    try {
        (void)reader.worksheet_part_by_sheet_name("Ignored Nested");
    } catch (const std::exception& error) {
        ignored_nested_failed = true;
        check_contains(error.what(), "workbook sheet name is not present",
            "sheet-name lookup should preserve missing-sheet diagnostic for nested decoy sheets");
    }
    check(ignored_nested_failed,
        "sheet-name lookup should ignore non-direct sheet tags inside sheets");

    bool failed = false;
    try {
        (void)reader.worksheet_part_by_sheet_name("Missing");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "workbook sheet name is not present",
            "sheet-name lookup should preserve missing-sheet diagnostic");
    }
    check(failed, "sheet-name lookup should reject missing sheet names");

    const std::filesystem::path duplicate_name_path =
        output_path("fastxlsx-package-reader-workbook-sheets-duplicate-name.xlsx");
    const std::string duplicate_content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet2.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(</Types>)";
    const std::string duplicate_workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet2.xml"/>)"
        R"(</Relationships>)";
    const std::string duplicate_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets>)"
        R"(<sheet name="Duplicated" sheetId="1" r:id="rId1"/>)"
        R"(<sheet name="Duplicated" sheetId="2" r:id="rId2"/>)"
        R"(</sheets>)"
        R"(</workbook>)";
    fastxlsx::detail::write_package(duplicate_name_path,
        {
            {"[Content_Types].xml", duplicate_content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", duplicate_workbook},
            {"xl/_rels/workbook.xml.rels", duplicate_workbook_relationships},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
            {"xl/worksheets/sheet2.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PackageReader duplicate_reader =
        fastxlsx::detail::PackageReader::open(duplicate_name_path);
    check(duplicate_reader.workbook_sheets().size() == 2,
        "workbook sheet catalog should expose duplicate sheet names");
    bool ambiguous_failed = false;
    try {
        (void)duplicate_reader.worksheet_part_by_sheet_name("Duplicated");
    } catch (const std::exception& error) {
        ambiguous_failed = true;
        check_contains(error.what(), "workbook sheet name is ambiguous",
            "sheet-name lookup should preserve ambiguous duplicate sheet diagnostic");
    }
    check(ambiguous_failed,
        "sheet-name lookup should reject ambiguous duplicate sheet names");

    bool ambiguous_cell_store_failed = false;
    try {
        (void)fastxlsx::detail::load_cell_store_from_workbook_sheet(
            duplicate_reader, "Duplicated");
    } catch (const fastxlsx::FastXlsxError& error) {
        ambiguous_cell_store_failed = true;
        check_contains(error.what(), "failed to resolve workbook sheet 'Duplicated'",
            "CellStore loader should report the requested duplicate sheet name");
        check_contains(error.what(), "workbook sheet name is ambiguous",
            "CellStore loader should preserve the ambiguous sheet-name diagnostic");
    }
    check(ambiguous_cell_store_failed,
        "CellStore loader should reject ambiguous duplicate sheet names");
}

void test_package_reader_loads_cell_store_from_workbook_sheet()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-cell-store.xlsx");

    auto workbook = fastxlsx::Workbook::create();
    auto& source_sheet = workbook.add_worksheet("Source");
    source_sheet.append_row({
        fastxlsx::Cell::number(12.5),
        fastxlsx::Cell::text(" text & <tag> "),
        fastxlsx::Cell::boolean(true),
    });
    source_sheet.append_row({
        fastxlsx::Cell::formula("SUM(A1:C1)&\"<ok>\""),
    });
    workbook.add_worksheet("Untouched");
    workbook.save(path);

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);
    const fastxlsx::detail::CellStore store =
        fastxlsx::detail::load_cell_store_from_workbook_sheet(reader, "Source");

    check(store.cell_count() == 4,
        "workbook sheet loader should materialize the selected worksheet cells");

    const fastxlsx::detail::CellRecord* number = store.find_cell(1, 1);
    check(number != nullptr, "workbook sheet loader should load the numeric cell");
    check(number->kind == fastxlsx::CellValueKind::Number,
        "workbook sheet loader numeric kind mismatch");
    check(number->number_value == 12.5,
        "workbook sheet loader numeric payload mismatch");

    const fastxlsx::detail::CellRecord* text = store.find_cell(1, 2);
    check(text != nullptr, "workbook sheet loader should load the inline string cell");
    check(text->kind == fastxlsx::CellValueKind::Text,
        "workbook sheet loader inline string kind mismatch");
    check(text->text_value == " text & <tag> ",
        "workbook sheet loader inline string payload mismatch");

    const fastxlsx::detail::CellRecord* boolean = store.find_cell(1, 3);
    check(boolean != nullptr, "workbook sheet loader should load the boolean cell");
    check(boolean->kind == fastxlsx::CellValueKind::Boolean,
        "workbook sheet loader boolean kind mismatch");
    check(boolean->boolean_value,
        "workbook sheet loader boolean payload mismatch");

    const fastxlsx::detail::CellRecord* formula = store.find_cell(2, 1);
    check(formula != nullptr, "workbook sheet loader should load the formula cell");
    check(formula->kind == fastxlsx::CellValueKind::Formula,
        "workbook sheet loader formula kind mismatch");
    check(formula->text_value == "SUM(A1:C1)&\"<ok>\"",
        "workbook sheet loader formula text mismatch");

    fastxlsx::detail::CellStoreOptions max_cell_options;
    max_cell_options.max_cells = 1;
    bool max_cells_failed = false;
    try {
        (void)fastxlsx::detail::load_cell_store_from_workbook_sheet(
            reader, "Source", max_cell_options);
    } catch (const fastxlsx::FastXlsxError& error) {
        max_cells_failed = true;
        check_contains(error.what(), "CellStore max_cells guardrail exceeded",
            "workbook sheet loader should propagate CellStore max_cells guardrails");
    }
    check(max_cells_failed, "workbook sheet loader should reject max_cells overflow");

    const fastxlsx::detail::CellStore reloaded_store =
        fastxlsx::detail::load_cell_store_from_workbook_sheet(reader, "Source");
    check(reloaded_store.cell_count() == 4,
        "workbook sheet loader guardrail failure should not poison the source reader");

    bool missing_sheet_failed = false;
    try {
        (void)fastxlsx::detail::load_cell_store_from_workbook_sheet(reader, "Missing");
    } catch (const fastxlsx::FastXlsxError& error) {
        missing_sheet_failed = true;
        check_contains(error.what(), "failed to resolve workbook sheet 'Missing'",
            "workbook sheet loader should report the requested missing sheet name");
        check_contains(error.what(), "workbook sheet name is not present",
            "workbook sheet loader should report missing sheet names");
    }
    check(missing_sheet_failed, "workbook sheet loader should reject missing sheets");
}

void test_package_reader_materializes_registry_session_from_workbook_sheet()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-materialized-session.xlsx");

    auto workbook = fastxlsx::Workbook::create();
    auto& source_sheet = workbook.add_worksheet("Source");
    source_sheet.append_row({
        fastxlsx::Cell::number(42.0),
        fastxlsx::Cell::text("source"),
    });
    workbook.add_worksheet("Untouched");
    workbook.save(path);

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);

    fastxlsx::detail::CellStoreOptions options;
    options.max_cells = 4;
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    auto& session = registry.materialize_from_workbook_sheet(
        reader, "Planned", "Source", options);

    check(session.planned_name() == "Planned",
        "registry materialization should keep the planned sheet name");
    check(session.options_match(options),
        "registry materialization should preserve source-load options");
    check(session.cell_count() == 2,
        "registry materialization should load source worksheet cells");
    const fastxlsx::detail::CellRecord* number = session.try_cell(1, 1);
    check(number != nullptr && number->kind == fastxlsx::CellValueKind::Number
            && number->number_value == 42.0,
        "registry materialization should expose loaded numeric cells");
    const fastxlsx::detail::CellRecord* text = session.try_cell(1, 2);
    check(text != nullptr && text->kind == fastxlsx::CellValueKind::Text
            && text->text_value == "source",
        "registry materialization should expose loaded text cells");
    check(!session.dirty(),
        "registry source materialization should return a clean session");

    session.set_cell(2, 1, fastxlsx::CellValue::boolean(true));
    auto& repeated_session = registry.materialize_from_workbook_sheet(
        reader, "Planned", "Missing", options);
    check(&repeated_session == &session,
        "matching repeated registry materialization should reuse the existing session");
    check(session.dirty(),
        "matching repeated registry materialization should preserve dirty state");
    check(session.try_cell(2, 1) != nullptr,
        "matching repeated registry materialization should not replace dirty cells");

    fastxlsx::detail::CellStoreOptions mismatched_options;
    mismatched_options.max_cells = 5;
    bool mismatch_failed = false;
    try {
        (void)registry.materialize_from_workbook_sheet(
            reader, "Planned", "Missing", mismatched_options);
    } catch (const fastxlsx::FastXlsxError& error) {
        mismatch_failed = true;
        check_contains(error.what(), "options mismatch",
            "registry materialization should fail on options mismatch before package lookup");
    }
    check(mismatch_failed,
        "registry materialization should reject mismatched repeated options");
    check(registry.session_count() == 1,
        "mismatched repeated registry materialization should not insert sessions");
    check(registry.try_session("Planned") == &session,
        "mismatched repeated registry materialization should preserve existing session");
    check(session.dirty(),
        "mismatched repeated registry materialization should preserve dirty state");

    bool missing_failed = false;
    try {
        (void)registry.materialize_from_workbook_sheet(
            reader, "MissingPlanned", "Missing", options);
    } catch (const fastxlsx::FastXlsxError& error) {
        missing_failed = true;
        check_contains(error.what(), "failed to resolve workbook sheet 'Missing'",
            "registry materialization should propagate missing source sheet diagnostics");
    }
    check(missing_failed,
        "registry materialization should reject missing source sheets");
    check(registry.try_session("MissingPlanned") == nullptr,
        "failed source registry materialization should not leave a session");

    fastxlsx::detail::CellStoreOptions too_small_options;
    too_small_options.max_cells = 1;
    bool max_cells_failed = false;
    try {
        (void)registry.materialize_from_workbook_sheet(
            reader, "TooSmall", "Source", too_small_options);
    } catch (const fastxlsx::FastXlsxError& error) {
        max_cells_failed = true;
        check_contains(error.what(), "CellStore max_cells guardrail exceeded",
            "registry materialization should propagate source load guardrails");
    }
    check(max_cells_failed,
        "registry materialization should reject source load guardrail failures");
    check(registry.try_session("TooSmall") == nullptr,
        "failed guarded registry materialization should not leave a session");
}

void test_package_reader_cell_store_loader_rejects_styles_and_loads_shared_strings()
{
    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/sharedStrings.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml"/>)"
        R"(<Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/>)"
        R"(</Types>)";
    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>)"
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Source" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    const std::string shared_strings =
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><si><t>from sst</t></si></sst>)";
    const std::string styles =
        R"(<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<cellXfs count="2"><xf/><xf/></cellXfs>)"
        R"(</styleSheet>)";

    const std::filesystem::path styled_path =
        output_path("fastxlsx-package-reader-cell-store-styled-source.xlsx");
    fastxlsx::detail::write_package(styled_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", workbook},
            {"xl/_rels/workbook.xml.rels", workbook_relationships},
            {"xl/worksheets/sheet1.xml",
                R"(<worksheet><sheetData><row r="1"><c r="A1" s="1"><v>1</v></c></row></sheetData></worksheet>)"},
            {"xl/sharedStrings.xml", shared_strings},
            {"xl/styles.xml", styles},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PackageReader styled_reader =
        fastxlsx::detail::PackageReader::open(styled_path);
    fastxlsx::detail::CellStore styled_store =
        fastxlsx::detail::load_cell_store_from_workbook_sheet(styled_reader, "Source");
    const fastxlsx::detail::CellRecord* styled_record = styled_store.try_cell(1, 1);
    check(styled_record != nullptr && styled_record->style_id.has_value()
            && styled_record->style_id->value() == 1,
        "workbook sheet CellStore loader should materialize source style ids");
    check_contains(styled_reader.read_entry("xl/worksheets/sheet1.xml"), R"(s="1")",
        "styled source loader should not poison the PackageReader");

    const std::filesystem::path explicit_default_style_path =
        output_path("fastxlsx-package-reader-cell-store-explicit-default-style-source.xlsx");
    fastxlsx::detail::write_package(explicit_default_style_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", workbook},
            {"xl/_rels/workbook.xml.rels", workbook_relationships},
            {"xl/worksheets/sheet1.xml",
                R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</v></c><c r="B1" s="0"><v>2</v></c></row></sheetData></worksheet>)"},
            {"xl/sharedStrings.xml", shared_strings},
            {"xl/styles.xml", styles},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PackageReader explicit_default_style_reader =
        fastxlsx::detail::PackageReader::open(explicit_default_style_path);
    const fastxlsx::detail::CellStore explicit_default_style_store =
        fastxlsx::detail::load_cell_store_from_workbook_sheet(
            explicit_default_style_reader, "Source");
    check(explicit_default_style_store.cell_count() == 2,
        "workbook sheet CellStore loader should accept explicit default source styles");
    const fastxlsx::detail::CellRecord* explicit_default_style_record =
        explicit_default_style_store.try_cell(1, 2);
    const fastxlsx::CellValue explicit_default_style_cell =
        explicit_default_style_record != nullptr ? explicit_default_style_record->to_value()
                                                 : fastxlsx::CellValue::blank();
    check(explicit_default_style_record != nullptr
            && explicit_default_style_cell.kind() == fastxlsx::CellValueKind::Number
            && explicit_default_style_cell.number_value() == 2.0
            && !explicit_default_style_cell.has_style(),
        "workbook sheet CellStore loader should normalize source s=0 to no style handle");
    check_contains(
        explicit_default_style_reader.read_entry("xl/worksheets/sheet1.xml"), R"(s="0")",
        "explicit-default-style source loader should not poison the PackageReader");

    const std::filesystem::path shared_string_path =
        output_path("fastxlsx-package-reader-cell-store-shared-string-source.xlsx");
    fastxlsx::detail::write_package(shared_string_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", workbook},
            {"xl/_rels/workbook.xml.rels", workbook_relationships},
            {"xl/worksheets/sheet1.xml",
                R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</v></c><c r="B1" t="s"><v>0</v></c></row></sheetData></worksheet>)"},
            {"xl/sharedStrings.xml", shared_strings},
            {"xl/styles.xml", styles},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PackageReader shared_string_reader =
        fastxlsx::detail::PackageReader::open(shared_string_path);
    const fastxlsx::detail::CellStore shared_string_store =
        fastxlsx::detail::load_cell_store_from_workbook_sheet(shared_string_reader, "Source");
    check(shared_string_store.cell_count() == 2,
        "workbook sheet CellStore loader should materialize shared-string source cells");
    const fastxlsx::detail::CellRecord* shared_string_record =
        shared_string_store.try_cell(1, 2);
    const fastxlsx::CellValue shared_string_cell =
        shared_string_record != nullptr ? shared_string_record->to_value()
                                        : fastxlsx::CellValue::blank();
    check(shared_string_record != nullptr
            && shared_string_cell.kind() == fastxlsx::CellValueKind::Text
            && shared_string_cell.text_value() == "from sst",
        "workbook sheet CellStore loader should resolve source shared string text");
    check_contains(shared_string_reader.read_entry("xl/sharedStrings.xml"), "from sst",
        "shared-string source loader should not poison the PackageReader");
}

void test_package_reader_cell_store_loader_rejects_invalid_shared_strings_sources()
{
    constexpr std::string_view shared_strings_content_type =
        "application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml";
    constexpr std::string_view shared_strings_relationship_type =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings";

    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Source" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    const std::string valid_shared_strings =
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><si><t>ok</t></si></sst>)";
    const std::string valid_shared_string_worksheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1" t="s"><v>0</v></c></row></sheetData></worksheet>)";

    const auto make_content_types = [](std::string_view shared_string_type) {
        std::string xml =
            R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
            R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
            R"(<Default Extension="xml" ContentType="application/xml"/>)"
            R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
            R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)";
        if (!shared_string_type.empty()) {
            xml += R"(<Override PartName="/xl/sharedStrings.xml" ContentType=")";
            xml += shared_string_type;
            xml += R"("/>)";
        }
        xml += R"(</Types>)";
        return xml;
    };
    const auto make_workbook_relationships = [&](std::string_view shared_strings_relationships) {
        std::string xml =
            R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
            R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)";
        xml += shared_strings_relationships;
        xml += R"(</Relationships>)";
        return xml;
    };
    const auto make_shared_strings_relationship = [&](std::string_view id,
                                                      std::string_view target,
                                                      bool external = false) {
        std::string xml = R"(<Relationship Id=")";
        xml += id;
        xml += R"(" Type=")";
        xml += shared_strings_relationship_type;
        xml += R"(" Target=")";
        xml += target;
        xml += R"(")";
        if (external) {
            xml += R"( TargetMode="External")";
        }
        xml += R"(/>)";
        return xml;
    };

    const auto write_source_package =
        [&](const std::filesystem::path& path,
            std::string_view content_types,
            std::string_view workbook_relationships,
            std::string_view worksheet_xml,
            std::optional<std::string_view> shared_strings_xml) {
            std::vector<fastxlsx::detail::PackageEntry> entries {
                {"[Content_Types].xml", std::string(content_types)},
                {"_rels/.rels", package_relationships},
                {"xl/workbook.xml", workbook},
                {"xl/_rels/workbook.xml.rels", std::string(workbook_relationships)},
                {"xl/worksheets/sheet1.xml", std::string(worksheet_xml)},
            };
            if (shared_strings_xml.has_value()) {
                entries.emplace_back("xl/sharedStrings.xml", std::string(*shared_strings_xml));
            }
            fastxlsx::detail::write_package(path,
                entries,
                {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
        };

    const auto expect_load_failure =
        [&](std::string_view name,
            std::string_view content_types,
            std::string_view workbook_relationships,
            std::string_view worksheet_xml,
            std::optional<std::string_view> shared_strings_xml,
            std::string_view expected_diagnostic,
            const char* context) {
            const std::filesystem::path path = output_path(name);
            write_source_package(
                path, content_types, workbook_relationships, worksheet_xml, shared_strings_xml);

            const fastxlsx::detail::PackageReader reader =
                fastxlsx::detail::PackageReader::open(path);
            std::optional<fastxlsx::detail::CellStore> loaded_store;
            bool failed = false;
            try {
                loaded_store =
                    fastxlsx::detail::load_cell_store_from_workbook_sheet(reader, "Source");
            } catch (const fastxlsx::FastXlsxError& error) {
                failed = true;
                check_contains(error.what(), expected_diagnostic, context);
            }
            check(failed, context);
            check(!loaded_store.has_value(),
                "invalid sharedStrings source load should not expose a partial CellStore");
            check_contains(reader.read_entry("xl/worksheets/sheet1.xml"), R"(t="s")",
                "invalid sharedStrings source load should not poison the PackageReader");
        };

    const std::string valid_content_types = make_content_types(shared_strings_content_type);
    const std::string valid_shared_strings_relationship =
        make_shared_strings_relationship("rId2", "sharedStrings.xml");
    const std::string valid_workbook_relationships =
        make_workbook_relationships(valid_shared_strings_relationship);

    expect_load_failure(
        "fastxlsx-package-reader-cell-store-shared-strings-duplicate-rel.xlsx",
        valid_content_types,
        make_workbook_relationships(valid_shared_strings_relationship
            + make_shared_strings_relationship("rId3", "sharedStrings.xml")),
        valid_shared_string_worksheet,
        valid_shared_strings,
        "workbook sharedStrings lookup found multiple sharedStrings relationships",
        "workbook sheet CellStore loader should reject duplicate sharedStrings relationships");

    expect_load_failure(
        "fastxlsx-package-reader-cell-store-shared-strings-external-target.xlsx",
        valid_content_types,
        make_workbook_relationships(
            make_shared_strings_relationship("rId2", "https://example.invalid/sst.xml", true)),
        valid_shared_string_worksheet,
        valid_shared_strings,
        "sharedStrings relationship target cannot be external",
        "workbook sheet CellStore loader should reject external sharedStrings targets");

    expect_load_failure(
        "fastxlsx-package-reader-cell-store-shared-strings-query-target.xlsx",
        valid_content_types,
        make_workbook_relationships(
            make_shared_strings_relationship("rId2", "sharedStrings.xml?x=1")),
        valid_shared_string_worksheet,
        valid_shared_strings,
        "sharedStrings relationship target must be a package part",
        "workbook sheet CellStore loader should reject query-qualified sharedStrings targets");

    expect_load_failure(
        "fastxlsx-package-reader-cell-store-shared-strings-fragment-target.xlsx",
        valid_content_types,
        make_workbook_relationships(
            make_shared_strings_relationship("rId2", "sharedStrings.xml#frag")),
        valid_shared_string_worksheet,
        valid_shared_strings,
        "sharedStrings relationship target must be a package part",
        "workbook sheet CellStore loader should reject fragment-qualified sharedStrings targets");

    expect_load_failure(
        "fastxlsx-package-reader-cell-store-shared-strings-missing-part.xlsx",
        make_content_types({}),
        valid_workbook_relationships,
        valid_shared_string_worksheet,
        std::nullopt,
        "workbook sharedStrings relationship targets an unknown package part",
        "workbook sheet CellStore loader should reject missing sharedStrings parts");

    expect_load_failure(
        "fastxlsx-package-reader-cell-store-shared-strings-wrong-content-type.xlsx",
        make_content_types(
            "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"),
        valid_workbook_relationships,
        valid_shared_string_worksheet,
        valid_shared_strings,
        "workbook sharedStrings relationship target is not a sharedStrings part",
        "workbook sheet CellStore loader should reject non-sharedStrings content types");

    expect_load_failure(
        "fastxlsx-package-reader-cell-store-shared-strings-malformed-xml.xlsx",
        valid_content_types,
        valid_workbook_relationships,
        valid_shared_string_worksheet,
        std::string_view {R"(<notSst/>)"},
        "CellStore sharedStrings loader root is missing an sst element",
        "workbook sheet CellStore loader should reject malformed sharedStrings XML");

    expect_load_failure(
        "fastxlsx-package-reader-cell-store-shared-strings-missing-index.xlsx",
        valid_content_types,
        valid_workbook_relationships,
        R"(<worksheet><sheetData><row r="1"><c r="A1" t="s"></c></row></sheetData></worksheet>)",
        valid_shared_strings,
        "CellStore worksheet loader found an invalid shared string index",
        "workbook sheet CellStore loader should reject missing shared string indexes");

    expect_load_failure(
        "fastxlsx-package-reader-cell-store-shared-strings-empty-index.xlsx",
        valid_content_types,
        valid_workbook_relationships,
        R"(<worksheet><sheetData><row r="1"><c r="A1" t="s"><v></v></c></row></sheetData></worksheet>)",
        valid_shared_strings,
        "CellStore worksheet loader found an invalid shared string index",
        "workbook sheet CellStore loader should reject empty shared string indexes");

    expect_load_failure(
        "fastxlsx-package-reader-cell-store-shared-strings-invalid-index.xlsx",
        valid_content_types,
        valid_workbook_relationships,
        R"(<worksheet><sheetData><row r="1"><c r="A1" t="s"><v>abc</v></c></row></sheetData></worksheet>)",
        valid_shared_strings,
        "CellStore worksheet loader found an invalid shared string index",
        "workbook sheet CellStore loader should reject non-numeric shared string indexes");

    expect_load_failure(
        "fastxlsx-package-reader-cell-store-shared-strings-out-of-range-index.xlsx",
        valid_content_types,
        valid_workbook_relationships,
        R"(<worksheet><sheetData><row r="1"><c r="A1" t="s"><v>1</v></c></row></sheetData></worksheet>)",
        valid_shared_strings,
        "CellStore worksheet loader found a shared string index out of range",
        "workbook sheet CellStore loader should reject out-of-range shared string indexes");
}

void test_package_reader_cell_store_loader_rejects_unsupported_source_cell_shapes()
{
    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
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
        R"(<sheets><sheet name="Source" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";

    const auto write_source_package = [&](const std::filesystem::path& path,
                                          std::string_view worksheet_xml) {
        fastxlsx::detail::write_package(path,
            {
                {"[Content_Types].xml", content_types},
                {"_rels/.rels", package_relationships},
                {"xl/workbook.xml", workbook},
                {"xl/_rels/workbook.xml.rels", workbook_relationships},
                {"xl/worksheets/sheet1.xml", std::string(worksheet_xml)},
            },
            {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    };

    const std::filesystem::path unsupported_type_path =
        output_path("fastxlsx-package-reader-cell-store-unsupported-type.xlsx");
    write_source_package(unsupported_type_path,
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</v></c><c r="B1" t="z"><v>cached</v></c></row></sheetData></worksheet>)");

    const fastxlsx::detail::PackageReader unsupported_type_reader =
        fastxlsx::detail::PackageReader::open(unsupported_type_path);
    std::optional<fastxlsx::detail::CellStore> unsupported_type_store;
    bool unsupported_type_failed = false;
    try {
        unsupported_type_store = fastxlsx::detail::load_cell_store_from_workbook_sheet(
            unsupported_type_reader, "Source");
    } catch (const fastxlsx::FastXlsxError& error) {
        unsupported_type_failed = true;
        check_contains(error.what(), "unsupported cell type: z",
            "workbook sheet CellStore loader should reject unsupported source cell types");
    }
    check(unsupported_type_failed,
        "workbook sheet CellStore loader should reject unsupported source cell types");
    check(!unsupported_type_store.has_value(),
        "unsupported-type loader failure should not expose a partial CellStore");
    check_contains(unsupported_type_reader.read_entry("xl/worksheets/sheet1.xml"), R"(t="z")",
        "unsupported-type source loader failure should not poison the PackageReader");

    const std::filesystem::path invalid_boolean_path =
        output_path("fastxlsx-package-reader-cell-store-invalid-boolean.xlsx");
    write_source_package(invalid_boolean_path,
        R"(<worksheet><sheetData><row r="1"><c r="A1" t="b"><v>1</v></c><c r="B1" t="b"><v>2</v></c></row></sheetData></worksheet>)");

    const fastxlsx::detail::PackageReader invalid_boolean_reader =
        fastxlsx::detail::PackageReader::open(invalid_boolean_path);
    std::optional<fastxlsx::detail::CellStore> invalid_boolean_store;
    bool invalid_boolean_failed = false;
    try {
        invalid_boolean_store = fastxlsx::detail::load_cell_store_from_workbook_sheet(
            invalid_boolean_reader, "Source");
    } catch (const fastxlsx::FastXlsxError& error) {
        invalid_boolean_failed = true;
        check_contains(error.what(), "invalid boolean cell value",
            "workbook sheet CellStore loader should reject invalid boolean source values");
    }
    check(invalid_boolean_failed,
        "workbook sheet CellStore loader should reject invalid boolean source values");
    check(!invalid_boolean_store.has_value(),
        "invalid-boolean loader failure should not expose a partial CellStore");
    check_contains(invalid_boolean_reader.read_entry("xl/worksheets/sheet1.xml"), R"(t="b")",
        "invalid-boolean source loader failure should not poison the PackageReader");
}

void test_package_reader_rejects_invalid_workbook_sheet_catalog()
{
    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/>)"
        R"(</Types>)";
    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";

    const std::filesystem::path corrupt_workbook_source_path =
        output_path("fastxlsx-package-reader-workbook-sheets-corrupt-source.xlsx");
    fastxlsx::detail::write_package(corrupt_workbook_source_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml",
                R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets></workbook>)"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>)"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    std::string corrupt_workbook_data =
        fastxlsx::test::read_file(corrupt_workbook_source_path);
    corrupt_first_occurrence(corrupt_workbook_data, "Sheet1");
    const std::filesystem::path corrupt_workbook_path =
        output_path("fastxlsx-package-reader-workbook-sheets-corrupt.xlsx");
    write_file(corrupt_workbook_path, corrupt_workbook_data);
    expect_workbook_sheets_failure_contains(corrupt_workbook_path,
        "failed to read materialized workbook sheet catalog XML",
        "workbook sheet catalog should wrap materialized workbook read failures");

    const std::filesystem::path missing_office_document_path =
        output_path("fastxlsx-package-reader-workbook-sheets-missing-office-document.xlsx");
    fastxlsx::detail::write_package(missing_office_document_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels",
                R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
                R"(<Relationship Id="rIdCustom" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXml" Target="custom/item1.xml"/>)"
                R"(</Relationships>)"},
            {"xl/workbook.xml",
                R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets></workbook>)"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>)"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure_contains(missing_office_document_path,
        "workbook sheet catalog requires package officeDocument relationship",
        "workbook sheet catalog should require a package officeDocument relationship");

    const std::filesystem::path duplicate_office_document_path =
        output_path("fastxlsx-package-reader-workbook-sheets-duplicate-office-document.xlsx");
    fastxlsx::detail::write_package(duplicate_office_document_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels",
                R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
                R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
                R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
                R"(</Relationships>)"},
            {"xl/workbook.xml",
                R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets></workbook>)"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>)"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure_contains(duplicate_office_document_path,
        "workbook sheet catalog has multiple officeDocument relationships",
        "workbook sheet catalog should reject multiple officeDocument relationships");

    const std::filesystem::path external_office_document_path =
        output_path("fastxlsx-package-reader-workbook-sheets-external-office-document.xlsx");
    fastxlsx::detail::write_package(external_office_document_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels",
                R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
                R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="https://example.invalid/workbook.xml" TargetMode="External"/>)"
                R"(</Relationships>)"},
            {"xl/workbook.xml",
                R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets></workbook>)"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>)"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure_contains(external_office_document_path,
        "workbook sheet catalog officeDocument target cannot be external",
        "workbook sheet catalog should reject external officeDocument targets");

    const std::filesystem::path query_office_document_path =
        output_path("fastxlsx-package-reader-workbook-sheets-query-office-document.xlsx");
    fastxlsx::detail::write_package(query_office_document_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels",
                R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
                R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml?version=1"/>)"
                R"(</Relationships>)"},
            {"xl/workbook.xml",
                R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets></workbook>)"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>)"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure_contains(query_office_document_path,
        "workbook sheet catalog officeDocument target must be a package part",
        "workbook sheet catalog should reject URI-qualified officeDocument targets");

    struct OfficeDocumentPercentFailureCase {
        const char* name;
        const char* target;
        const char* expected_diagnostic;
    };
    const OfficeDocumentPercentFailureCase office_document_percent_failure_cases[] = {
        {
            "fastxlsx-package-reader-workbook-sheets-incomplete-percent-office-document.xlsx",
            "xl/workbook.xml%",
            "relationship target percent escape is incomplete",
        },
        {
            "fastxlsx-package-reader-workbook-sheets-invalid-percent-office-document.xlsx",
            "xl/workbook%GG.xml",
            "relationship target percent escape is invalid",
        },
        {
            "fastxlsx-package-reader-workbook-sheets-null-percent-office-document.xlsx",
            "xl/workbook%00.xml",
            "relationship target cannot contain null bytes",
        },
    };
    for (const OfficeDocumentPercentFailureCase& test_case
        : office_document_percent_failure_cases) {
        const std::filesystem::path percent_office_document_path = output_path(test_case.name);
        fastxlsx::detail::write_package(percent_office_document_path,
            {
                {"[Content_Types].xml", content_types},
                {"_rels/.rels",
                    std::string(
                        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target=")")
                    + test_case.target + R"("/></Relationships>)"},
                {"xl/workbook.xml",
                    R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets></workbook>)"},
                {"xl/_rels/workbook.xml.rels",
                    R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>)"},
                {"xl/worksheets/sheet1.xml", "<worksheet/>"},
            },
            {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
        expect_workbook_sheets_failure_contains(percent_office_document_path,
            test_case.expected_diagnostic,
            "workbook sheet catalog should reject malformed percent-encoded officeDocument targets precisely");
    }

    const std::string alternate_content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Override PartName="/xl/altWorkbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(</Types>)";
    const std::filesystem::path alternate_office_document_path =
        output_path("fastxlsx-package-reader-workbook-sheets-alternate-office-document.xlsx");
    fastxlsx::detail::write_package(alternate_office_document_path,
        {
            {"[Content_Types].xml", alternate_content_types},
            {"_rels/.rels",
                R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
                R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/altWorkbook.xml"/>)"
                R"(</Relationships>)"},
            {"xl/altWorkbook.xml",
                R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets></workbook>)"},
            {"xl/_rels/altWorkbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>)"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    {
        const fastxlsx::detail::PackageReader alternate_reader =
            fastxlsx::detail::PackageReader::open(alternate_office_document_path);
        check(alternate_reader.workbook_part()
                == fastxlsx::detail::PartName("/xl/altWorkbook.xml"),
            "workbook sheet catalog should expose the actual officeDocument target");
        const std::vector<fastxlsx::detail::WorkbookSheetReference> sheets =
            alternate_reader.workbook_sheets();
        check(sheets.size() == 1 && sheets[0].name == "Sheet1"
                && sheets[0].part_name
                    == fastxlsx::detail::PartName("/xl/worksheets/sheet1.xml"),
            "workbook sheet catalog should accept non-fixed officeDocument targets");
    }

    const std::string root_content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Override PartName="/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(</Types>)";
    const std::filesystem::path root_office_document_path =
        output_path("fastxlsx-package-reader-workbook-sheets-root-office-document.xlsx");
    fastxlsx::detail::write_package(root_office_document_path,
        {
            {"[Content_Types].xml", root_content_types},
            {"_rels/.rels",
                R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
                R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="workbook.xml"/>)"
                R"(</Relationships>)"},
            {"workbook.xml",
                R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="1" sheetId="1" r:id="rId1"/></sheets></workbook>)"},
            {"_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="sheet1.xml"/></Relationships>)"},
            {"sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    {
        const fastxlsx::detail::PackageReader root_reader =
            fastxlsx::detail::PackageReader::open(root_office_document_path);
        check(root_reader.workbook_part() == fastxlsx::detail::PartName("/workbook.xml"),
            "workbook sheet catalog should expose root-level officeDocument targets");
        const std::vector<fastxlsx::detail::WorkbookSheetReference> sheets =
            root_reader.workbook_sheets();
        check(sheets.size() == 1 && sheets[0].name == "1"
                && sheets[0].part_name == fastxlsx::detail::PartName("/sheet1.xml"),
            "workbook sheet catalog should resolve root-level worksheet targets from the workbook part");
    }

    const std::filesystem::path missing_id_path =
        output_path("fastxlsx-package-reader-workbook-sheets-missing-id.xlsx");
    fastxlsx::detail::write_package(missing_id_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml",
                R"(<workbook><sheets><sheet name="Sheet1" sheetId="1"/></sheets></workbook>)"},
            {"xl/_rels/workbook.xml.rels", "<Relationships/>"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure_contains(missing_id_path,
        "workbook sheet is missing relationship id",
        "workbook sheet catalog should reject sheets without relationship ids");

    const std::filesystem::path missing_relationship_id_path =
        output_path("fastxlsx-package-reader-workbook-sheets-missing-rel-id.xlsx");
    fastxlsx::detail::write_package(missing_relationship_id_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml",
                R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
                R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="missingRel"/></sheets>)"
                R"(</workbook>)"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>)"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure_contains(missing_relationship_id_path,
        "workbook sheet relationship id is not present in workbook .rels",
        "workbook sheet catalog should reject sheet ids missing from workbook relationships");

    const std::filesystem::path unregistered_target_path =
        output_path("fastxlsx-package-reader-workbook-sheets-unregistered-target.xlsx");
    fastxlsx::detail::write_package(unregistered_target_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml",
                R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
                R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
                R"(</workbook>)"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/missing.xml"/></Relationships>)"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure_contains(unregistered_target_path,
        "workbook sheet relationship targets an unknown part",
        "workbook sheet catalog should reject worksheet relationships to unregistered parts");
    const fastxlsx::detail::PackageReader unregistered_target_reader =
        fastxlsx::detail::PackageReader::open(unregistered_target_path);
    const std::string planned_unregistered_target_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Planned Missing Target" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    bool planned_unregistered_target_failed = false;
    try {
        (void)unregistered_target_reader.workbook_sheets_from_xml(
            planned_unregistered_target_workbook);
    } catch (const std::exception& error) {
        planned_unregistered_target_failed = true;
        check_contains(error.what(), "workbook sheet relationship targets an unknown part",
            "planned workbook sheet catalog should preserve unregistered target diagnostic");
    }
    check(planned_unregistered_target_failed,
        "planned workbook sheet catalog should reject worksheet relationships to unregistered parts");
    bool planned_unregistered_lookup_failed = false;
    try {
        (void)unregistered_target_reader.worksheet_part_by_sheet_name_from_xml(
            "Planned Missing Target", planned_unregistered_target_workbook);
    } catch (const std::exception& error) {
        planned_unregistered_lookup_failed = true;
        check_contains(error.what(), "workbook sheet relationship targets an unknown part",
            "planned sheet-name lookup should preserve unregistered target diagnostic");
    }
    check(planned_unregistered_lookup_failed,
        "planned sheet-name lookup should reject worksheet relationships to unregistered parts");

    const std::filesystem::path namespaced_name_path =
        output_path("fastxlsx-package-reader-workbook-sheets-namespaced-name.xlsx");
    fastxlsx::detail::write_package(namespaced_name_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml",
                R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:x="urn:fastxlsx:not-workbook"><sheets><sheet x:name="Sheet1" sheetId="1" r:id="rId1"/></sheets></workbook>)"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>)"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure_contains(namespaced_name_path,
        "workbook sheet is missing name",
        "workbook sheet catalog should reject namespaced sheet name attributes");

    const std::filesystem::path namespaced_sheet_id_path =
        output_path("fastxlsx-package-reader-workbook-sheets-namespaced-sheet-id.xlsx");
    fastxlsx::detail::write_package(namespaced_sheet_id_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml",
                R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:x="urn:fastxlsx:not-workbook"><sheets><sheet name="Sheet1" x:sheetId="1" r:id="rId1"/></sheets></workbook>)"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>)"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure_contains(namespaced_sheet_id_path,
        "workbook sheet is missing sheetId",
        "workbook sheet catalog should reject namespaced sheetId attributes");

    const std::filesystem::path wrong_id_namespace_path =
        output_path("fastxlsx-package-reader-workbook-sheets-wrong-id-namespace.xlsx");
    fastxlsx::detail::write_package(wrong_id_namespace_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml",
                R"(<workbook xmlns:x="urn:fastxlsx:not-relationships"><sheets><sheet name="Sheet1" sheetId="1" x:id="rId1"/></sheets></workbook>)"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>)"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure_contains(wrong_id_namespace_path,
        "workbook sheet is missing relationship id",
        "workbook sheet catalog should reject non-relationship namespace id attributes");

    const std::filesystem::path unqualified_id_path =
        output_path("fastxlsx-package-reader-workbook-sheets-unqualified-id.xlsx");
    fastxlsx::detail::write_package(unqualified_id_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml",
                R"(<workbook><sheets><sheet name="Sheet1" sheetId="1" id="rId1"/></sheets></workbook>)"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>)"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure_contains(unqualified_id_path,
        "workbook sheet is missing relationship id",
        "workbook sheet catalog should reject unqualified id attributes");

    const std::filesystem::path external_path =
        output_path("fastxlsx-package-reader-workbook-sheets-external.xlsx");
    fastxlsx::detail::write_package(external_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml",
                R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets></workbook>)"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="https://example.invalid/sheet.xml" TargetMode="External"/></Relationships>)"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure_contains(external_path,
        "workbook sheet relationship target cannot be external",
        "workbook sheet catalog should reject external worksheet targets with a precise diagnostic");

    const std::filesystem::path uri_qualified_path =
        output_path("fastxlsx-package-reader-workbook-sheets-uri-qualified-target.xlsx");
    fastxlsx::detail::write_package(uri_qualified_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml",
                R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets></workbook>)"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml?version=1"/></Relationships>)"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure_contains(uri_qualified_path,
        "workbook sheet relationship target must be a package part",
        "workbook sheet catalog should reject URI-qualified worksheet targets with a precise diagnostic");

    struct PercentTargetFailureCase {
        const char* name;
        const char* target;
        const char* expected_diagnostic;
    };
    const PercentTargetFailureCase percent_target_failure_cases[] = {
        {
            "fastxlsx-package-reader-workbook-sheets-incomplete-percent-target.xlsx",
            "worksheets/sheet1.xml%",
            "relationship target percent escape is incomplete",
        },
        {
            "fastxlsx-package-reader-workbook-sheets-invalid-percent-target.xlsx",
            "worksheets/sheet%GG.xml",
            "relationship target percent escape is invalid",
        },
        {
            "fastxlsx-package-reader-workbook-sheets-null-percent-target.xlsx",
            "worksheets/sheet%00.xml",
            "relationship target cannot contain null bytes",
        },
    };
    for (const PercentTargetFailureCase& test_case : percent_target_failure_cases) {
        const std::filesystem::path percent_target_path = output_path(test_case.name);
        fastxlsx::detail::write_package(percent_target_path,
            {
                {"[Content_Types].xml", content_types},
                {"_rels/.rels", package_relationships},
                {"xl/workbook.xml",
                    R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets></workbook>)"},
                {"xl/_rels/workbook.xml.rels",
                    std::string(
                        R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target=")")
                    + test_case.target + R"("/></Relationships>)"},
                {"xl/worksheets/sheet1.xml", "<worksheet/>"},
            },
            {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
        expect_workbook_sheets_failure_contains(percent_target_path,
            test_case.expected_diagnostic,
            "workbook sheet catalog should reject malformed percent-encoded worksheet targets precisely");
    }

    const std::filesystem::path non_worksheet_relationship_type_path =
        output_path("fastxlsx-package-reader-workbook-sheets-non-worksheet-rel-type.xlsx");
    fastxlsx::detail::write_package(non_worksheet_relationship_type_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml",
                R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets></workbook>)"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="worksheets/sheet1.xml"/></Relationships>)"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure_contains(non_worksheet_relationship_type_path,
        "workbook sheet relationship is not a worksheet relationship",
        "workbook sheet catalog should reject non-worksheet relationship types with a precise diagnostic");

    const std::filesystem::path non_worksheet_path =
        output_path("fastxlsx-package-reader-workbook-sheets-non-worksheet.xlsx");
    fastxlsx::detail::write_package(non_worksheet_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml",
                R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets></workbook>)"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="styles.xml"/></Relationships>)"},
            {"xl/styles.xml", "<styleSheet/>"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure_contains(non_worksheet_path,
        "workbook sheet relationship target is not a worksheet part",
        "workbook sheet catalog should reject non-worksheet relationship targets with a precise diagnostic");
}

void test_package_reader_ingests_root_source_relationships_as_metadata()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-root-source-rels.xlsx");

    const std::string root_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXml" Target="custom/item1.xml"/>)"
        R"(</Relationships>)";

    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/>)"
                R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
                R"(</Types>)"},
            {"_rels/.rels", "<Relationships/>"},
            {"root.xml", "<root/>"},
            {"_rels/root.xml.rels", root_relationships},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);

    check(reader.part_index().size() == 1,
        "root source relationships should not be indexed as ordinary parts");
    check(reader.part_index().find_part(fastxlsx::detail::PartName("/root.xml")) != nullptr,
        "root source part should be indexed");
    check(reader.part_index().find_part(
              fastxlsx::detail::PartName("/_rels/root.xml.rels")) == nullptr,
        "root source relationships entry should stay metadata-only");
    check(reader.read_entry("_rels/root.xml.rels") == root_relationships,
        "root source relationships bytes should remain readable");

    const auto* root_rels =
        reader.relationships_for(fastxlsx::detail::PartName("/root.xml"));
    check(root_rels != nullptr, "root source relationships should be attached to root part");
    check(root_rels->size() == 1, "root source relationship count mismatch");
    check(root_rels->find_by_id("rId1")->target == "custom/item1.xml",
        "root source relationship target mismatch");

    const fastxlsx::detail::RelationshipGraph graph = reader.relationship_graph();
    const auto* graph_root_rels =
        graph.relationships_for(fastxlsx::detail::PartName("/root.xml"));
    check(graph_root_rels != nullptr,
        "relationship graph should include root source relationships");
    check(graph_root_rels->find_by_id("rId1") != nullptr,
        "relationship graph should preserve root source relationship id");
}

void test_package_reader_ingests_unknown_extension_relationships_as_metadata()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-unknown-extension-rels.xlsx");

    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(</Types>)";
    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(</Relationships>)";
    const std::string worksheet_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId9" Type="https://fastxlsx.invalid/relationships/opaque-extension" Target="../../custom/opaque-extension.bin"/>)"
        R"(</Relationships>)";
    const std::string opaque_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdOpaqueExternal" Type="https://fastxlsx.invalid/relationships/opaque-extension-audit" Target="https://example.invalid/opaque-extension-audit" TargetMode="External"/>)"
        R"(</Relationships>)";
    const std::string opaque_payload("extension\0payload", 17);

    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", "<workbook/>"},
            {"xl/_rels/workbook.xml.rels", workbook_relationships},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
            {"xl/worksheets/_rels/sheet1.xml.rels", worksheet_relationships},
            {"custom/opaque-extension.bin", opaque_payload},
            {"custom/_rels/opaque-extension.bin.rels", opaque_relationships},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);

    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName opaque_part("/custom/opaque-extension.bin");
    const fastxlsx::detail::PartName opaque_relationships_part(
        "/custom/_rels/opaque-extension.bin.rels");

    check(reader.part_index().find_part(opaque_part) != nullptr,
        "unknown extension owner part should be indexed");
    check(reader.part_index().find_part(opaque_relationships_part) == nullptr,
        "unknown extension source-owned relationships should stay metadata-only");
    check(reader.read_entry("custom/opaque-extension.bin") == opaque_payload,
        "unknown extension owner bytes should remain readable");
    check(reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == opaque_relationships,
        "unknown extension owner relationships bytes should remain readable");

    const auto* worksheet_rels = reader.relationships_for(worksheet_part);
    check(worksheet_rels != nullptr,
        "worksheet relationships should be attached before graph construction");
    const auto* opaque_link = worksheet_rels->find_by_id("rId9");
    check(opaque_link != nullptr,
        "worksheet should retain the relationship to the unknown extension part");
    check(opaque_link->target == "../../custom/opaque-extension.bin",
        "worksheet relationship target to unknown extension should remain raw");

    const auto* opaque_rels = reader.relationships_for(opaque_part);
    check(opaque_rels != nullptr,
        "unknown extension source-owned relationships should attach to the owner part");
    const auto* external_audit = opaque_rels->find_by_id("rIdOpaqueExternal");
    check(external_audit != nullptr,
        "unknown extension owner relationships should retain their relationship id");
    check(external_audit->target == "https://example.invalid/opaque-extension-audit",
        "unknown extension owner relationship target mismatch");
    check(external_audit->target_mode
            == fastxlsx::detail::Relationship::TargetMode::External,
        "unknown extension owner relationship TargetMode should be parsed");

    const fastxlsx::detail::RelationshipGraph graph = reader.relationship_graph();
    const auto* graph_opaque_rels = graph.relationships_for(opaque_part);
    check(graph_opaque_rels != nullptr,
        "relationship graph should include unknown extension owner relationships");
    check(graph_opaque_rels->find_by_id("rIdOpaqueExternal") != nullptr,
        "relationship graph should preserve unknown extension owner relationship id");
}


} // namespace

int main()
{
    try {
        test_package_reader_ingests_content_types_and_relationships();
        test_package_reader_resolves_workbook_sheet_catalog();
        test_package_reader_loads_cell_store_from_workbook_sheet();
        test_package_reader_materializes_registry_session_from_workbook_sheet();
        test_package_reader_cell_store_loader_rejects_styles_and_loads_shared_strings();
        test_package_reader_cell_store_loader_rejects_invalid_shared_strings_sources();
        test_package_reader_cell_store_loader_rejects_unsupported_source_cell_shapes();
        test_package_reader_rejects_invalid_workbook_sheet_catalog();
        test_package_reader_ingests_root_source_relationships_as_metadata();
        test_package_reader_ingests_unknown_extension_relationships_as_metadata();
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
