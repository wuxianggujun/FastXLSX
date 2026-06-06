#include <fastxlsx/detail/xml.hpp>
#include <fastxlsx/fastxlsx.hpp>

#include "zip_test_utils.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
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
}

} // namespace

int main()
{
    try {
        test_xml_helpers();
        test_minimal_xlsx_package();
        test_validation_errors();
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
