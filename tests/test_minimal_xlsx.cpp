#include <fastxlsx/detail/xml.hpp>
#include <fastxlsx/fastxlsx.hpp>

#include "zip_test_utils.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
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

template <typename Callable>
void check_fastxlsx_error(Callable callable, const char* message)
{
    bool failed = false;
    try {
        callable();
    } catch (const fastxlsx::FastXlsxError&) {
        failed = true;
    }
    check(failed, message);
}

void expect_invalid_in_memory_number(double value, const char* sheet_name, const char* file_name,
    const char* message)
{
    auto workbook = fastxlsx::Workbook::create();
    auto& sheet = workbook.add_worksheet(sheet_name);
    sheet.append_row({fastxlsx::Cell::number(value)});
    check_fastxlsx_error(
        [&workbook, file_name] {
            workbook.save(std::filesystem::current_path() / file_name);
        },
        message);
}

void expect_invalid_in_memory_row_height(double height, const char* sheet_name,
    const char* file_name, const char* message)
{
    auto workbook = fastxlsx::Workbook::create();
    auto& sheet = workbook.add_worksheet(sheet_name);
    const std::vector<fastxlsx::Cell> row {fastxlsx::Cell::text("bad height")};
    sheet.append_row(row, fastxlsx::RowOptions {height});
    check_fastxlsx_error(
        [&workbook, file_name] {
            workbook.save(std::filesystem::current_path() / file_name);
        },
        message);
}

void test_xml_helpers()
{
    check(fastxlsx::detail::escape_xml_text("a&b<c>d") == "a&amp;b&lt;c&gt;d",
        "xml text escaping failed");
    check(fastxlsx::detail::escape_xml_attribute("\"'&<>") == "&quot;&apos;&amp;&lt;&gt;",
        "xml attribute escaping failed");
    check(fastxlsx::detail::cell_reference(1, 1) == "A1", "A1 reference failed");
    check(fastxlsx::detail::cell_reference(1, 26) == "Z1", "Z1 reference failed");
    check(fastxlsx::detail::cell_reference(1, 27) == "AA1", "AA1 reference failed");
    check(fastxlsx::detail::cell_reference(1048576, 16384) == "XFD1048576",
        "last Excel cell reference failed");
    check(fastxlsx::detail::range_reference(1, 1, 1, 1) == "A1",
        "single-cell range reference failed");
    check(fastxlsx::detail::range_reference(1, 1, 2, 2) == "A1:B2",
        "multi-cell range reference failed");

    const std::vector<fastxlsx::CellRange> ranges {
        fastxlsx::CellRange {1, 1, 1, 1},
        fastxlsx::CellRange {1, 2, 2, 3},
        fastxlsx::CellRange {1048576, 16384, 1048576, 16384},
    };
    check(fastxlsx::detail::sqref(ranges) == "A1 B1:C2 XFD1048576",
        "sqref range list failed");

    check_fastxlsx_error([] { (void)fastxlsx::detail::cell_reference(0, 1); },
        "zero row cell reference should fail");
    check_fastxlsx_error([] { (void)fastxlsx::detail::cell_reference(1, 0); },
        "zero column cell reference should fail");
    check_fastxlsx_error([] { (void)fastxlsx::detail::cell_reference(1048577, 1); },
        "row beyond Excel limit should fail");
    check_fastxlsx_error([] { (void)fastxlsx::detail::cell_reference(1, 16385); },
        "column beyond Excel limit should fail");
    check_fastxlsx_error([] { (void)fastxlsx::detail::range_reference(2, 1, 1, 1); },
        "reversed row range should fail");
    check_fastxlsx_error([] { (void)fastxlsx::detail::range_reference(1, 2, 1, 1); },
        "reversed column range should fail");

    const std::vector<fastxlsx::CellRange> invalid_ranges {
        fastxlsx::CellRange {1, 1, 1, 1},
        fastxlsx::CellRange {1, 0, 1, 1},
    };
    check_fastxlsx_error([&invalid_ranges] { (void)fastxlsx::detail::sqref(invalid_ranges); },
        "invalid sqref range should fail");
}

void test_cell_value_public_boundary()
{
    const fastxlsx::CellValue blank = fastxlsx::CellValue::blank();
    check(blank.kind() == fastxlsx::CellValueKind::Blank, "blank CellValue kind mismatch");
    check(!blank.has_style(), "blank CellValue should not carry an explicit style");

    const fastxlsx::CellValue number = fastxlsx::CellValue::number(42.5);
    check(number.kind() == fastxlsx::CellValueKind::Number, "number CellValue kind mismatch");
    check(number.number_value() == 42.5, "number CellValue payload mismatch");

    std::string text_payload = "owned text";
    const fastxlsx::CellValue text = fastxlsx::CellValue::text(text_payload);
    text_payload = "mutated caller text";
    check(text.kind() == fastxlsx::CellValueKind::Text, "text CellValue kind mismatch");
    check(text.text_value() == "owned text", "text CellValue should own caller payload");

    const fastxlsx::CellValue boolean = fastxlsx::CellValue::boolean(true);
    check(boolean.kind() == fastxlsx::CellValueKind::Boolean, "boolean CellValue kind mismatch");
    check(boolean.boolean_value(), "boolean CellValue payload mismatch");

    std::string formula_payload = "SUM(A1:B1)";
    const fastxlsx::CellValue formula = fastxlsx::CellValue::formula(std::move(formula_payload));
    check(formula.kind() == fastxlsx::CellValueKind::Formula, "formula CellValue kind mismatch");
    check(formula.text_value() == "SUM(A1:B1)", "formula CellValue payload mismatch");

    const fastxlsx::Cell source_number = fastxlsx::Cell::number(7.5);
    const fastxlsx::CellValue converted_number = fastxlsx::CellValue::from_cell(source_number);
    check(converted_number.kind() == fastxlsx::CellValueKind::Number,
        "Cell-to-CellValue number conversion mismatch");
    check(converted_number.number_value() == 7.5,
        "Cell-to-CellValue number payload mismatch");

    const fastxlsx::Cell source_formula = fastxlsx::Cell::formula("A1+1");
    const fastxlsx::CellValue converted_formula = fastxlsx::CellValue::from_cell(source_formula);
    check(converted_formula.kind() == fastxlsx::CellValueKind::Formula,
        "Cell-to-CellValue formula conversion mismatch");
    check(converted_formula.text_value() == "A1+1",
        "Cell-to-CellValue formula payload mismatch");

    const std::optional<fastxlsx::Cell> converted_back_number = converted_number.to_cell();
    check(converted_back_number.has_value(), "CellValue number should convert back to Cell");
    check(converted_back_number->type() == fastxlsx::Cell::Type::Number,
        "CellValue-to-Cell number conversion kind mismatch");
    check(converted_back_number->number_value() == 7.5,
        "CellValue-to-Cell number conversion payload mismatch");

    const std::optional<fastxlsx::Cell> converted_back_blank =
        fastxlsx::CellValue::blank().to_cell();
    check(!converted_back_blank.has_value(),
        "blank CellValue should not have a Cell representation");

    const fastxlsx::CellValue styled = text.with_style(fastxlsx::StyleId {});
    check(styled.has_style(), "styled CellValue should carry an explicit style");
    check(styled.style_id().value() == 0, "styled CellValue default style id mismatch");
    check(!text.has_style(), "with_style should not mutate the original CellValue");

    const fastxlsx::CellValue unstyled = styled.without_style();
    check(!unstyled.has_style(), "without_style should clear explicit style");
    check(unstyled.text_value() == "owned text", "without_style should preserve payload");

    check_fastxlsx_error(
        [] { (void)fastxlsx::CellValue::number(std::numeric_limits<double>::quiet_NaN()); },
        "CellValue should reject NaN numeric payloads");
    check_fastxlsx_error(
        [] { (void)fastxlsx::CellValue::number(std::numeric_limits<double>::infinity()); },
        "CellValue should reject infinite numeric payloads");
    check_fastxlsx_error(
        [] { (void)fastxlsx::CellValue::number(-std::numeric_limits<double>::infinity()); },
        "CellValue should reject negative infinite numeric payloads");
}

void test_minimal_xlsx_package()
{
    auto workbook = fastxlsx::Workbook::create();
    auto& sheet = workbook.add_worksheet("Sheet1");
    sheet.append_row({
        fastxlsx::Cell::number(123.5),
        fastxlsx::Cell::text("text & <tag>"),
        fastxlsx::Cell::boolean(true),
    });
    sheet.append_row({
        fastxlsx::Cell::text(" leading "),
        fastxlsx::Cell::boolean(false),
    });

    const auto output_path = std::filesystem::current_path() / "fastxlsx-phase1-minimal.xlsx";
    workbook.save(output_path);
    check(std::filesystem::exists(output_path), "xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("[Content_Types].xml"), "missing content types part");
    check(entries.contains("_rels/.rels"), "missing package relationships part");
    check(entries.contains("docProps/core.xml"), "missing core properties part");
    check(entries.contains("docProps/app.xml"), "missing extended properties part");
    check(entries.contains("xl/workbook.xml"), "missing workbook part");
    check(entries.contains("xl/_rels/workbook.xml.rels"), "missing workbook relationships part");
    check(entries.contains("xl/worksheets/sheet1.xml"), "missing worksheet part");

    const auto& content_types = entries.at("[Content_Types].xml");
    check(content_types.find("application/vnd.openxmlformats-package.relationships+xml")
            != std::string::npos,
        "missing rels default content type");
    check(content_types.find("/xl/workbook.xml") != std::string::npos,
        "missing workbook content type override");
    check(content_types.find("/xl/worksheets/sheet1.xml") != std::string::npos,
        "missing worksheet content type override");
    check(content_types.find("/docProps/core.xml") != std::string::npos,
        "missing core properties content type override");
    check(content_types.find("application/vnd.openxmlformats-package.core-properties+xml")
            != std::string::npos,
        "missing core properties content type");
    check(content_types.find("/docProps/app.xml") != std::string::npos,
        "missing extended properties content type override");
    check(content_types.find("application/vnd.openxmlformats-officedocument.extended-properties+xml")
            != std::string::npos,
        "missing extended properties content type");

    const auto& package_rels = entries.at("_rels/.rels");
    check(package_rels.find("officeDocument") != std::string::npos,
        "missing officeDocument relationship");
    check(package_rels.find("Target=\"xl/workbook.xml\"") != std::string::npos,
        "package relationship target mismatch");
    check(package_rels.find("Target=\"docProps/core.xml\"") != std::string::npos,
        "missing core properties package relationship");
    check(package_rels.find("relationships/metadata/core-properties") != std::string::npos,
        "missing core properties package relationship type");
    check(package_rels.find("Target=\"docProps/app.xml\"") != std::string::npos,
        "missing extended properties package relationship");
    check(package_rels.find("relationships/extended-properties") != std::string::npos,
        "missing extended properties package relationship type");

    const auto& core_properties_xml = entries.at("docProps/core.xml");
    check(core_properties_xml.find("<cp:coreProperties ") != std::string::npos,
        "core properties root missing");
    check(core_properties_xml.find("<dc:creator>FastXLSX</dc:creator>") != std::string::npos,
        "core properties creator missing");
    check(core_properties_xml.find("<cp:lastModifiedBy>FastXLSX</cp:lastModifiedBy>")
            != std::string::npos,
        "core properties lastModifiedBy missing");

    const auto& extended_properties_xml = entries.at("docProps/app.xml");
    check(extended_properties_xml.find("<Properties ") != std::string::npos,
        "extended properties root missing");
    check(extended_properties_xml.find("<Application>FastXLSX</Application>") != std::string::npos,
        "extended properties application missing");
    check(extended_properties_xml.find("<AppVersion>0.1</AppVersion>") != std::string::npos,
        "extended properties app version missing");

    const auto& workbook_xml = entries.at("xl/workbook.xml");
    check(workbook_xml.find("name=\"Sheet1\"") != std::string::npos,
        "workbook sheet name missing");
    check(workbook_xml.find("r:id=\"rId1\"") != std::string::npos,
        "workbook relationship id missing");

    const auto& workbook_rels = entries.at("xl/_rels/workbook.xml.rels");
    check(workbook_rels.find("Target=\"worksheets/sheet1.xml\"") != std::string::npos,
        "worksheet relationship target mismatch");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check(worksheet_xml.find("<sheetData>") != std::string::npos,
        "worksheet sheetData missing");
    check(worksheet_xml.find("</worksheet>") != std::string::npos,
        "worksheet XML closing tag missing");
    check(worksheet_xml.find("<dimension ref=\"A1:C2\"/>") != std::string::npos,
        "worksheet dimension mismatch");
    check(worksheet_xml.find("<c r=\"A1\"><v>123.5</v></c>") != std::string::npos,
        "numeric cell encoding mismatch");
    check(worksheet_xml.find("<c r=\"B1\" t=\"inlineStr\"><is><t>text &amp; &lt;tag&gt;</t></is></c>")
            != std::string::npos,
        "inline string encoding mismatch");
    check(worksheet_xml.find("<c r=\"C1\" t=\"b\"><v>1</v></c>") != std::string::npos,
        "boolean true cell encoding mismatch");
    check(worksheet_xml.find("<t xml:space=\"preserve\"> leading </t>") != std::string::npos,
        "xml:space preserve missing");
    check(worksheet_xml.find("<c r=\"B2\" t=\"b\"><v>0</v></c>") != std::string::npos,
        "boolean false cell encoding mismatch");
}

void test_workbook_document_properties()
{
    auto workbook = fastxlsx::Workbook::create();

    fastxlsx::DocumentProperties properties;
    properties.creator = "Alice & Bob";
    properties.last_modified_by = "QA <Owner>";
    properties.title = "Quarterly <Report>";
    properties.subject = "Metadata & API";
    properties.description = "Generated by FastXLSX <test>";
    properties.keywords = "xlsx;docprops";
    properties.category = "Validation";
    properties.application = "FastXLSX Unit & Tools";
    properties.app_version = "2.5";
    workbook.set_document_properties(properties);

    auto& sheet = workbook.add_worksheet("Props");
    sheet.append_row({fastxlsx::Cell::text("doc props")});

    const auto output_path =
        std::filesystem::current_path() / "fastxlsx-document-properties.xlsx";
    workbook.save(output_path);
    check(std::filesystem::exists(output_path), "document properties xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("docProps/core.xml"), "missing custom core properties part");
    check(entries.contains("docProps/app.xml"), "missing custom extended properties part");
    check(!entries.contains("docProps/custom.xml"),
        "document properties API should not create custom properties part");

    const auto& package_rels = entries.at("_rels/.rels");
    check(package_rels.find("Target=\"docProps/core.xml\"") != std::string::npos,
        "custom document properties should keep core properties package relationship");
    check(package_rels.find("Target=\"docProps/app.xml\"") != std::string::npos,
        "custom document properties should keep extended properties package relationship");

    const auto& core_properties_xml = entries.at("docProps/core.xml");
    check(core_properties_xml.find("<dc:creator>Alice &amp; Bob</dc:creator>")
            != std::string::npos,
        "custom workbook creator escaping mismatch");
    check(core_properties_xml.find("<cp:lastModifiedBy>QA &lt;Owner&gt;</cp:lastModifiedBy>")
            != std::string::npos,
        "custom workbook lastModifiedBy escaping mismatch");
    check(core_properties_xml.find("<dc:title>Quarterly &lt;Report&gt;</dc:title>")
            != std::string::npos,
        "custom workbook title escaping mismatch");
    check(core_properties_xml.find("<dc:subject>Metadata &amp; API</dc:subject>")
            != std::string::npos,
        "custom workbook subject escaping mismatch");
    check(core_properties_xml.find("<dc:description>Generated by FastXLSX &lt;test&gt;</dc:description>")
            != std::string::npos,
        "custom workbook description escaping mismatch");
    check(core_properties_xml.find("<cp:keywords>xlsx;docprops</cp:keywords>")
            != std::string::npos,
        "custom workbook keywords mismatch");
    check(core_properties_xml.find("<cp:category>Validation</cp:category>") != std::string::npos,
        "custom workbook category mismatch");

    const auto& extended_properties_xml = entries.at("docProps/app.xml");
    check(extended_properties_xml.find("<Application>FastXLSX Unit &amp; Tools</Application>")
            != std::string::npos,
        "custom workbook application escaping mismatch");
    check(extended_properties_xml.find("<AppVersion>2.5</AppVersion>") != std::string::npos,
        "custom workbook app version mismatch");
}

void test_workbook_formula_and_row_height_metadata()
{
    auto workbook = fastxlsx::Workbook::create();
    auto& sheet = workbook.add_worksheet("FormulaHeight");

    sheet.append_row({
        fastxlsx::Cell::number(1.0),
        fastxlsx::Cell::number(2.0),
        fastxlsx::Cell::formula("SUM(A1:B1)&\"<ok>\""),
    });
    const std::vector<fastxlsx::Cell> row {
        fastxlsx::Cell::text("tall row"),
    };
    sheet.append_row(row, fastxlsx::RowOptions {18.5});

    const auto output_path =
        std::filesystem::current_path() / "fastxlsx-formula-row-height.xlsx";
    workbook.save(output_path);
    check(std::filesystem::exists(output_path), "formula and row height xlsx was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    const auto& workbook_xml = entries.at("xl/workbook.xml");
    check(workbook_xml.find(R"(<calcPr calcId="124519" fullCalcOnLoad="1"/>)")
            != std::string::npos,
        "formula workbook should request full recalculation on load");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check(worksheet_xml.find("<dimension ref=\"A1:C2\"/>") != std::string::npos,
        "formula workbook dimension mismatch");
    check(worksheet_xml.find("<c r=\"C1\"><f>SUM(A1:B1)&amp;\"&lt;ok&gt;\"</f></c>")
            != std::string::npos,
        "formula XML escaping mismatch");
    check(worksheet_xml.find("<row r=\"2\" ht=\"18.5\" customHeight=\"1\">")
            != std::string::npos,
        "row height metadata XML mismatch");
    check(!entries.contains("xl/calcChain.xml"),
        "in-memory formula writer should not create calcChain");
    check(!entries.contains("xl/styles.xml"),
        "row height metadata should not create styles");
}

void test_workbook_dimension_and_column_boundaries()
{
    {
        auto workbook = fastxlsx::Workbook::create();
        workbook.add_worksheet("Empty");

        const auto output_path =
            std::filesystem::current_path() / "fastxlsx-empty-worksheet.xlsx";
        workbook.save(output_path);

        const auto entries = fastxlsx::test::read_zip_entries(output_path);
        const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
        check(worksheet_xml.find("<dimension ref=\"A1\"/>") != std::string::npos,
            "empty in-memory worksheet dimension mismatch");
        check(worksheet_xml.find("<sheetData></sheetData>") != std::string::npos,
            "empty in-memory worksheet sheetData mismatch");
    }

    {
        auto workbook = fastxlsx::Workbook::create();
        auto& sheet = workbook.add_worksheet("EmptyRow");
        sheet.append_row({});

        const auto output_path =
            std::filesystem::current_path() / "fastxlsx-empty-row-worksheet.xlsx";
        workbook.save(output_path);

        const auto entries = fastxlsx::test::read_zip_entries(output_path);
        const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
        check(worksheet_xml.find("<dimension ref=\"A1:A1\"/>") != std::string::npos,
            "single empty in-memory row dimension mismatch");
        check(worksheet_xml.find("<row r=\"1\"></row>") != std::string::npos,
            "single empty in-memory row XML mismatch");
    }

    {
        auto workbook = fastxlsx::Workbook::create();
        auto& sheet = workbook.add_worksheet("MaxColumns");
        const std::vector<fastxlsx::Cell> max_columns(16384, fastxlsx::Cell::number(1.0));
        sheet.append_row(max_columns);

        const auto output_path =
            std::filesystem::current_path() / "fastxlsx-max-columns.xlsx";
        workbook.save(output_path);

        const auto entries = fastxlsx::test::read_zip_entries(output_path);
        const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
        check(worksheet_xml.find("<dimension ref=\"A1:XFD1\"/>") != std::string::npos,
            "max-column in-memory worksheet dimension mismatch");
        check(worksheet_xml.find("<c r=\"XFD1\"><v>1</v></c>") != std::string::npos,
            "max-column in-memory cell reference mismatch");
    }
}

void test_validation_errors()
{
    auto workbook = fastxlsx::Workbook::create();
    bool empty_name_failed = false;
    try {
        workbook.add_worksheet("");
    } catch (const fastxlsx::FastXlsxError&) {
        empty_name_failed = true;
    }
    check(empty_name_failed, "empty worksheet name should fail");

    bool empty_workbook_failed = false;
    try {
        workbook.save(std::filesystem::current_path() / "invalid-empty.xlsx");
    } catch (const fastxlsx::FastXlsxError&) {
        empty_workbook_failed = true;
    }
    check(empty_workbook_failed, "empty workbook save should fail");

    expect_invalid_in_memory_number(std::numeric_limits<double>::quiet_NaN(),
        "InvalidNumberNaN", "invalid-nan-number.xlsx",
        "workbook save should reject NaN numeric cells");
    expect_invalid_in_memory_number(std::numeric_limits<double>::infinity(),
        "InvalidNumberPositiveInf", "invalid-positive-infinity-number.xlsx",
        "workbook save should reject positive infinity numeric cells");
    expect_invalid_in_memory_number(-std::numeric_limits<double>::infinity(),
        "InvalidNumberNegativeInf", "invalid-negative-infinity-number.xlsx",
        "workbook save should reject negative infinity numeric cells");

    expect_invalid_in_memory_row_height(0.0, "InvalidZeroHeight",
        "invalid-zero-height.xlsx", "workbook save should reject zero row heights");
    expect_invalid_in_memory_row_height(-1.0, "InvalidNegativeHeight",
        "invalid-negative-height.xlsx", "workbook save should reject negative row heights");
    expect_invalid_in_memory_row_height(std::numeric_limits<double>::quiet_NaN(),
        "InvalidNaNHeight", "invalid-nan-height.xlsx",
        "workbook save should reject NaN row heights");
    expect_invalid_in_memory_row_height(std::numeric_limits<double>::infinity(),
        "InvalidPositiveInfHeight", "invalid-positive-infinity-height.xlsx",
        "workbook save should reject positive infinity row heights");
    expect_invalid_in_memory_row_height(-std::numeric_limits<double>::infinity(),
        "InvalidNegativeInfHeight", "invalid-negative-infinity-height.xlsx",
        "workbook save should reject negative infinity row heights");

    {
        auto too_wide_workbook = fastxlsx::Workbook::create();
        auto& sheet = too_wide_workbook.add_worksheet("TooWide");
        const std::vector<fastxlsx::Cell> too_wide_row(16385, fastxlsx::Cell::number(1.0));
        sheet.append_row(too_wide_row);
        check_fastxlsx_error(
            [&too_wide_workbook] {
                too_wide_workbook.save(
                    std::filesystem::current_path() / "invalid-too-wide-row.xlsx");
            },
            "workbook save should reject rows beyond Excel's column limit");
    }
}

} // namespace

int main()
{
    try {
        test_xml_helpers();
        test_cell_value_public_boundary();
        test_minimal_xlsx_package();
        test_workbook_document_properties();
        test_workbook_formula_and_row_height_metadata();
        test_workbook_dimension_and_column_boundaries();
        test_validation_errors();
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
