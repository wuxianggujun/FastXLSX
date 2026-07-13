#include <fastxlsx/detail/cell_store.hpp>
#include <fastxlsx/detail/materialized_worksheet_session.hpp>
#include <fastxlsx/detail/xml.hpp>
#include <fastxlsx/fastxlsx.hpp>

#include "zip_test_utils.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
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
            workbook.save(fastxlsx::test::artifact_dir() / file_name);
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
            workbook.save(fastxlsx::test::artifact_dir() / file_name);
        },
        message);
}

void test_xml_helpers()
{
    check(fastxlsx::detail::escape_xml_text("a&b<c>d") == "a&amp;b&lt;c&gt;d",
        "xml text escaping failed");
    check(fastxlsx::detail::escape_xml_attribute("\"'&<>") == "&quot;&apos;&amp;&lt;&gt;",
        "xml attribute escaping failed");
    std::string appended_text = "text=";
    fastxlsx::detail::append_escaped_xml_text(appended_text, "a&b<c>d");
    check(appended_text == "text=a&amp;b&lt;c&gt;d",
        "append_escaped_xml_text should preserve existing text");
    std::string appended_attribute = "attr=";
    fastxlsx::detail::append_escaped_xml_attribute(appended_attribute, "\"'&<>");
    check(appended_attribute == "attr=&quot;&apos;&amp;&lt;&gt;",
        "append_escaped_xml_attribute should preserve existing text");
    check(fastxlsx::detail::cell_reference(1, 1) == "A1", "A1 reference failed");
    check(fastxlsx::detail::cell_reference(1, 26) == "Z1", "Z1 reference failed");
    check(fastxlsx::detail::cell_reference(1, 27) == "AA1", "AA1 reference failed");
    check(fastxlsx::detail::cell_reference(1048576, 16384) == "XFD1048576",
        "last Excel cell reference failed");
    std::string appended_reference = "ref=";
    fastxlsx::detail::append_cell_reference(appended_reference, 1048576, 16384);
    check(appended_reference == "ref=XFD1048576",
        "append_cell_reference should preserve existing text");
    std::string appended_number = "value=";
    fastxlsx::detail::append_number(appended_number, 123.5);
    check(appended_number == "value=123.5", "append_number should preserve existing text");
    std::string appended_unsigned_zero = "uint=";
    fastxlsx::detail::append_unsigned_decimal(appended_unsigned_zero, std::uint64_t {0});
    check(appended_unsigned_zero == "uint=0",
        "append_unsigned_decimal should append zero and preserve existing text");
    std::string appended_unsigned_max = "uint=";
    fastxlsx::detail::append_unsigned_decimal(
        appended_unsigned_max, std::numeric_limits<std::uint32_t>::max());
    check(appended_unsigned_max == "uint=4294967295",
        "append_unsigned_decimal should append uint32 max");
    std::string appended_unsigned_64_max = "uint64=";
    fastxlsx::detail::append_unsigned_decimal(
        appended_unsigned_64_max, std::numeric_limits<std::uint64_t>::max());
    check(appended_unsigned_64_max == "uint64=18446744073709551615",
        "append_unsigned_decimal should append uint64 max");
    const std::string formatted_scientific = fastxlsx::detail::format_number(1.25e-10);
    std::string appended_scientific;
    fastxlsx::detail::append_number(appended_scientific, 1.25e-10);
    check(appended_scientific == formatted_scientific,
        "append_number should match format_number output");
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
    check_fastxlsx_error(
        [] {
            std::string output;
            fastxlsx::detail::append_cell_reference(output, 0, 1);
        },
        "append_cell_reference should reject zero row");
    check_fastxlsx_error(
        [] {
            std::string output;
            fastxlsx::detail::append_number(output, std::numeric_limits<double>::infinity());
        },
        "append_number should reject infinite values");
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

    std::string error_payload = "#VALUE!";
    const fastxlsx::CellValue error = fastxlsx::CellValue::error(std::move(error_payload));
    check(error.kind() == fastxlsx::CellValueKind::Error, "error CellValue kind mismatch");
    check(error.text_value() == "#VALUE!", "error CellValue payload mismatch");
    check(!error.to_cell().has_value(), "error CellValue should not have a Cell representation");

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
    check_fastxlsx_error(
        [] { (void)fastxlsx::CellValue::formula(""); },
        "CellValue should reject empty formula payloads");
    check_fastxlsx_error(
        [] { (void)fastxlsx::Cell::formula(""); },
        "Cell should reject empty formula payloads");
    check_fastxlsx_error(
        [] { (void)fastxlsx::CellValue::error(""); },
        "CellValue should reject empty error payloads");
}

void test_workbook_sheet_inspection_helpers()
{
    auto workbook = fastxlsx::Workbook::create();
    check(workbook.worksheet_count() == 0, "new workbook should start with zero sheets");
    check(workbook.worksheet_names().empty(), "new workbook should start with empty names");
    check(!workbook.has_worksheet("Data"), "new workbook should not report missing sheets");
    check(!workbook.try_worksheet("Data").has_value(),
        "new workbook should return empty optional for missing sheets");

    workbook.add_worksheet("Data").append_row({fastxlsx::Cell::text("alpha")});
    workbook.add_worksheet("Summary").append_row({fastxlsx::Cell::number(1.0)});

    check(workbook.worksheet_count() == 2, "workbook sheet count should track additions");

    const std::vector<std::string> names = workbook.worksheet_names();
    check(names.size() == 2 && names[0] == "Data" && names[1] == "Summary",
        "worksheet_names should preserve workbook order");
    check(workbook.has_worksheet("Data"), "has_worksheet should find the first sheet");
    check(workbook.has_worksheet("Summary"), "has_worksheet should find the second sheet");
    check(workbook.has_worksheet("data"),
        "has_worksheet should use ASCII case-insensitive matching");

    fastxlsx::Worksheet& data = workbook.worksheet("Data");
    fastxlsx::Worksheet& summary = workbook.worksheet("Summary");
    check(&workbook.worksheet("Data") == &data,
        "worksheet lookup should return the matching mutable worksheet");
    check(&workbook.worksheet("summary") == &summary,
        "worksheet lookup should use ASCII case-insensitive matching");
    const fastxlsx::Workbook& const_workbook = workbook;
    check(&const_workbook.worksheet("DATA") == &data,
        "const worksheet lookup should use ASCII case-insensitive matching");
    check(&workbook.try_worksheet("Summary")->get() == &summary,
        "try_worksheet should return the matching mutable worksheet");
    check(&const_workbook.try_worksheet("summary")->get() == &summary,
        "const try_worksheet should use ASCII case-insensitive matching");

    check_fastxlsx_error(
        [&workbook] { (void)workbook.worksheet("Missing"); },
        "worksheet lookup should reject missing names");
}

void test_workbook_sheet_addition_uniqueness()
{
    auto workbook = fastxlsx::Workbook::create();

    workbook.add_worksheet("Data").append_row({fastxlsx::Cell::text("alpha")});

    check_fastxlsx_error(
        [&workbook] { workbook.add_worksheet("data"); },
        "workbook add_worksheet should reject ASCII case-insensitive duplicate sheet names");
    check_fastxlsx_error(
        [&workbook] { workbook.add_worksheet("Data"); },
        "workbook add_worksheet should reject exact duplicate sheet names");
    check_fastxlsx_error(
        [&workbook] { workbook.add_worksheet(""); },
        "workbook add_worksheet should reject empty sheet names without mutation");
    check_fastxlsx_error(
        [&workbook] { workbook.add_worksheet("Bad/Name"); },
        "workbook add_worksheet should reject invalid sheet-name characters without mutation");
    check_fastxlsx_error(
        [&workbook] { workbook.add_worksheet(std::string(32, 'A')); },
        "workbook add_worksheet should reject overlong sheet names without mutation");

    check(workbook.worksheet_count() == 1, "failed add_worksheet should not change sheet count");
    const std::vector<std::string> names = workbook.worksheet_names();
    check(names.size() == 1 && names[0] == "Data",
        "failed add_worksheet should preserve the existing sheet order");
    check(workbook.worksheet("Data").row_count() == 1,
        "failed add_worksheet should preserve existing sheet rows");

    workbook.add_worksheet("Extra").append_row({fastxlsx::Cell::boolean(true)});
    check(workbook.worksheet_count() == 2,
        "valid add_worksheet should still work after rejected sheet names");
    check(workbook.worksheet_names()[1] == "Extra",
        "valid add_worksheet should append after rejected sheet names");
}

void test_workbook_sheet_removal_helpers()
{
    auto workbook = fastxlsx::Workbook::create();
    workbook.add_worksheet("Data").append_row({fastxlsx::Cell::text("alpha")});
    workbook.add_worksheet("Scratch").append_row({fastxlsx::Cell::formula("SUM(A1:A1)")});
    workbook.add_worksheet("Summary").append_row({fastxlsx::Cell::number(42.0)});

    workbook.remove_worksheet("scratch");

    check(workbook.worksheet_count() == 2, "remove_worksheet should reduce sheet count");
    const std::vector<std::string> names = workbook.worksheet_names();
    check(names.size() == 2 && names[0] == "Data" && names[1] == "Summary",
        "remove_worksheet should preserve remaining workbook order");
    check(workbook.has_worksheet("Data"), "remove_worksheet should preserve earlier sheets");
    check(!workbook.has_worksheet("Scratch"), "remove_worksheet should hide the erased sheet");
    check(workbook.try_worksheet("Scratch") == std::nullopt,
        "try_worksheet should return empty after removal");
    check(workbook.worksheet("Summary").row_count() == 1,
        "remove_worksheet should preserve later sheet rows");

    check_fastxlsx_error(
        [&workbook] { workbook.remove_worksheet("Missing"); },
        "remove_worksheet should reject missing sheets");
    const std::vector<std::string> names_after_failed_remove = workbook.worksheet_names();
    check(names_after_failed_remove == names,
        "failed remove_worksheet should not mutate workbook order");

    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-remove-worksheet.xlsx";
    workbook.save(output_path);
    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/worksheets/sheet1.xml"),
        "remaining first sheet part should be generated");
    check(entries.contains("xl/worksheets/sheet2.xml"),
        "remaining second sheet part should be generated");
    check(!entries.contains("xl/worksheets/sheet3.xml"),
        "removed sheet should not leave a stale worksheet part");

    const auto& content_types = entries.at("[Content_Types].xml");
    check(content_types.find("/xl/worksheets/sheet1.xml") != std::string::npos,
        "content types should contain first remaining worksheet");
    check(content_types.find("/xl/worksheets/sheet2.xml") != std::string::npos,
        "content types should contain second remaining worksheet");
    check(content_types.find("/xl/worksheets/sheet3.xml") == std::string::npos,
        "content types should not contain removed worksheet");

    const auto& workbook_xml = entries.at("xl/workbook.xml");
    check(workbook_xml.find(R"(name="Data" sheetId="1" r:id="rId1")")
            != std::string::npos,
        "workbook catalog should renumber first remaining sheet");
    check(workbook_xml.find(R"(name="Summary" sheetId="2" r:id="rId2")")
            != std::string::npos,
        "workbook catalog should renumber second remaining sheet");
    check(workbook_xml.find("Scratch") == std::string::npos,
        "workbook catalog should not contain removed sheet name");
    check(workbook_xml.find("<calcPr") == std::string::npos,
        "formula cells from removed sheets should not request recalculation");

    const auto& workbook_rels = entries.at("xl/_rels/workbook.xml.rels");
    check(workbook_rels.find(R"(Id="rId1")") != std::string::npos
            && workbook_rels.find(R"(Target="worksheets/sheet1.xml")") != std::string::npos,
        "workbook rels should target first remaining sheet");
    check(workbook_rels.find(R"(Id="rId2")") != std::string::npos
            && workbook_rels.find(R"(Target="worksheets/sheet2.xml")") != std::string::npos,
        "workbook rels should target second remaining sheet");
    check(workbook_rels.find("sheet3.xml") == std::string::npos,
        "workbook rels should not target removed sheet");

    check(entries.at("xl/worksheets/sheet1.xml").find("alpha") != std::string::npos,
        "first remaining sheet payload should be preserved");
    check(entries.at("xl/worksheets/sheet2.xml").find(R"(<c r="A1"><v>42</v></c>)")
            != std::string::npos,
        "second remaining sheet payload should be preserved");

    auto empty_after_remove = fastxlsx::Workbook::create();
    empty_after_remove.add_worksheet("Only");
    empty_after_remove.remove_worksheet("Only");
    check(empty_after_remove.worksheet_count() == 0,
        "removing the only worksheet should leave an empty workbook");
    check_fastxlsx_error(
        [&empty_after_remove] {
            empty_after_remove.save(
                fastxlsx::test::artifact_dir() / "invalid-empty-after-remove.xlsx");
        },
        "save should reject a workbook emptied by remove_worksheet");
}

void test_workbook_sheet_rename_helpers()
{
    auto workbook = fastxlsx::Workbook::create();
    workbook.add_worksheet("Data").append_row({fastxlsx::Cell::text("alpha")});
    workbook.add_worksheet("Summary").append_row({fastxlsx::Cell::number(42.0)});

    workbook.rename_worksheet("data", "Renamed & Data");

    check(workbook.worksheet_count() == 2, "rename_worksheet should preserve sheet count");
    const std::vector<std::string> names = workbook.worksheet_names();
    check(names.size() == 2 && names[0] == "Renamed & Data" && names[1] == "Summary",
        "rename_worksheet should preserve workbook order");
    check(!workbook.has_worksheet("Data"), "rename_worksheet should hide the old name");
    check(workbook.has_worksheet("Renamed & Data"),
        "rename_worksheet should expose the new name");
    check(workbook.worksheet("Renamed & Data").row_count() == 1,
        "rename_worksheet should preserve renamed sheet rows");
    check(workbook.worksheet("Summary").row_count() == 1,
        "rename_worksheet should preserve other sheet rows");

    check_fastxlsx_error(
        [&workbook] { workbook.rename_worksheet("Missing", "Other"); },
        "rename_worksheet should reject missing sheets");
    const std::vector<std::string> names_after_missing_rename = workbook.worksheet_names();
    check(names_after_missing_rename == names,
        "failed rename_worksheet should not mutate workbook order");

    check_fastxlsx_error(
        [&workbook] { workbook.rename_worksheet("Renamed & Data", "Summary"); },
        "rename_worksheet should reject duplicate sheet names");
    check_fastxlsx_error(
        [&workbook] { workbook.rename_worksheet("Renamed & Data", "summary"); },
        "rename_worksheet should reject ASCII case-insensitive duplicate sheet names");
    check_fastxlsx_error(
        [&workbook] { workbook.rename_worksheet("Renamed & Data", "Renamed & Data"); },
        "rename_worksheet should reject a no-op same-name rename");
    check_fastxlsx_error(
        [&workbook] { workbook.rename_worksheet("Renamed & Data", "Bad/Name"); },
        "rename_worksheet should reject invalid sheet-name characters");
    check_fastxlsx_error(
        [&workbook] { workbook.rename_worksheet("Renamed & Data", std::string(32, 'A')); },
        "rename_worksheet should reject overlong sheet names");
    const std::vector<std::string> names_after_duplicate_rename = workbook.worksheet_names();
    check(names_after_duplicate_rename == names,
        "failed rename_worksheet should not mutate workbook order");

    workbook.rename_worksheet("renamed & data", "Final Name");
    const std::vector<std::string> renamed_again = workbook.worksheet_names();
    check(renamed_again.size() == 2 && renamed_again[0] == "Final Name" && renamed_again[1] == "Summary",
        "rename_worksheet should allow a later valid rename after failures");

    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-rename-worksheet.xlsx";
    workbook.save(output_path);
    const auto entries = fastxlsx::test::read_zip_entries(output_path);

    const auto& workbook_xml = entries.at("xl/workbook.xml");
    check(workbook_xml.find(R"(name="Final Name" sheetId="1" r:id="rId1")")
            != std::string::npos,
        "workbook catalog should serialize the renamed first sheet");
    check(workbook_xml.find(R"(name="Summary" sheetId="2" r:id="rId2")")
            != std::string::npos,
        "workbook catalog should keep the second sheet");
    check(workbook_xml.find("Renamed &amp; Data") == std::string::npos,
        "workbook catalog should reflect the later rename target only");
    check(entries.at("xl/worksheets/sheet1.xml").find("alpha") != std::string::npos,
        "renamed first sheet payload should be preserved");
    check(entries.at("xl/worksheets/sheet2.xml").find(R"(<c r="A1"><v>42</v></c>)")
            != std::string::npos,
        "renamed second sheet payload should be preserved");
}

void test_workbook_memory_diagnostics()
{
    auto workbook = fastxlsx::Workbook::create();
    check(workbook.cell_count() == 0, "new workbook should start with zero buffered cells");
    const std::size_t empty_memory = workbook.estimated_memory_usage();
    check(empty_memory >= sizeof(fastxlsx::Workbook),
        "empty workbook memory estimate should include the workbook object");

    workbook.add_worksheet("Data")
        .append_row({fastxlsx::Cell::text("alpha"), fastxlsx::Cell::number(1.0)});
    workbook.worksheet("Data").append_row({});
    workbook.add_worksheet("Summary")
        .append_row({fastxlsx::Cell::formula("SUM(Data!B1:B1)")});

    fastxlsx::Worksheet& data = workbook.worksheet("Data");
    fastxlsx::Worksheet& summary = workbook.worksheet("Summary");

    check(workbook.cell_count() == 3,
        "workbook cell_count should sum buffered cells across worksheets");
    check(data.cell_count() == 2, "worksheet cell_count should count buffered cells");
    check(summary.cell_count() == 1, "worksheet cell_count should count formula cells");

    const std::size_t workbook_memory = workbook.estimated_memory_usage();
    check(workbook_memory > empty_memory,
        "workbook memory estimate should grow after buffering rows and text");
    check(data.estimated_memory_usage() > sizeof(fastxlsx::Worksheet),
        "worksheet memory estimate should grow after buffering rows");
    check(summary.estimated_memory_usage() > 0,
        "formula worksheet memory estimate should be non-zero");

    const std::size_t data_memory_before_rename = data.estimated_memory_usage();
    workbook.rename_worksheet("Data", "Renamed Data");
    check(workbook.cell_count() == 3,
        "worksheet rename should not change buffered cell count");
    const std::size_t workbook_memory_after_rename = workbook.estimated_memory_usage();
    check(workbook_memory_after_rename >= workbook_memory,
        "worksheet rename should not shrink the workbook memory estimate");
    check(workbook.worksheet("Renamed Data").estimated_memory_usage()
            >= data_memory_before_rename,
        "worksheet rename should not shrink the buffered worksheet memory estimate");

    workbook.remove_worksheet("summary");
    check(workbook.worksheet_count() == 1,
        "worksheet removal should reduce the workbook sheet count");
    check(workbook.cell_count() == 2,
        "worksheet removal should subtract erased buffered cells");
    check(!workbook.has_worksheet("Summary"),
        "worksheet removal should hide the erased formula sheet");
    check(workbook.worksheet("Renamed Data").cell_count() == 2,
        "worksheet removal should preserve remaining buffered cells");
    const std::size_t workbook_memory_after_remove = workbook.estimated_memory_usage();
    check(workbook_memory_after_remove >= empty_memory,
        "worksheet removal should leave a valid workbook memory estimate");
    check(workbook_memory_after_remove < workbook_memory_after_rename,
        "worksheet removal should lower the estimate for erased worksheet storage");

    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-memory-diagnostics-after-remove.xlsx";
    workbook.save(output_path);
    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.at("xl/workbook.xml").find("<calcPr") == std::string::npos,
        "removed formula-only sheets should not leave workbook calculation metadata");
    check(!entries.contains("xl/worksheets/sheet2.xml"),
        "removed sheets should not leave stale generated worksheet parts");
}

void test_internal_cell_store_sparse_boundary()
{
    fastxlsx::detail::CellStore store;
    check(store.empty(), "new CellStore should be empty");
    check(store.cell_count() == 0, "new CellStore count should be zero");
    const std::size_t empty_memory = store.estimated_memory_usage();
    check(empty_memory >= sizeof(fastxlsx::detail::CellStore),
        "empty CellStore memory estimate should include the store object");

    check(store.set_cell(3, 3, fastxlsx::CellValue::number(3.25)),
        "CellStore set_cell should report a new sparse record");
    check(!store.set_cell(3, 3, fastxlsx::CellValue::number(3.25)),
        "CellStore set_cell should report a final-state-equal record as a no-op");
    const fastxlsx::detail::CellRecord* number = store.try_cell(3, 3);
    check(number != nullptr, "CellStore should find an inserted numeric cell");
    check(number->kind == fastxlsx::CellValueKind::Number,
        "CellStore numeric record kind mismatch");
    check(number->number_value == 3.25, "CellStore numeric record payload mismatch");
    check(store.find_cell(3, 3) == number, "CellStore find_cell should alias try_cell");

    std::string text_payload = "stored text";
    store.set_cell(1, 2, fastxlsx::CellValue::text(text_payload));
    text_payload = "mutated caller text";
    const fastxlsx::detail::CellRecord* text = store.find_cell(1, 2);
    check(text != nullptr, "CellStore should find an inserted text cell");
    check(text->kind == fastxlsx::CellValueKind::Text, "CellStore text record kind mismatch");
    check(text->text_value == "stored text", "CellStore should own stored text payload");

    const fastxlsx::CellValue styled_formula =
        fastxlsx::CellValue::formula("A1+B1").with_style(fastxlsx::StyleId {});
    store.set_cell(2, 1, styled_formula);
    const fastxlsx::detail::CellRecord* formula = store.find_cell(2, 1);
    check(formula != nullptr, "CellStore should find an inserted formula cell");
    check(formula->kind == fastxlsx::CellValueKind::Formula,
        "CellStore formula record kind mismatch");
    check(formula->text_value == "A1+B1", "CellStore formula payload mismatch");
    check(!formula->style_id.has_value(),
        "CellStore should normalize explicit default StyleId to no style handle");
    const fastxlsx::CellValue round_tripped_formula = formula->to_value();
    check(round_tripped_formula.kind() == fastxlsx::CellValueKind::Formula,
        "CellRecord should convert back to a formula CellValue");
    check(round_tripped_formula.text_value() == "A1+B1",
        "CellRecord formula round trip payload mismatch");
    check(!round_tripped_formula.has_style(),
        "CellRecord should round trip normalized default StyleId as no style");

    store.set_cell(3, 4, fastxlsx::CellValue::error("#DIV/0!"));
    const fastxlsx::detail::CellRecord* error = store.find_cell(3, 4);
    check(error != nullptr, "CellStore should find an inserted error cell");
    check(error->kind == fastxlsx::CellValueKind::Error,
        "CellStore error record kind mismatch");
    check(error->text_value == "#DIV/0!", "CellStore error payload mismatch");
    const fastxlsx::CellValue round_tripped_error = error->to_value();
    check(round_tripped_error.kind() == fastxlsx::CellValueKind::Error,
        "CellRecord should convert back to an error CellValue");
    check(round_tripped_error.text_value() == "#DIV/0!",
        "CellRecord error round trip payload mismatch");
    check(!round_tripped_error.to_cell().has_value(),
        "CellRecord error should round trip without a Cell representation");

    check(store.set_cell(2, 1, fastxlsx::CellValue::boolean(false)),
        "CellStore set_cell should report an effective replacement");
    check(!store.set_cell(2, 1, fastxlsx::CellValue::boolean(false)),
        "CellStore set_cell should report an equal replacement as a no-op");
    check(store.cell_count() == 4, "CellStore overwrite should not grow the sparse index");
    const fastxlsx::detail::CellRecord* boolean = store.find_cell(2, 1);
    check(boolean != nullptr, "CellStore should find an overwritten boolean cell");
    check(boolean->kind == fastxlsx::CellValueKind::Boolean,
        "CellStore boolean record kind mismatch");
    check(!boolean->boolean_value, "CellStore boolean payload mismatch");

    auto iterator = store.records().begin();
    check(iterator != store.records().end(), "CellStore records should not be empty");
    check(iterator->first.row == 1 && iterator->first.column == 2,
        "CellStore records should be ordered row-major");
    ++iterator;
    check(iterator != store.records().end(), "CellStore should have a second sparse record");
    check(iterator->first.row == 2 && iterator->first.column == 1,
        "CellStore records should keep deterministic sparse ordering");

    store.set_cell(4, 1, fastxlsx::CellValue::blank());
    const fastxlsx::detail::CellRecord* missing = store.try_cell(4, 2);
    check(missing == nullptr, "CellStore try_cell should return null for missing cells");
    const fastxlsx::detail::CellRecord* blank = store.try_cell(4, 1);
    check(blank != nullptr, "CellStore should keep explicit blank records");
    check(blank->kind == fastxlsx::CellValueKind::Blank,
        "CellStore blank record kind mismatch");
    check(blank != missing, "CellStore explicit blank should differ from missing cells");
    check(!blank->to_value().to_cell().has_value(),
        "CellStore blank record should round trip without a Cell representation");
    check(store.cell_count() == 5, "CellStore blank record should count as an active record");

    store.erase_cell(4, 1);
    check(store.try_cell(4, 1) == nullptr, "CellStore erase should remove the sparse record");
    check(store.cell_count() == 4, "CellStore erase should reduce the active record count");

    store.set_cell(5, 1, fastxlsx::CellValue::text(std::string(128, 'x')));
    check(store.estimated_memory_usage() > empty_memory,
        "CellStore memory estimate should grow after inserting records");

    check_fastxlsx_error(
        [&store] { store.set_cell(0, 1, fastxlsx::CellValue::number(1.0)); },
        "CellStore should reject zero row coordinates");
    check_fastxlsx_error([&store] { (void)store.try_cell(1, 16385); },
        "CellStore should reject columns beyond Excel's limit");
    check_fastxlsx_error([&store] { store.erase_cell(1048577, 1); },
        "CellStore should reject rows beyond Excel's limit");
    check(store.cell_count() == 5,
        "CellStore coordinate validation failures should not change the sparse index");
    const fastxlsx::detail::CellRecord* preserved_after_invalid_coordinate =
        store.try_cell(5, 1);
    check(preserved_after_invalid_coordinate != nullptr,
        "CellStore coordinate validation failures should preserve existing records");
    check(preserved_after_invalid_coordinate->kind == fastxlsx::CellValueKind::Text,
        "CellStore coordinate validation failures should preserve existing record kind");
    check(preserved_after_invalid_coordinate->text_value == std::string(128, 'x'),
        "CellStore coordinate validation failures should preserve existing record payload");
}

void test_internal_cell_store_guardrails()
{
    fastxlsx::detail::CellStoreOptions max_cell_options;
    max_cell_options.max_cells = 1;
    fastxlsx::detail::CellStore max_cell_store(max_cell_options);

    max_cell_store.set_cell(1, 1, fastxlsx::CellValue::number(1.0));
    check(max_cell_store.cell_count() == 1,
        "CellStore max_cells setup should insert the first record");
    check_fastxlsx_error(
        [&max_cell_store] { max_cell_store.set_cell(1, 2, fastxlsx::CellValue::text("blocked")); },
        "CellStore should reject inserts beyond max_cells");
    check(max_cell_store.cell_count() == 1,
        "CellStore max_cells failure should not grow the sparse index");
    check(max_cell_store.find_cell(1, 2) == nullptr,
        "CellStore max_cells failure should not leave a rejected record");

    max_cell_store.set_cell(1, 1, fastxlsx::CellValue::boolean(true));
    const fastxlsx::detail::CellRecord* overwritten = max_cell_store.find_cell(1, 1);
    check(overwritten != nullptr, "CellStore max_cells overwrite should keep the record");
    check(overwritten->kind == fastxlsx::CellValueKind::Boolean,
        "CellStore max_cells should allow overwriting an existing record");
    check(overwritten->boolean_value, "CellStore max_cells overwrite payload mismatch");

    fastxlsx::detail::CellStore sizing_store;
    sizing_store.set_cell(1, 1, fastxlsx::CellValue::text("a"));
    fastxlsx::detail::CellStoreOptions memory_options;
    memory_options.memory_budget_bytes = sizing_store.estimated_memory_usage();
    fastxlsx::detail::CellStore memory_store(memory_options);

    memory_store.set_cell(1, 1, fastxlsx::CellValue::text("a"));
    check(memory_store.cell_count() == 1,
        "CellStore memory budget setup should insert the first record");
    check_fastxlsx_error(
        [&memory_store] {
            memory_store.set_cell(1, 1, fastxlsx::CellValue::text(std::string(1024, 'x')));
        },
        "CellStore should reject overwrites beyond memory_budget_bytes");
    const fastxlsx::detail::CellRecord* preserved = memory_store.find_cell(1, 1);
    check(preserved != nullptr,
        "CellStore memory budget failure should preserve the existing record");
    check(preserved->kind == fastxlsx::CellValueKind::Text,
        "CellStore memory budget failure should preserve the existing kind");
    check(preserved->text_value == "a",
        "CellStore memory budget failure should preserve the existing payload");

    fastxlsx::detail::CellStoreOptions batch_max_options;
    batch_max_options.max_cells = 2;
    fastxlsx::detail::CellStore batch_max_store(batch_max_options);
    batch_max_store.set_cell(1, 1, fastxlsx::CellValue::text("kept"));
    const fastxlsx::CellValue batch_valid_insert = fastxlsx::CellValue::text("valid");
    const fastxlsx::CellValue batch_blocked_insert = fastxlsx::CellValue::text("blocked");
    const std::vector<fastxlsx::detail::CellStoreUpdate> max_batch_updates = {
        {fastxlsx::detail::CellPosition {1, 2}, &batch_valid_insert},
        {fastxlsx::detail::CellPosition {1, 3}, &batch_blocked_insert},
    };
    check_fastxlsx_error(
        [&batch_max_store, &max_batch_updates] {
            batch_max_store.set_cells(max_batch_updates);
        },
        "CellStore batch set_cells should reject inserts beyond max_cells");
    check(batch_max_store.cell_count() == 1,
        "CellStore batch max_cells failure should preserve the sparse count");
    check(batch_max_store.find_cell(1, 2) == nullptr,
        "CellStore batch max_cells failure should not leave the first inserted record");
    check(batch_max_store.find_cell(1, 3) == nullptr,
        "CellStore batch max_cells failure should not leave the overflowing record");

    const fastxlsx::CellValue batch_memory_insert = fastxlsx::CellValue::text("insert");
    const fastxlsx::CellValue batch_memory_huge =
        fastxlsx::CellValue::text(std::string(1024, 'x'));
    const std::vector<fastxlsx::detail::CellStoreUpdate> memory_batch_updates = {
        {fastxlsx::detail::CellPosition {1, 2}, &batch_memory_insert},
        {fastxlsx::detail::CellPosition {1, 1}, &batch_memory_huge},
    };
    check_fastxlsx_error(
        [&memory_store, &memory_batch_updates] {
            memory_store.set_cells(memory_batch_updates);
        },
        "CellStore batch set_cells should reject memory_budget_bytes overflow");
    const fastxlsx::detail::CellRecord* batch_memory_preserved =
        memory_store.find_cell(1, 1);
    check(batch_memory_preserved != nullptr
            && batch_memory_preserved->text_value == "a",
        "CellStore batch memory failure should preserve the overwritten record");
    check(memory_store.find_cell(1, 2) == nullptr,
        "CellStore batch memory failure should not leave inserted records");

    fastxlsx::detail::CellStore style_batch_store;
    const fastxlsx::StyleId source_style = fastxlsx::detail::make_source_style_id(7);
    style_batch_store.set_cell(
        1, 1, fastxlsx::CellValue::number(1.0).with_style(source_style));
    const fastxlsx::CellValue preserve_first = fastxlsx::CellValue::text("first");
    const fastxlsx::CellValue preserve_last = fastxlsx::CellValue::text("last");
    const std::vector<fastxlsx::detail::CellStoreUpdate> preserve_batch_updates = {
        {fastxlsx::detail::CellPosition {1, 1}, &preserve_first},
        {fastxlsx::detail::CellPosition {1, 1}, &preserve_last},
    };
    style_batch_store.set_cells(preserve_batch_updates,
        fastxlsx::detail::CellStoreBatchStylePolicy::PreserveExistingStyles);
    const fastxlsx::detail::CellRecord* preserved_style_record =
        style_batch_store.find_cell(1, 1);
    check(preserved_style_record != nullptr
            && preserved_style_record->kind == fastxlsx::CellValueKind::Text
            && preserved_style_record->text_value == "last",
        "CellStore preserve-style batch should keep later-wins duplicate semantics");
    check(preserved_style_record->style_id.has_value()
            && preserved_style_record->style_id->value() == source_style.value(),
        "CellStore preserve-style batch should keep existing source style ids");
    const fastxlsx::CellValue replace_value = fastxlsx::CellValue::text("replace");
    const std::vector<fastxlsx::detail::CellStoreUpdate> replace_batch_updates = {
        {fastxlsx::detail::CellPosition {1, 1}, &replace_value},
    };
    style_batch_store.set_cells(replace_batch_updates);
    const fastxlsx::detail::CellRecord* replaced_style_record =
        style_batch_store.find_cell(1, 1);
    check(replaced_style_record != nullptr
            && replaced_style_record->kind == fastxlsx::CellValueKind::Text
            && replaced_style_record->text_value == "replace"
            && !replaced_style_record->style_id.has_value(),
        "CellStore replace batch should drop existing style ids");

    fastxlsx::detail::CellStoreOptions patch_max_options;
    patch_max_options.max_cells = 2;
    fastxlsx::detail::CellStore patch_store(patch_max_options);
    patch_store.set_cell(1, 1, fastxlsx::CellValue::text("erase-candidate"));
    patch_store.set_cell(1, 2, fastxlsx::CellValue::text("kept"));
    const fastxlsx::CellValue patch_insert_c1 =
        fastxlsx::CellValue::text("insert-c1");
    const fastxlsx::CellValue patch_insert_d1 =
        fastxlsx::CellValue::text("insert-d1");
    const std::vector<fastxlsx::detail::CellPosition> patch_erasures = {
        fastxlsx::detail::CellPosition {1, 1},
    };
    const std::vector<fastxlsx::detail::CellStoreUpdate> overflowing_patch_updates = {
        {fastxlsx::detail::CellPosition {1, 3}, &patch_insert_c1},
        {fastxlsx::detail::CellPosition {1, 4}, &patch_insert_d1},
    };
    check_fastxlsx_error(
        [&patch_store, &patch_erasures, &overflowing_patch_updates] {
            (void)patch_store.apply_cell_edits(
                patch_erasures, overflowing_patch_updates);
        },
        "CellStore apply_cell_edits should reject final sparse count overflow");
    check(patch_store.cell_count() == 2,
        "CellStore failed apply_cell_edits should preserve sparse count");
    const fastxlsx::detail::CellRecord* patch_preserved_a1 =
        patch_store.find_cell(1, 1);
    check(patch_preserved_a1 != nullptr
            && patch_preserved_a1->text_value == "erase-candidate",
        "CellStore failed apply_cell_edits should not erase preflighted records");
    check(patch_store.find_cell(1, 3) == nullptr
            && patch_store.find_cell(1, 4) == nullptr,
        "CellStore failed apply_cell_edits should not leak rejected inserts");

    const fastxlsx::CellValue patch_temporary_a1 =
        fastxlsx::CellValue::text("temporary-a1");
    const fastxlsx::CellValue patch_equal_a1 =
        fastxlsx::CellValue::text("erase-candidate");
    const fastxlsx::CellValue patch_equal_b1 = fastxlsx::CellValue::text("kept");
    const std::vector<fastxlsx::detail::CellPosition> equal_patch_erasures = {
        fastxlsx::detail::CellPosition {1, 1},
        fastxlsx::detail::CellPosition {9, 9},
    };
    const std::vector<fastxlsx::detail::CellStoreUpdate> equal_patch_updates = {
        {fastxlsx::detail::CellPosition {1, 1}, &patch_temporary_a1},
        {fastxlsx::detail::CellPosition {1, 1}, &patch_equal_a1},
        {fastxlsx::detail::CellPosition {1, 2}, &patch_equal_b1},
    };
    const bool equal_patch_changed =
        patch_store.apply_cell_edits(equal_patch_erasures, equal_patch_updates);
    check(!equal_patch_changed,
        "CellStore apply_cell_edits should report a final-state-equal batch as unchanged");
    check(patch_store.cell_count() == 2
            && patch_store.find_cell(1, 1)->text_value == "erase-candidate"
            && patch_store.find_cell(1, 2)->text_value == "kept",
        "CellStore final-state-equal batch should preserve active sparse records");

    const std::vector<fastxlsx::detail::CellStoreUpdate> valid_patch_updates = {
        {fastxlsx::detail::CellPosition {1, 3}, &patch_insert_c1},
    };
    const bool patch_changed =
        patch_store.apply_cell_edits(patch_erasures, valid_patch_updates);
    check(patch_changed,
        "CellStore apply_cell_edits should report effective sparse edits");
    check(patch_store.cell_count() == 2,
        "CellStore successful apply_cell_edits should apply final sparse count");
    check(patch_store.find_cell(1, 1) == nullptr,
        "CellStore successful apply_cell_edits should erase requested records");
    const fastxlsx::detail::CellRecord* patch_inserted_c1 =
        patch_store.find_cell(1, 3);
    check(patch_inserted_c1 != nullptr
            && patch_inserted_c1->text_value == "insert-c1",
        "CellStore successful apply_cell_edits should commit requested inserts");

    fastxlsx::detail::CellStoreOptions empty_budget_options;
    empty_budget_options.memory_budget_bytes = sizeof(fastxlsx::detail::CellStore);
    fastxlsx::detail::CellStore empty_budget_store(empty_budget_options);
    check_fastxlsx_error(
        [&empty_budget_store] {
            empty_budget_store.set_cell(1, 1, fastxlsx::CellValue::text("blocked"));
        },
        "CellStore should reject inserts beyond an empty memory budget");
    check(empty_budget_store.empty(),
        "CellStore memory budget insert failure should leave the store empty");
}

void test_internal_materialized_worksheet_session()
{
    fastxlsx::detail::CellStoreOptions options;
    options.max_cells = 2;

    fastxlsx::detail::CellStore source_store(options);
    source_store.set_cell(1, 1, fastxlsx::CellValue::number(1.0));
    source_store.set_cell(2, 1, fastxlsx::CellValue::blank());

    fastxlsx::detail::MaterializedWorksheetSession session(
        "Data", std::move(source_store));
    check(session.planned_name() == "Data",
        "materialized worksheet session should keep the planned sheet name");
    check(!session.dirty(), "loaded materialized worksheet session should start clean");
    check(session.options_match(options),
        "materialized worksheet session should preserve materialization options");
    check(session.cell_count() == 2,
        "materialized worksheet session should expose source-loaded cell count");

    session.set_cell(1, 1, fastxlsx::CellValue::number(1.0));
    check(!session.dirty(),
        "final-state-equal materialized set_cell should remain a clean no-op");
    session.clear_cell_value(2, 1);
    check(!session.dirty(),
        "clearing an explicit blank materialized cell should remain a clean no-op");

    const fastxlsx::CellValue equal_number = fastxlsx::CellValue::number(1.0);
    const std::vector<fastxlsx::detail::CellStoreUpdate> equal_updates = {
        {fastxlsx::detail::CellPosition {1, 1}, &equal_number},
    };
    session.set_cells(equal_updates);
    check(!session.dirty(),
        "final-state-equal materialized batch should remain a clean no-op");

    session.erase_cell(3, 1);
    check(!session.dirty(),
        "erasing a missing materialized cell should remain a clean no-op");
    check(session.cell_count() == 2,
        "erasing a missing materialized cell should not change cell count");

    session.erase_cell(2, 1);
    check(session.dirty(),
        "erasing an existing materialized cell should mark the session dirty");
    check(session.try_cell(2, 1) == nullptr,
        "erasing an existing materialized cell should remove the sparse record");
    check(session.cell_count() == 1,
        "erasing an existing materialized cell should reduce cell count");

    session.clear_dirty();
    check(!session.dirty(), "clear_dirty should reset the internal dirty marker");
    session.set_cell(1, 2, fastxlsx::CellValue::text("new"));
    check(session.dirty(),
        "setting a materialized cell should mark the session dirty after success");
    check(session.cell_count() == 2,
        "setting a new materialized cell within max_cells should grow the store");
    auto session_projection = session.worksheet_chunk_source();
    std::string session_projection_xml;
    std::string session_projection_chunk;
    while (session_projection(session_projection_chunk)) {
        session_projection_xml += session_projection_chunk;
    }
    check(session_projection_xml.find(R"(<dimension ref="A1:B1"/>)") != std::string::npos,
        "materialized worksheet session projection should refresh dimension from dirty store");
    check(session_projection_xml.find(R"(<c r="B1" t="inlineStr"><is><t>new</t></is></c>)")
            != std::string::npos,
        "materialized worksheet session projection should include dirty set_cell payload");
    check(session_projection_xml.find(R"(<c r="A2")") == std::string::npos,
        "materialized worksheet session projection should omit erased sparse records");

    session.clear_dirty();
    check_fastxlsx_error(
        [&session] { session.set_cell(1, 3, fastxlsx::CellValue::text("blocked")); },
        "materialized worksheet session should propagate CellStore guardrail failures");
    check(!session.dirty(),
        "failed materialized set_cell should not mark the session dirty");
    check(session.cell_count() == 2,
        "failed materialized set_cell should not change the sparse store");
    check(session.try_cell(1, 3) == nullptr,
        "failed materialized set_cell should not leave a rejected record");

    check_fastxlsx_error(
        [&session] { session.erase_cell(0, 1); },
        "materialized worksheet session should propagate coordinate validation failures");
    check(!session.dirty(),
        "failed materialized erase_cell should not mark the session dirty");

    fastxlsx::detail::CellStoreOptions mismatched_options;
    mismatched_options.max_cells = 3;
    check(!session.options_match(mismatched_options),
        "materialized worksheet session should detect mismatched rematerialization options");

    fastxlsx::detail::CellStore formula_store;
    formula_store.set_cell(1, 1, fastxlsx::CellValue::formula("A1+1"));
    fastxlsx::detail::MaterializedWorksheetSession formula_session(
        "Formula", std::move(formula_store));
    formula_session.replace_formula_text(1, 1, "A1+1");
    check(!formula_session.dirty(),
        "final-state-equal materialized formula rewrite should remain clean");
    formula_session.replace_formula_text(1, 1, "B1+1");
    check(formula_session.dirty(),
        "effective materialized formula rewrite should mark the session dirty");
}

void test_internal_materialized_worksheet_session_shifts_sparse_records()
{
    const fastxlsx::StyleId formula_style = fastxlsx::detail::make_source_style_id(9);
    const auto collect_worksheet_xml =
        [](fastxlsx::detail::MaterializedWorksheetSession& session) {
            auto projection = session.worksheet_chunk_source();
            std::string xml;
            std::string chunk;
            while (projection(chunk)) {
                xml += chunk;
            }
            return xml;
        };
    const auto check_formula_record =
        [formula_style](const fastxlsx::detail::CellRecord* record,
            std::string_view text,
            const char* message) {
            check(record != nullptr, message);
            check(record->kind == fastxlsx::CellValueKind::Formula, message);
            check(record->text_value == text, message);
            check(record->style_id.has_value()
                    && record->style_id->value() == formula_style.value(),
                message);
        };
    const auto make_store = [formula_style](
        std::uint32_t stationary_row,
        std::uint32_t stationary_column,
        std::string_view stationary_formula,
        std::uint32_t moving_row,
        std::uint32_t moving_column,
        std::string_view moving_formula,
        std::uint32_t tail_row,
        std::uint32_t tail_column,
        std::string_view tail_text) {
        fastxlsx::detail::CellStore store;
        store.set_cell(stationary_row, stationary_column,
            fastxlsx::CellValue::formula(std::string(stationary_formula))
                .with_style(formula_style));
        store.set_cell(moving_row, moving_column,
            fastxlsx::CellValue::formula(std::string(moving_formula))
                .with_style(formula_style));
        store.set_cell(tail_row, tail_column,
            fastxlsx::CellValue::text(std::string(tail_text)));
        return store;
    };

    {
        fastxlsx::detail::MaterializedWorksheetSession session(
            "Data",
            make_store(1, 1, "A2+B1", 2, 2, "A2+B1", 3, 1, "tail-a3"));
        session.insert_rows(2, 1);

        check(session.dirty(), "materialized insert_rows should dirty shifted records");
        check(session.cell_count() == 3,
            "materialized insert_rows should preserve sparse record count");
        check_formula_record(session.try_cell(1, 1), "A3+B1",
            "materialized insert_rows should rewrite stationary formula references");
        check_formula_record(session.try_cell(3, 2), "A3+B1",
            "materialized insert_rows should structurally rewrite moved formula references");
        check(session.try_cell(2, 2) == nullptr,
            "materialized insert_rows should remove old moved formula coordinate");
        const fastxlsx::detail::CellRecord* tail = session.try_cell(4, 1);
        check(tail != nullptr && tail->kind == fastxlsx::CellValueKind::Text &&
                tail->text_value == "tail-a3",
            "materialized insert_rows should shift trailing text records");
        const std::string xml = collect_worksheet_xml(session);
        check(xml.find(R"(<dimension ref="A1:B4"/>)") != std::string::npos,
            "materialized insert_rows projection should expose shifted bounds");
        check(xml.find(R"(<c r="A1" s="9"><f>A3+B1</f></c>)") != std::string::npos,
            "materialized insert_rows projection should write stationary rewritten formula");
        check(xml.find(R"(<c r="B3" s="9"><f>A3+B1</f></c>)") != std::string::npos,
            "materialized insert_rows projection should write moved structurally rewritten formula");
        check(xml.find(R"(<c r="A4" t="inlineStr"><is><t>tail-a3</t></is></c>)")
                != std::string::npos,
            "materialized insert_rows projection should write shifted text");
    }

    {
        fastxlsx::detail::MaterializedWorksheetSession session(
            "Data",
            make_store(1, 1, "A2+A3", 3, 2, "A3+B4", 4, 1, "tail-a4"));
        session.delete_rows(2, 1);

        check(session.dirty(), "materialized delete_rows should dirty shifted records");
        check(session.cell_count() == 3,
            "materialized delete_rows should preserve surviving sparse record count");
        check_formula_record(session.try_cell(1, 1), "#REF!+A2",
            "materialized delete_rows should rewrite stationary formula references");
        check_formula_record(session.try_cell(2, 2), "A2+B3",
            "materialized delete_rows should translate moved formula references");
        check(session.try_cell(3, 2) == nullptr,
            "materialized delete_rows should remove old moved formula coordinate");
        const fastxlsx::detail::CellRecord* tail = session.try_cell(3, 1);
        check(tail != nullptr && tail->text_value == "tail-a4",
            "materialized delete_rows should shift trailing text records");
        const std::string xml = collect_worksheet_xml(session);
        check(xml.find(R"(<dimension ref="A1:B3"/>)") != std::string::npos,
            "materialized delete_rows projection should expose shifted bounds");
        check(xml.find(R"(<c r="A1" s="9"><f>#REF!+A2</f></c>)") != std::string::npos,
            "materialized delete_rows projection should write stationary rewritten formula");
        check(xml.find(R"(<c r="B2" s="9"><f>A2+B3</f></c>)") != std::string::npos,
            "materialized delete_rows projection should write moved translated formula");
    }

    {
        fastxlsx::detail::MaterializedWorksheetSession session(
            "Data",
            make_store(1, 1, "B1+C1", 1, 2, "B1+C1", 2, 3, "tail-c2"));
        session.insert_columns(2, 1);

        check(session.dirty(), "materialized insert_columns should dirty shifted records");
        check(session.cell_count() == 3,
            "materialized insert_columns should preserve sparse record count");
        check_formula_record(session.try_cell(1, 1), "C1+D1",
            "materialized insert_columns should rewrite stationary formula references");
        check_formula_record(session.try_cell(1, 3), "C1+D1",
            "materialized insert_columns should translate moved formula references");
        check(session.try_cell(1, 2) == nullptr,
            "materialized insert_columns should remove old moved formula coordinate");
        const fastxlsx::detail::CellRecord* tail = session.try_cell(2, 4);
        check(tail != nullptr && tail->text_value == "tail-c2",
            "materialized insert_columns should shift trailing text records");
        const std::string xml = collect_worksheet_xml(session);
        check(xml.find(R"(<dimension ref="A1:D2"/>)") != std::string::npos,
            "materialized insert_columns projection should expose shifted bounds");
        check(xml.find(R"(<c r="A1" s="9"><f>C1+D1</f></c>)") != std::string::npos,
            "materialized insert_columns projection should write stationary rewritten formula");
        check(xml.find(R"(<c r="C1" s="9"><f>C1+D1</f></c>)") != std::string::npos,
            "materialized insert_columns projection should write moved translated formula");
    }

    {
        fastxlsx::detail::MaterializedWorksheetSession session(
            "Data",
            make_store(1, 1, "B1+C1", 1, 3, "B1+C1", 2, 4, "tail-d2"));
        session.delete_columns(2, 1);

        check(session.dirty(), "materialized delete_columns should dirty shifted records");
        check(session.cell_count() == 3,
            "materialized delete_columns should preserve surviving sparse record count");
        check_formula_record(session.try_cell(1, 1), "#REF!+B1",
            "materialized delete_columns should rewrite stationary formula references");
        check_formula_record(session.try_cell(1, 2), "#REF!+B1",
            "materialized delete_columns should structurally rewrite moved formula references");
        check(session.try_cell(1, 3) == nullptr,
            "materialized delete_columns should remove old moved formula coordinate");
        const fastxlsx::detail::CellRecord* tail = session.try_cell(2, 3);
        check(tail != nullptr && tail->text_value == "tail-d2",
            "materialized delete_columns should shift trailing text records");
        const std::string xml = collect_worksheet_xml(session);
        check(xml.find(R"(<dimension ref="A1:C2"/>)") != std::string::npos,
            "materialized delete_columns projection should expose shifted bounds");
        check(xml.find(R"(<c r="A1" s="9"><f>#REF!+B1</f></c>)") != std::string::npos,
            "materialized delete_columns projection should write stationary rewritten formula");
        check(xml.find(R"(<c r="B1" s="9"><f>#REF!+B1</f></c>)") != std::string::npos,
            "materialized delete_columns projection should write moved structurally rewritten formula");
    }
}

void test_internal_materialized_worksheet_session_shift_noops_preserve_state()
{
    const fastxlsx::StyleId formula_style = fastxlsx::detail::make_source_style_id(10);
    const auto collect_worksheet_xml =
        [](fastxlsx::detail::MaterializedWorksheetSession& session) {
            auto projection = session.worksheet_chunk_source();
            std::string xml;
            std::string chunk;
            while (projection(chunk)) {
                xml += chunk;
            }
            return xml;
        };
    const auto check_formula_record =
        [formula_style](const fastxlsx::detail::CellRecord* record,
            std::string_view text,
            const char* message) {
            check(record != nullptr, message);
            check(record->kind == fastxlsx::CellValueKind::Formula, message);
            check(record->text_value == text, message);
            check(record->style_id.has_value()
                    && record->style_id->value() == formula_style.value(),
                message);
        };
    const auto check_text_record =
        [](const fastxlsx::detail::CellRecord* record,
            std::string_view text,
            const char* message) {
            check(record != nullptr, message);
            check(record->kind == fastxlsx::CellValueKind::Text, message);
            check(record->text_value == text, message);
        };

    {
        fastxlsx::detail::CellStore store;
        store.set_cell(1, 1,
            fastxlsx::CellValue::formula("A1+B1").with_style(formula_style));
        store.set_cell(2, 2, fastxlsx::CellValue::text("tail-b2"));

        fastxlsx::detail::MaterializedWorksheetSession session(
            "CleanNoop", std::move(store));
        const std::size_t memory_before = session.estimated_memory_usage();

        session.insert_rows(3, 2);
        session.delete_rows(3, 2);
        session.insert_columns(3, 2);
        session.delete_columns(3, 2);

        check(!session.dirty(),
            "non-intersecting materialized shifts should keep a clean session clean");
        check(session.cell_count() == 2,
            "non-intersecting materialized shifts should preserve sparse count");
        check(session.estimated_memory_usage() == memory_before,
            "non-intersecting materialized shifts should preserve sparse memory estimate");
        check(session.dimension_reference() == "A1:B2",
            "non-intersecting materialized shifts should preserve dimension");
        check_formula_record(session.try_cell(1, 1), "A1+B1",
            "non-intersecting materialized shifts should preserve formula text and style");
        check_text_record(session.try_cell(2, 2), "tail-b2",
            "non-intersecting materialized shifts should preserve text records");

        const std::string xml = collect_worksheet_xml(session);
        check(xml.find(R"(<dimension ref="A1:B2"/>)") != std::string::npos,
            "non-intersecting materialized shifts projection should preserve dimension");
        check(xml.find(R"(<c r="A1" s="10"><f>A1+B1</f></c>)") != std::string::npos,
            "non-intersecting materialized shifts projection should preserve formula XML");
        check(xml.find(R"(<c r="B2" t="inlineStr"><is><t>tail-b2</t></is></c>)")
                != std::string::npos,
            "non-intersecting materialized shifts projection should preserve text XML");
    }

    {
        fastxlsx::detail::CellStore store;
        store.set_cell(1, 1, fastxlsx::CellValue::number(1.0));
        fastxlsx::detail::MaterializedWorksheetSession session(
            "CleanZeroCount", std::move(store));

        session.insert_rows(1, 0);
        session.delete_rows(1, 0);
        session.insert_columns(1, 0);
        session.delete_columns(1, 0);

        check(!session.dirty(),
            "zero-count materialized shifts should keep a clean session clean");
        check(session.cell_count() == 1,
            "zero-count materialized shifts should preserve clean sparse count");
        const fastxlsx::detail::CellRecord* number = session.try_cell(1, 1);
        check(number != nullptr && number->kind == fastxlsx::CellValueKind::Number
                && number->number_value == 1.0,
            "zero-count materialized shifts should preserve clean records");
    }

    {
        fastxlsx::detail::CellStore store;
        store.set_cell(1, 1, fastxlsx::CellValue::number(1.0));
        fastxlsx::detail::MaterializedWorksheetSession session(
            "DirtyZeroCount", std::move(store));
        session.set_cell(2, 2, fastxlsx::CellValue::text("dirty-b2"));

        session.insert_rows(1, 0);
        session.delete_rows(1, 0);
        session.insert_columns(1, 0);
        session.delete_columns(1, 0);

        check(session.dirty(),
            "zero-count materialized shifts should preserve pre-existing dirty state");
        check(session.cell_count() == 2,
            "zero-count materialized shifts should preserve dirty sparse count");
        const fastxlsx::detail::CellRecord* number = session.try_cell(1, 1);
        check(number != nullptr && number->kind == fastxlsx::CellValueKind::Number
                && number->number_value == 1.0,
            "zero-count materialized shifts should preserve source records");
        check_text_record(session.try_cell(2, 2), "dirty-b2",
            "zero-count materialized shifts should preserve dirty records");
    }

    {
        fastxlsx::detail::MaterializedWorksheetSession session(
            "Empty", fastxlsx::detail::CellStore {});

        session.insert_rows(1, 1);
        session.delete_rows(1, 1);
        session.insert_columns(1, 1);
        session.delete_columns(1, 1);

        check(!session.dirty(),
            "materialized shifts on an empty store should keep the session clean");
        check(session.cell_count() == 0,
            "materialized shifts on an empty store should keep the sparse count empty");
        check(session.dimension_reference() == "A1",
            "materialized shifts on an empty store should keep the empty dimension");
    }
}

void test_internal_materialized_worksheet_session_shift_failures_preserve_state()
{
    const fastxlsx::StyleId formula_style = fastxlsx::detail::make_source_style_id(12);
    const auto check_formula_record =
        [formula_style](const fastxlsx::detail::CellRecord* record,
            std::string_view text,
            const char* message) {
            check(record != nullptr, message);
            check(record->kind == fastxlsx::CellValueKind::Formula, message);
            check(record->text_value == text, message);
            check(record->style_id.has_value()
                    && record->style_id->value() == formula_style.value(),
                message);
        };
    const auto check_text_record =
        [](const fastxlsx::detail::CellRecord* record,
            std::string_view text,
            const char* message) {
            check(record != nullptr, message);
            check(record->kind == fastxlsx::CellValueKind::Text, message);
            check(record->text_value == text, message);
        };

    {
        fastxlsx::detail::CellStore store;
        store.set_cell(1, 1,
            fastxlsx::CellValue::formula("A2+A1048576").with_style(formula_style));
        store.set_cell(1048576, 1, fastxlsx::CellValue::text("edge-row"));

        fastxlsx::detail::MaterializedWorksheetSession session(
            "Rows", std::move(store));
        check_fastxlsx_error(
            [&session] { session.insert_rows(2, 1); },
            "materialized insert_rows should reject shifting records past the row limit");
        check(!session.dirty(),
            "failed materialized insert_rows should leave a clean session clean");
        check(session.cell_count() == 2,
            "failed materialized insert_rows should preserve sparse record count");
        check_formula_record(session.try_cell(1, 1), "A2+A1048576",
            "failed materialized insert_rows should preserve stationary formula text");
        check_text_record(session.try_cell(1048576, 1), "edge-row",
            "failed materialized insert_rows should preserve the row-limit record");
        check(session.dimension_reference() == "A1:A1048576",
            "failed materialized insert_rows should preserve the sparse dimension");
    }

    {
        fastxlsx::detail::CellStore store;
        store.set_cell(1, 1,
            fastxlsx::CellValue::formula("B1+XFD1").with_style(formula_style));
        store.set_cell(1, 16384, fastxlsx::CellValue::text("edge-column"));

        fastxlsx::detail::MaterializedWorksheetSession session(
            "Columns", std::move(store));
        check_fastxlsx_error(
            [&session] { session.insert_columns(2, 1); },
            "materialized insert_columns should reject shifting records past the column limit");
        check(!session.dirty(),
            "failed materialized insert_columns should leave a clean session clean");
        check(session.cell_count() == 2,
            "failed materialized insert_columns should preserve sparse record count");
        check_formula_record(session.try_cell(1, 1), "B1+XFD1",
            "failed materialized insert_columns should preserve stationary formula text");
        check_text_record(session.try_cell(1, 16384), "edge-column",
            "failed materialized insert_columns should preserve the column-limit record");
        check(session.dimension_reference() == "A1:XFD1",
            "failed materialized insert_columns should preserve the sparse dimension");
    }

    {
        fastxlsx::detail::CellStore store;
        store.set_cell(1, 1, fastxlsx::CellValue::number(1.0));
        fastxlsx::detail::MaterializedWorksheetSession session(
            "DirtyRows", std::move(store));
        session.set_cell(2, 1, fastxlsx::CellValue::text("dirty-row"));

        check_fastxlsx_error(
            [&session] { session.delete_rows(2, 1048576); },
            "materialized delete_rows should reject row spans beyond the sheet limit");
        check(session.dirty(),
            "failed materialized delete_rows should preserve pre-existing dirty state");
        check(session.cell_count() == 2,
            "failed materialized delete_rows should preserve dirty sparse count");
        const fastxlsx::detail::CellRecord* number = session.try_cell(1, 1);
        check(number != nullptr && number->kind == fastxlsx::CellValueKind::Number
                && number->number_value == 1.0,
            "failed materialized delete_rows should preserve source records");
        check_text_record(session.try_cell(2, 1), "dirty-row",
            "failed materialized delete_rows should preserve dirty records");
    }

    {
        fastxlsx::detail::CellStore store;
        store.set_cell(1, 1, fastxlsx::CellValue::number(1.0));
        fastxlsx::detail::MaterializedWorksheetSession session(
            "DirtyColumns", std::move(store));
        session.set_cell(1, 2, fastxlsx::CellValue::text("dirty-column"));

        check_fastxlsx_error(
            [&session] { session.delete_columns(2, 16384); },
            "materialized delete_columns should reject column spans beyond the sheet limit");
        check(session.dirty(),
            "failed materialized delete_columns should preserve pre-existing dirty state");
        check(session.cell_count() == 2,
            "failed materialized delete_columns should preserve dirty sparse count");
        const fastxlsx::detail::CellRecord* number = session.try_cell(1, 1);
        check(number != nullptr && number->kind == fastxlsx::CellValueKind::Number
                && number->number_value == 1.0,
            "failed materialized delete_columns should preserve source records");
        check_text_record(session.try_cell(1, 2), "dirty-column",
            "failed materialized delete_columns should preserve dirty records");
    }
}

void test_internal_materialized_worksheet_session_registry()
{
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    check(registry.empty(), "materialized worksheet session registry should start empty");
    check(registry.session_count() == 0,
        "materialized worksheet session registry should expose empty count");
    check(registry.try_session("Data") == nullptr,
        "materialized worksheet session registry should return nullptr for missing sheet");

    fastxlsx::detail::CellStoreOptions options;
    options.max_cells = 3;
    registry.preflight_materialization("Data", options);
    check(registry.session_count() == 0,
        "materialized worksheet session preflight should not mutate registry state");

    fastxlsx::detail::CellStore data_store(options);
    data_store.set_cell(1, 1, fastxlsx::CellValue::number(10.0));
    auto& data_session = registry.materialize("Data", std::move(data_store));
    check(!registry.empty(), "materialized worksheet session registry should become non-empty");
    check(registry.session_count() == 1,
        "materialized worksheet session registry should count inserted sessions");
    check(registry.contains("Data"),
        "materialized worksheet session registry should find inserted sheet name");
    check(registry.try_session("Data") == &data_session,
        "materialized worksheet session registry should return the inserted session");
    check(data_session.planned_name() == "Data",
        "materialized worksheet session registry should preserve planned sheet name");
    check(data_session.cell_count() == 1,
        "materialized worksheet session registry should preserve source-loaded store");
    check(registry.dirty_session_count() == 0,
        "newly materialized worksheet sessions should start clean");

    data_session.set_cell(1, 2, fastxlsx::CellValue::text("dirty"));
    check(registry.dirty_session_count() == 1,
        "dirty materialized worksheet session should be counted by registry");
    check(registry.dirty_cell_count() == data_session.cell_count(),
        "dirty materialized worksheet session registry should aggregate dirty cells");
    check(registry.estimated_dirty_memory_usage() == data_session.estimated_memory_usage(),
        "dirty materialized worksheet session registry should aggregate dirty memory usage");
    const std::vector<std::string_view> dirty_names = registry.dirty_session_names();
    check(dirty_names.size() == 1 && dirty_names.front() == "Data",
        "dirty materialized worksheet session registry should expose dirty planned names");

    fastxlsx::detail::CellStore repeated_store(options);
    repeated_store.set_cell(3, 3, fastxlsx::CellValue::text("ignored"));
    auto& repeated_session = registry.materialize("Data", std::move(repeated_store));
    check(&repeated_session == &data_session,
        "repeated matching materialization should reuse the existing session");
    check(data_session.cell_count() == 2,
        "repeated matching materialization should not replace dirty session state");
    check(data_session.try_cell(3, 3) == nullptr,
        "repeated matching materialization should not import a replacement store");
    check(registry.dirty_session_count() == 1,
        "repeated matching materialization should preserve dirty bookkeeping");
    check(registry.dirty_cell_count() == data_session.cell_count(),
        "repeated matching materialization should preserve dirty cell bookkeeping");
    check(registry.estimated_dirty_memory_usage() == data_session.estimated_memory_usage(),
        "repeated matching materialization should preserve dirty memory bookkeeping");

    fastxlsx::detail::CellStoreOptions mismatched_options;
    mismatched_options.max_cells = 4;
    check_fastxlsx_error(
        [&registry, &mismatched_options] {
            registry.preflight_materialization("Data", mismatched_options);
        },
        "materialized worksheet session registry should reject mismatched options");
    check(registry.session_count() == 1,
        "mismatched materialization preflight should not insert a new session");
    check(registry.try_session("Data") == &data_session,
        "mismatched materialization preflight should preserve the existing session");

    fastxlsx::detail::CellStore mismatched_store(mismatched_options);
    check_fastxlsx_error(
        [&registry, &mismatched_store] {
            registry.materialize("Data", std::move(mismatched_store));
        },
        "materialized worksheet session registry should reject mismatched repeated materialization");
    check(registry.session_count() == 1,
        "failed mismatched materialization should not mutate registry count");
    check(registry.try_session("Data") == &data_session,
        "failed mismatched materialization should not replace the existing session");
    check(data_session.dirty(),
        "failed mismatched materialization should preserve dirty session state");

    registry.preflight_no_materialized_session("Missing", "replace sheet data");
    check(registry.session_count() == 1,
        "operation-mixing preflight for a non-materialized sheet should not mutate registry");
    check_fastxlsx_error(
        [&registry] {
            registry.preflight_no_materialized_session("Data", "replace sheet data");
        },
        "operation-mixing preflight should reject edits after materialization");
    check(registry.session_count() == 1,
        "failed operation-mixing preflight should preserve registry count");
    check(registry.try_session("Data") == &data_session,
        "failed operation-mixing preflight should preserve the materialized session");
    check(data_session.dirty(),
        "failed operation-mixing preflight should preserve dirty session state");

    fastxlsx::detail::CellStore other_store(options);
    auto& other_session = registry.materialize("Other", std::move(other_store));
    check(registry.session_count() == 2,
        "materialized worksheet session registry should track multiple sheets");
    check(registry.try_session("Other") == &other_session,
        "materialized worksheet session registry should find a second sheet");
    check(other_session.planned_name() == "Other",
        "second materialized worksheet session should keep its planned sheet name");
    check(registry.dirty_session_count() == 1,
        "clean second materialized session should not change dirty count");
    check(registry.dirty_cell_count() == data_session.cell_count(),
        "clean second materialized session should not change dirty cell bookkeeping");
    check(registry.estimated_dirty_memory_usage() == data_session.estimated_memory_usage(),
        "clean second materialized session should not change dirty memory bookkeeping");

    fastxlsx::detail::CellStore alpha_store(options);
    auto& alpha_session = registry.materialize("Alpha", std::move(alpha_store));
    alpha_session.set_cell(2, 1, fastxlsx::CellValue::boolean(true));
    check(registry.dirty_session_count() == 2,
        "second dirty materialized session should update dirty session count");
    check(registry.dirty_cell_count()
            == data_session.cell_count() + alpha_session.cell_count(),
        "second dirty materialized session should update dirty cell count");
    check(registry.estimated_dirty_memory_usage()
            == data_session.estimated_memory_usage()
                + alpha_session.estimated_memory_usage(),
        "second dirty materialized session should update dirty memory usage");
    const std::vector<std::string_view> ordered_dirty_names = registry.dirty_session_names();
    check(ordered_dirty_names.size() == 2 && ordered_dirty_names[0] == "Alpha"
            && ordered_dirty_names[1] == "Data",
        "dirty materialized worksheet session names should follow registry planned-name order");

    const std::vector<fastxlsx::detail::MaterializedWorksheetProjection> projections =
        registry.dirty_worksheet_chunk_sources();
    check(projections.size() == 2,
        "dirty worksheet projections should include only dirty materialized sessions");
    check(projections[0].planned_name == "Alpha" && projections[1].planned_name == "Data",
        "dirty worksheet projections should follow registry planned-name order");

    std::string alpha_projection_xml;
    std::string projection_chunk;
    while (projections[0].read_next_chunk(projection_chunk)) {
        alpha_projection_xml += projection_chunk;
    }
    check(alpha_projection_xml.find(R"(<dimension ref="A2"/>)") != std::string::npos,
        "dirty worksheet projection should refresh dimension for each dirty session");
    check(alpha_projection_xml.find(R"(<c r="A2" t="b"><v>1</v></c>)")
            != std::string::npos,
        "dirty worksheet projection should emit each dirty session's sparse payload");

    std::string data_projection_xml;
    while (projections[1].read_next_chunk(projection_chunk)) {
        data_projection_xml += projection_chunk;
    }
    check(data_projection_xml.find(R"(<c r="B1" t="inlineStr"><is><t>dirty</t></is></c>)")
            != std::string::npos,
        "dirty worksheet projection should preserve prior dirty session payload");
    check(data_projection_xml.find("Other") == std::string::npos,
        "dirty worksheet projections should not include clean session data");

    const std::vector<fastxlsx::detail::MaterializedWorksheetSheetDataProjection>
        sheet_data_projections = registry.dirty_sheet_data_chunk_sources();
    check(sheet_data_projections.size() == 2,
        "dirty sheetData projections should include only dirty materialized sessions");
    check(sheet_data_projections[0].planned_name == "Alpha"
            && sheet_data_projections[1].planned_name == "Data",
        "dirty sheetData projections should follow registry planned-name order");
    check(sheet_data_projections[0].dimension_reference == "A2",
        "dirty sheetData projections should carry each dirty session dimension");
    check(sheet_data_projections[1].dimension_reference == "A1:B1",
        "dirty sheetData projections should carry dimensions for mixed source and dirty cells");

    std::string alpha_sheet_data_xml;
    projection_chunk.clear();
    while (sheet_data_projections[0].read_next_chunk(projection_chunk)) {
        alpha_sheet_data_xml += projection_chunk;
    }
    check(alpha_sheet_data_xml.find("<worksheet") == std::string::npos,
        "dirty sheetData projection should not emit a full worksheet wrapper");
    check(alpha_sheet_data_xml.find(R"(<sheetData>)") != std::string::npos,
        "dirty sheetData projection should emit standalone sheetData boundaries");
    check(alpha_sheet_data_xml.find(R"(<c r="A2" t="b"><v>1</v></c>)")
            != std::string::npos,
        "dirty sheetData projection should emit the dirty sparse payload");

    std::string data_sheet_data_xml;
    projection_chunk.clear();
    while (sheet_data_projections[1].read_next_chunk(projection_chunk)) {
        data_sheet_data_xml += projection_chunk;
    }
    check(data_sheet_data_xml.find("<worksheet") == std::string::npos,
        "dirty sheetData projection should omit worksheet wrapper for mixed stores");
    check(data_sheet_data_xml.find(R"(<c r="A1"><v>10</v></c>)") != std::string::npos,
        "dirty sheetData projection should preserve source sparse payload");
    check(data_sheet_data_xml.find(R"(<c r="B1" t="inlineStr"><is><t>dirty</t></is></c>)")
            != std::string::npos,
        "dirty sheetData projection should preserve dirty sparse payload");
    check(data_sheet_data_xml.find("Other") == std::string::npos,
        "dirty sheetData projections should not include clean session data");

    auto shared_string_index_provider =
        std::make_shared<fastxlsx::detail::CellStoreSharedStringIndexProvider>(
            [](std::string_view text) -> std::uint32_t {
                return text == "dirty" ? 7U : 99U;
            });

    const std::vector<fastxlsx::detail::MaterializedWorksheetProjection>
        shared_string_projections =
            registry.dirty_worksheet_chunk_sources(shared_string_index_provider);
    check(shared_string_projections.size() == 2,
        "dirty worksheet shared-string projections should include dirty sessions");
    std::string shared_string_data_projection_xml;
    projection_chunk.clear();
    while (shared_string_projections[1].read_next_chunk(projection_chunk)) {
        shared_string_data_projection_xml += projection_chunk;
    }
    check(shared_string_data_projection_xml.find(
              R"(<c r="B1" t="s"><v>7</v></c>)")
            != std::string::npos,
        "dirty worksheet shared-string projection should use the supplied provider");
    check(shared_string_data_projection_xml.find("inlineStr") == std::string::npos,
        "dirty worksheet shared-string projection should not emit inline text cells");

    const std::vector<fastxlsx::detail::MaterializedWorksheetSheetDataProjection>
        shared_string_sheet_data_projections =
            registry.dirty_sheet_data_chunk_sources(shared_string_index_provider);
    check(shared_string_sheet_data_projections.size() == 2,
        "dirty sheetData shared-string projections should include dirty sessions");
    std::string shared_string_data_sheet_data_xml;
    projection_chunk.clear();
    while (shared_string_sheet_data_projections[1].read_next_chunk(projection_chunk)) {
        shared_string_data_sheet_data_xml += projection_chunk;
    }
    check(shared_string_data_sheet_data_xml.find(
              R"(<c r="B1" t="s"><v>7</v></c>)")
            != std::string::npos,
        "dirty sheetData shared-string projection should use the supplied provider");
    check(shared_string_data_sheet_data_xml.find("inlineStr") == std::string::npos,
        "dirty sheetData shared-string projection should not emit inline text cells");

    auto drain_projection_xml = [](const auto& projection) {
        std::string xml;
        std::string chunk;
        while (projection.read_next_chunk(chunk)) {
            xml += chunk;
        }
        return xml;
    };

    auto fail_if_called_provider =
        std::make_shared<fastxlsx::detail::CellStoreSharedStringIndexProvider>(
            [](std::string_view) -> std::uint32_t {
                throw fastxlsx::FastXlsxError("shared string provider should not be called");
            });

    const std::vector<fastxlsx::detail::MaterializedWorksheetProjection>
        value_only_worksheet_projections =
            registry.dirty_worksheet_chunk_sources(fail_if_called_provider);
    check(value_only_worksheet_projections.size() == 2,
        "dirty worksheet value-only shared-string projections should include dirty sessions");
    const std::string value_only_worksheet_xml =
        drain_projection_xml(value_only_worksheet_projections[0]);
    check(value_only_worksheet_xml.find(R"(<c r="A2" t="b"><v>1</v></c>)")
            != std::string::npos,
        "dirty worksheet value-only shared-string projection should not call provider");

    const std::vector<fastxlsx::detail::MaterializedWorksheetSheetDataProjection>
        value_only_sheet_data_projections =
            registry.dirty_sheet_data_chunk_sources(fail_if_called_provider);
    check(value_only_sheet_data_projections.size() == 2,
        "dirty sheetData value-only shared-string projections should include dirty sessions");
    const std::string value_only_sheet_data_xml =
        drain_projection_xml(value_only_sheet_data_projections[0]);
    check(value_only_sheet_data_xml.find(R"(<c r="A2" t="b"><v>1</v></c>)")
            != std::string::npos,
        "dirty sheetData value-only shared-string projection should not call provider");

    auto throwing_shared_string_index_provider =
        std::make_shared<fastxlsx::detail::CellStoreSharedStringIndexProvider>(
            [](std::string_view text) -> std::uint32_t {
                if (text == "dirty") {
                    throw fastxlsx::FastXlsxError(
                        "intentional shared string provider failure");
                }
                return 99U;
            });

    const std::vector<fastxlsx::detail::MaterializedWorksheetProjection>
        failing_worksheet_projections =
            registry.dirty_worksheet_chunk_sources(throwing_shared_string_index_provider);
    check_fastxlsx_error(
        [&drain_projection_xml, &failing_worksheet_projections] {
            (void)drain_projection_xml(failing_worksheet_projections[1]);
        },
        "dirty worksheet shared-string projection should propagate provider failures");
    check(registry.dirty_session_count() == 2,
        "failed dirty worksheet shared-string projection should preserve dirty sessions");
    check(registry.dirty_cell_count()
            == data_session.cell_count() + alpha_session.cell_count(),
        "failed dirty worksheet shared-string projection should preserve dirty cell count");
    const fastxlsx::detail::CellRecord* dirty_text = data_session.try_cell(1, 2);
    check(dirty_text != nullptr && dirty_text->kind == fastxlsx::CellValueKind::Text
            && dirty_text->text_value == "dirty",
        "failed dirty worksheet shared-string projection should preserve dirty text cell");

    auto retry_shared_string_index_provider =
        std::make_shared<fastxlsx::detail::CellStoreSharedStringIndexProvider>(
            [](std::string_view text) -> std::uint32_t {
                return text == "dirty" ? 11U : 100U;
            });
    const std::vector<fastxlsx::detail::MaterializedWorksheetProjection>
        retry_worksheet_projections =
            registry.dirty_worksheet_chunk_sources(retry_shared_string_index_provider);
    const std::string retry_worksheet_xml =
        drain_projection_xml(retry_worksheet_projections[1]);
    check(retry_worksheet_xml.find(R"(<c r="B1" t="s"><v>11</v></c>)")
            != std::string::npos,
        "dirty worksheet shared-string projection should be retryable after provider failure");
    check(retry_worksheet_xml.find("inlineStr") == std::string::npos,
        "retry dirty worksheet shared-string projection should still avoid inline text");

    const std::vector<fastxlsx::detail::MaterializedWorksheetSheetDataProjection>
        failing_sheet_data_projections =
            registry.dirty_sheet_data_chunk_sources(throwing_shared_string_index_provider);
    check_fastxlsx_error(
        [&drain_projection_xml, &failing_sheet_data_projections] {
            (void)drain_projection_xml(failing_sheet_data_projections[1]);
        },
        "dirty sheetData shared-string projection should propagate provider failures");
    check(registry.dirty_session_count() == 2,
        "failed dirty sheetData shared-string projection should preserve dirty sessions");
    check(registry.dirty_cell_count()
            == data_session.cell_count() + alpha_session.cell_count(),
        "failed dirty sheetData shared-string projection should preserve dirty cell count");

    auto retry_sheet_data_shared_string_index_provider =
        std::make_shared<fastxlsx::detail::CellStoreSharedStringIndexProvider>(
            [](std::string_view text) -> std::uint32_t {
                return text == "dirty" ? 13U : 101U;
            });
    const std::vector<fastxlsx::detail::MaterializedWorksheetSheetDataProjection>
        retry_sheet_data_projections =
            registry.dirty_sheet_data_chunk_sources(
                retry_sheet_data_shared_string_index_provider);
    const std::string retry_sheet_data_xml =
        drain_projection_xml(retry_sheet_data_projections[1]);
    check(retry_sheet_data_xml.find(R"(<c r="B1" t="s"><v>13</v></c>)")
            != std::string::npos,
        "dirty sheetData shared-string projection should be retryable after provider failure");
    check(retry_sheet_data_xml.find("inlineStr") == std::string::npos,
        "retry dirty sheetData shared-string projection should still avoid inline text");
}

void test_internal_cell_store_sheet_data_serialization()
{
    fastxlsx::detail::CellStore store;
    auto empty_read_next_chunk = fastxlsx::detail::cell_store_sheet_data_chunk_source(store);
    std::string empty_xml;
    std::string chunk;
    while (empty_read_next_chunk(chunk)) {
        empty_xml += chunk;
    }
    check(empty_xml == "<sheetData></sheetData>",
        "empty CellStore sheetData chunk-source serialization mismatch");
    check(fastxlsx::detail::cell_store_dimension_reference(store) == "A1",
        "empty CellStore dimension reference should be A1");
    auto empty_worksheet_read_next_chunk =
        fastxlsx::detail::cell_store_worksheet_chunk_source(store);
    std::string empty_worksheet_xml;
    while (empty_worksheet_read_next_chunk(chunk)) {
        empty_worksheet_xml += chunk;
    }
    check(empty_worksheet_xml
            == R"(<?xml version="1.0" encoding="UTF-8"?>)"
               R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
               R"(<dimension ref="A1"/><sheetData></sheetData></worksheet>)",
        "empty CellStore worksheet chunk-source projection mismatch");

    store.set_cell(2, 2, fastxlsx::CellValue::text(" text & <tag> "));
    store.set_cell(1, 1, fastxlsx::CellValue::number(12.5));
    store.set_cell(1, 3, fastxlsx::CellValue::boolean(true));
    store.set_cell(3, 1, fastxlsx::CellValue::formula("SUM(A1:C1)&\"<ok>\""));
    store.set_cell(3, 2, fastxlsx::CellValue::blank().with_style(fastxlsx::StyleId {}));
    auto style_owner = fastxlsx::WorkbookWriter::create(
        fastxlsx::test::artifact_dir() / "fastxlsx-cell-store-style-owner-unused.xlsx");
    const fastxlsx::StyleId caller_style = style_owner.add_style(fastxlsx::CellStyle {"0.0"});
    check(caller_style.value() == 1, "first caller-owned style id should be 1");
    store.set_cell(4, 2, fastxlsx::CellValue::text("styled").with_style(caller_style));
    store.set_cell(4, 3, fastxlsx::CellValue::error("#VALUE!"));

    auto read_next_chunk = fastxlsx::detail::cell_store_sheet_data_chunk_source(store);
    std::vector<std::string> chunks;
    std::string xml;
    chunk.clear();
    while (read_next_chunk(chunk)) {
        chunks.push_back(chunk);
        xml += chunk;
    }
    check(xml
            == "<sheetData><row r=\"1\"><c r=\"A1\"><v>12.5</v></c>"
               "<c r=\"C1\" t=\"b\"><v>1</v></c></row><row r=\"2\">"
               "<c r=\"B2\" t=\"inlineStr\"><is><t xml:space=\"preserve\">"
               " text &amp; &lt;tag&gt; </t></is></c></row><row r=\"3\">"
                "<c r=\"A3\"><f>SUM(A1:C1)&amp;\"&lt;ok&gt;\"</f></c>"
                "<c r=\"B3\"/></row><row r=\"4\"><c r=\"B4\" s=\"1\" t=\"inlineStr\">"
                "<is><t>styled</t></is></c><c r=\"C4\" t=\"e\"><v>#VALUE!</v></c></row></sheetData>",
        "CellStore sheetData serialization should group sparse rows and escape payloads");
    check(xml.find("s=\"0\"") == std::string::npos,
        "CellStore sheetData should omit explicit default style attributes");
    check(xml.find("sharedStrings") == std::string::npos,
        "CellStore sheetData serialization should not imply sharedStrings migration");
    check(chunks.size() > store.cell_count(),
        "CellStore sheetData chunk source should expose row/cell chunks instead of one full payload");
    check(chunks.front() == "<sheetData>" && chunks.back() == "</sheetData>",
        "CellStore sheetData chunk source should expose standalone sheetData boundaries");
    check(std::find(chunks.begin(), chunks.end(),
              "<c r=\"B4\" s=\"1\" t=\"inlineStr\"><is><t>styled</t></is></c>")
            != chunks.end(),
        "CellStore sheetData chunk source should emit styled cells as individual cell chunks");
    check(std::find(chunks.begin(), chunks.end(), "<c r=\"C4\" t=\"e\"><v>#VALUE!</v></c>")
            != chunks.end(),
        "CellStore sheetData chunk source should emit error cells as individual cell chunks");
    check(fastxlsx::detail::cell_store_dimension_reference(store) == "A1:C4",
        "CellStore dimension reference should cover emitted sparse record extents");

    store.erase_cell(1, 1);
    store.erase_cell(1, 3);
    check(fastxlsx::detail::cell_store_dimension_reference(store) == "A2:C4",
        "CellStore dimension reference should shrink after erasing edge records");

    store.set_cell(5, 5, fastxlsx::CellValue::blank());
    check(fastxlsx::detail::cell_store_dimension_reference(store) == "A2:E5",
        "CellStore dimension reference should include explicit blank records");

    auto worksheet_read_next_chunk =
        fastxlsx::detail::cell_store_worksheet_chunk_source(store);
    std::vector<std::string> worksheet_chunks;
    std::string worksheet_xml;
    chunk.clear();
    while (worksheet_read_next_chunk(chunk)) {
        worksheet_chunks.push_back(chunk);
        worksheet_xml += chunk;
    }
    check(worksheet_chunks.size() > store.cell_count(),
        "CellStore worksheet chunk source should remain chunked around sheetData");
    check(worksheet_chunks.front() == R"(<?xml version="1.0" encoding="UTF-8"?>)",
        "CellStore worksheet chunk source should start with an XML declaration");
    check(worksheet_xml.find(R"(<dimension ref="A2:E5"/>)") != std::string::npos,
        "CellStore worksheet chunk source should refresh top-level dimension");
    check(worksheet_xml.find("<sheetData><row r=\"2\">") != std::string::npos,
        "CellStore worksheet chunk source should embed standalone sheetData chunks");
    check(worksheet_xml.find("<c r=\"B4\" s=\"1\" t=\"inlineStr\">") != std::string::npos,
        "CellStore worksheet chunk source should preserve existing style id attributes");
    check(worksheet_xml.find("sharedStrings") == std::string::npos,
        "CellStore worksheet chunk source should not imply sharedStrings migration");

    fastxlsx::detail::CellStore roundtrip_store;
    roundtrip_store.set_cell(2, 1, fastxlsx::CellValue::number(7.0));
    roundtrip_store.set_cell(5, 5, fastxlsx::CellValue::blank());
    auto roundtrip_read_next_chunk =
        fastxlsx::detail::cell_store_worksheet_chunk_source(roundtrip_store);
    std::string roundtrip_xml;
    chunk.clear();
    while (roundtrip_read_next_chunk(chunk)) {
        roundtrip_xml += chunk;
    }
    const fastxlsx::detail::CellStore reloaded_projection =
        fastxlsx::detail::load_cell_store_from_worksheet_xml(roundtrip_xml);
    check(reloaded_projection.cell_count() == roundtrip_store.cell_count(),
        "CellStore worksheet projection should round-trip through the internal loader");
    const fastxlsx::detail::CellRecord* projected_blank =
        reloaded_projection.find_cell(5, 5);
    check(projected_blank != nullptr && projected_blank->kind == fastxlsx::CellValueKind::Blank,
        "CellStore worksheet projection should preserve explicit blank extent records");
    check(fastxlsx::detail::cell_store_dimension_reference(reloaded_projection) == "A2:E5",
        "CellStore worksheet projection should reload with the refreshed dimension extent");
}

void test_internal_cell_store_worksheet_loader()
{
    const std::string worksheet_xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<sheetData>)"
        R"(<row r="1">)"
        R"(<c r="A1"><v>12.5</v></c>)"
        R"(<c r="B1" t="b"><v>1</v></c>)"
        R"(<c r="C1" t="inlineStr"><is><t xml:space="preserve"> hello &amp; raw </t></is></c>)"
        R"(<c r="D1"><f>SUM(A1:B1)&amp;"&lt;ok&gt;"</f><v>999</v></c>)"
        R"(<c r="E1"/>)"
        R"(<c r="F1" t="str"><v>scalar &amp; text</v></c>)"
        R"(<c r="G1" t="str"><f>TEXT(A1,"@")&amp;"!"</f><v>stale</v></c>)"
        R"(<c r="H1" t="e"><v>#VALUE!</v></c>)"
        R"(</row>)"
        R"(<row r="2">)"
        R"(<c r="A2"><v>0</v></c>)"
        R"(</row>)"
        R"(</sheetData>)"
        R"(</worksheet>)";

    const fastxlsx::detail::CellStore store =
        fastxlsx::detail::load_cell_store_from_worksheet_xml(worksheet_xml);

    check(store.cell_count() == 9, "worksheet loader should materialize explicit cells");
    const fastxlsx::detail::CellRecord* number = store.find_cell(1, 1);
    check(number != nullptr && number->kind == fastxlsx::CellValueKind::Number,
        "worksheet loader should load numeric cells");
    check(number->number_value == 12.5, "worksheet loader numeric payload mismatch");

    const fastxlsx::detail::CellRecord* boolean = store.find_cell(1, 2);
    check(boolean != nullptr && boolean->kind == fastxlsx::CellValueKind::Boolean,
        "worksheet loader should load boolean cells");
    check(boolean->boolean_value, "worksheet loader boolean payload mismatch");

    const fastxlsx::detail::CellRecord* text = store.find_cell(1, 3);
    check(text != nullptr && text->kind == fastxlsx::CellValueKind::Text,
        "worksheet loader should load inline string cells");
    check(text->text_value == " hello & raw ",
        "worksheet loader should decode inline string text");

    const fastxlsx::detail::CellRecord* formula = store.find_cell(1, 4);
    check(formula != nullptr && formula->kind == fastxlsx::CellValueKind::Formula,
        "worksheet loader should load formula cells");
    check(formula->text_value == "SUM(A1:B1)&\"<ok>\"",
        "worksheet loader should decode formula text and ignore cached values");

    const fastxlsx::detail::CellRecord* blank = store.find_cell(1, 5);
    check(blank != nullptr && blank->kind == fastxlsx::CellValueKind::Blank,
        "worksheet loader should keep explicit blank cells");
    const fastxlsx::detail::CellRecord* scalar_string = store.find_cell(1, 6);
    check(scalar_string != nullptr && scalar_string->kind == fastxlsx::CellValueKind::Text,
        "worksheet loader should load source t=str scalar cells as text");
    check(scalar_string->text_value == "scalar & text",
        "worksheet loader should decode source t=str scalar text");

    const fastxlsx::detail::CellRecord* string_formula = store.find_cell(1, 7);
    check(string_formula != nullptr && string_formula->kind == fastxlsx::CellValueKind::Formula,
        "worksheet loader should load source t=str formula cells as formulas");
    check(string_formula->text_value == "TEXT(A1,\"@\")&\"!\"",
        "worksheet loader should decode source t=str formula text and ignore cached values");

    const fastxlsx::detail::CellRecord* error = store.find_cell(1, 8);
    check(error != nullptr && error->kind == fastxlsx::CellValueKind::Error,
        "worksheet loader should load source t=e error cells");
    check(error->text_value == "#VALUE!",
        "worksheet loader should preserve source error token text");

    check(fastxlsx::detail::cell_store_dimension_reference(store) == "A1:H2",
        "source-loaded CellStore dimension reference should cover source cell extents");

    fastxlsx::detail::CellStore edited_store = store;
    edited_store.erase_cell(1, 1);
    edited_store.erase_cell(1, 2);
    edited_store.erase_cell(1, 3);
    edited_store.set_cell(3, 4, fastxlsx::CellValue::text("new extent"));
    check(fastxlsx::detail::cell_store_dimension_reference(edited_store) == "A1:H3",
        "mutated source-loaded CellStore dimension reference should cover edited extents");

    const fastxlsx::detail::CellRecord* second_row = store.find_cell(2, 1);
    check(second_row != nullptr && second_row->kind == fastxlsx::CellValueKind::Number,
        "worksheet loader should load later rows");
    check(second_row->number_value == 0.0, "worksheet loader should preserve zero numeric value");

    auto chunk_source = fastxlsx::detail::cell_store_sheet_data_chunk_source(store);
    std::string rebuilt_sheet_data;
    std::string chunk;
    while (chunk_source(chunk)) {
        rebuilt_sheet_data += chunk;
    }
    check(rebuilt_sheet_data.find("<c r=\"D1\"><f>SUM(A1:B1)&amp;\"&lt;ok&gt;\"</f></c>")
            != std::string::npos,
        "worksheet loader round-trip should preserve semantic formula text");
    check(rebuilt_sheet_data.find(
              "<c r=\"F1\" t=\"inlineStr\"><is><t>scalar &amp; text</t></is></c>")
            != std::string::npos,
        "worksheet loader round-trip should write source t=str scalar text as inline text");
    check(rebuilt_sheet_data.find("<c r=\"G1\"><f>TEXT(A1,\"@\")&amp;\"!\"</f></c>")
            != std::string::npos,
        "worksheet loader round-trip should write source t=str formulas as formula text");
    check(rebuilt_sheet_data.find("<c r=\"H1\" t=\"e\"><v>#VALUE!</v></c>")
            != std::string::npos,
        "worksheet loader round-trip should write source t=e error cells");

    std::size_t chunk_position = 0;
    const fastxlsx::detail::CellStore chunked_store =
        fastxlsx::detail::load_cell_store_from_worksheet_chunks(
            [&](std::string& output_chunk) {
                if (chunk_position >= worksheet_xml.size()) {
                    output_chunk.clear();
                    return false;
                }
                const std::size_t bytes =
                    std::min<std::size_t>(7, worksheet_xml.size() - chunk_position);
                output_chunk.assign(worksheet_xml.data() + chunk_position, bytes);
                chunk_position += bytes;
                return true;
            });
    check(chunked_store.cell_count() == store.cell_count(),
        "worksheet loader chunk-source path should match string loading");
    const fastxlsx::detail::CellRecord* chunked_formula = chunked_store.find_cell(1, 4);
    check(chunked_formula != nullptr && chunked_formula->text_value == "SUM(A1:B1)&\"<ok>\"",
        "worksheet loader chunk-source path should preserve formula text");

    fastxlsx::detail::CellStoreOptions loader_max_cell_options;
    loader_max_cell_options.max_cells = 1;
    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</v></c><c r="B1"><v>2</v></c></row></sheetData></worksheet>)",
                loader_max_cell_options);
        },
        "worksheet loader should enforce max_cells guardrails");

    fastxlsx::detail::CellStoreOptions loader_persistent_max_cell_options;
    loader_persistent_max_cell_options.max_cells = 2;
    fastxlsx::detail::CellStore max_guarded_loaded_store =
        fastxlsx::detail::load_cell_store_from_worksheet_xml(
            R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</v></c><c r="B1"><v>2</v></c></row></sheetData></worksheet>)",
            loader_persistent_max_cell_options);
    check(max_guarded_loaded_store.options().max_cells == 2,
        "worksheet loader should preserve max_cells options on returned CellStore");
    check_fastxlsx_error(
        [&max_guarded_loaded_store] {
            max_guarded_loaded_store.set_cell(1, 3, fastxlsx::CellValue::number(3.0));
        },
        "worksheet loader returned CellStore should keep enforcing max_cells");
    check(max_guarded_loaded_store.cell_count() == 2,
        "worksheet loader returned CellStore max_cells failure should not add records");
    check(max_guarded_loaded_store.find_cell(1, 3) == nullptr,
        "worksheet loader returned CellStore max_cells failure should not leave rejected cells");

    fastxlsx::detail::CellStoreOptions loader_memory_options;
    loader_memory_options.memory_budget_bytes = sizeof(fastxlsx::detail::CellStore);
    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData></worksheet>)",
                loader_memory_options);
        },
        "worksheet loader should enforce memory budget guardrails");

    fastxlsx::detail::CellStore loaded_memory_sizing_store;
    loaded_memory_sizing_store.set_cell(1, 1, fastxlsx::CellValue::text("a"));
    fastxlsx::detail::CellStoreOptions loader_persistent_memory_options;
    loader_persistent_memory_options.memory_budget_bytes =
        loaded_memory_sizing_store.estimated_memory_usage();
    fastxlsx::detail::CellStore memory_guarded_loaded_store =
        fastxlsx::detail::load_cell_store_from_worksheet_xml(
            R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>a</t></is></c></row></sheetData></worksheet>)",
            loader_persistent_memory_options);
    check(memory_guarded_loaded_store.options().memory_budget_bytes
            == loaded_memory_sizing_store.estimated_memory_usage(),
        "worksheet loader should preserve memory budget options on returned CellStore");
    check_fastxlsx_error(
        [&memory_guarded_loaded_store] {
            memory_guarded_loaded_store.set_cell(
                1, 1, fastxlsx::CellValue::text(std::string(1024, 'x')));
        },
        "worksheet loader returned CellStore should keep enforcing memory budget");
    const fastxlsx::detail::CellRecord* memory_guarded_cell =
        memory_guarded_loaded_store.find_cell(1, 1);
    check(memory_guarded_cell != nullptr,
        "worksheet loader returned CellStore memory failure should preserve the existing cell");
    check(memory_guarded_cell->text_value == "a",
        "worksheet loader returned CellStore memory failure should preserve the existing payload");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>bad &unknown;</t></is></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject unknown XML entity references");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>bad &amp</t></is></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject unterminated XML entities");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>bad &#xZZ;</t></is></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject invalid XML character references");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>bad &#x110000;</t></is></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject out-of-range XML character references");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" t="s"><v>0</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject shared string indexes");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" s="+1"><v>1</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject invalid style references");

    const fastxlsx::detail::CellStore explicit_default_style_store =
        fastxlsx::detail::load_cell_store_from_worksheet_xml(
            R"(<worksheet><sheetData><row r="1"><c r="A1" s="0"><v>1</v></c></row></sheetData></worksheet>)");
    const fastxlsx::detail::CellRecord* explicit_default_style_record =
        explicit_default_style_store.find_cell(1, 1);
    const fastxlsx::CellValue explicit_default_style_value =
        explicit_default_style_record != nullptr ? explicit_default_style_record->to_value()
                                                 : fastxlsx::CellValue::blank();
    check(explicit_default_style_record != nullptr
            && explicit_default_style_value.kind() == fastxlsx::CellValueKind::Number
            && explicit_default_style_value.number_value() == 1.0
            && !explicit_default_style_value.has_style(),
        "worksheet loader should normalize explicit default style references");
    const fastxlsx::detail::CellStore single_quoted_default_style_store =
        fastxlsx::detail::load_cell_store_from_worksheet_xml(
            R"(<worksheet><sheetData><row r="1"><c r="A1" s='0'><v>1</v></c></row></sheetData></worksheet>)");
    const fastxlsx::detail::CellRecord* single_quoted_default_style_record =
        single_quoted_default_style_store.find_cell(1, 1);
    check(single_quoted_default_style_record != nullptr
            && !single_quoted_default_style_record->to_value().has_style(),
        "worksheet loader should accept exact default style references with single quotes");
    const fastxlsx::detail::CellStore spaced_default_style_store =
        fastxlsx::detail::load_cell_store_from_worksheet_xml(
            R"(<worksheet><sheetData><row r="1"><c r="A1" s = "0"><v>1</v></c></row></sheetData></worksheet>)");
    const fastxlsx::detail::CellRecord* spaced_default_style_record =
        spaced_default_style_store.find_cell(1, 1);
    check(spaced_default_style_record != nullptr
            && !spaced_default_style_record->to_value().has_style(),
        "worksheet loader should accept exact default style references with whitespace around equals");

    {
        fastxlsx::detail::CellStore source_style_store =
            fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" s="7" t="inlineStr"><is><t>styled source</t></is></c></row></sheetData></worksheet>)");
        const fastxlsx::detail::CellRecord* source_style_record =
            source_style_store.find_cell(1, 1);
        check(source_style_record != nullptr && source_style_record->style_id.has_value()
                && source_style_record->style_id->value() == 7,
            "worksheet loader should materialize non-default source style ids");
        auto source_style_chunks =
            fastxlsx::detail::cell_store_sheet_data_chunk_source(source_style_store);
        std::string source_style_xml;
        std::string source_style_chunk;
        while (source_style_chunks(source_style_chunk)) {
            source_style_xml += source_style_chunk;
        }
        check(source_style_xml.find(
                  R"(<c r="A1" s="7" t="inlineStr"><is><t>styled source</t></is></c>)")
                != std::string::npos,
            "CellStore sheetData projection should write materialized source style ids");
    }

    const auto expect_rejected_style_token = [](std::string_view style_attribute,
                                                const char* scenario) {
        const std::string xml =
            std::string(R"(<worksheet><sheetData><row r="1"><c r="A1" )")
            + std::string(style_attribute)
            + R"(><v>1</v></c></row></sheetData></worksheet>)";
        check_fastxlsx_error(
            [&] {
                (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(xml);
            },
            scenario);
    };
    expect_rejected_style_token(R"(s="")",
        "worksheet loader should reject empty style references");
    expect_rejected_style_token(R"(s="00")",
        "worksheet loader should reject leading-zero default-like style references");
    expect_rejected_style_token(R"(s="+0")",
        "worksheet loader should reject signed default-like style references");
    expect_rejected_style_token(R"(s=" 0 ")",
        "worksheet loader should reject whitespace-padded default-like style references");
    expect_rejected_style_token(R"(s="&#48;")",
        "worksheet loader should reject entity-encoded default-like style references");
    expect_rejected_style_token(R"(s)",
        "worksheet loader should reject valueless default style attributes");
    expect_rejected_style_token(R"(s=0)",
        "worksheet loader should reject unquoted default style attributes");
    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" s="0><v>1</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject unterminated default style attributes");
    expect_rejected_style_token(R"(s="0" s="0")",
        "worksheet loader should reject duplicate default style attributes");
    expect_rejected_style_token(R"(x:s="0")",
        "worksheet loader should reject qualified default-like style references");
    expect_rejected_style_token(R"(x:s="1")",
        "worksheet loader should reject qualified non-default style references");

    {
        fastxlsx::detail::CellStore row_metadata_store =
            fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1" spans="1:13" s="4" customFormat="1" ht="20" customHeight="1"><c r="A1"><v>1</v></c></row></sheetData></worksheet>)");
        const fastxlsx::detail::CellRecord* record =
            row_metadata_store.try_cell(1, 1);
        check(record != nullptr && record->kind == fastxlsx::CellValueKind::Number
                && record->number_value == 1.0,
            "worksheet loader should tolerate and ignore common source row metadata attributes");
    }
    {
        fastxlsx::detail::CellStore cell_metadata_store =
            fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" ph="1"><v>1</v></c></row></sheetData><phoneticPr fontId="1"/></worksheet>)");
        const fastxlsx::detail::CellRecord* record =
            cell_metadata_store.try_cell(1, 1);
        check(record != nullptr && record->kind == fastxlsx::CellValueKind::Number
                && record->number_value == 1.0,
            "worksheet loader should tolerate and ignore source phonetic cell metadata");
    }

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" cm="1"><v>1</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject unsupported cell metadata attributes");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</v></c><c r="A1"><v>2</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject duplicate cell references");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</v></c></row><row r="1"><c r="B1"><v>2</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject duplicate row numbers");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="2"><c r="A2"><v>2</v></c></row><row r="1"><c r="A1"><v>1</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject out-of-order row numbers");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="B1"><v>2</v></c><c r="A1"><v>1</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject out-of-order cell references");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" t="z"><v>1</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject unsupported cell types");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" t="e"/></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject missing error cell payloads");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" t="e"><v></v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject empty error cell payloads");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" t="d"><v>2026-06-16T00:00:00Z</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject date-like cell payloads");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1"><is><t>bad</t></is></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject inline text in non-inline string cells");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><v>bad</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject value text in inline string cells");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</v><v>2</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject duplicate scalar value elements");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1"><v foo="1">1</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject unsupported scalar value attributes");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1"><f>1</f><f>2</f></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject duplicate formula elements");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1"><f/></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject empty formula text");

    {
        const fastxlsx::detail::CellStore formula_metadata_store =
            fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1"><f t="array" ref="A1" aca="1" ca="1" bx="1">PI()</f><v>3.14</v></c></row><row r="2"><c r="A2"><f t="shared" si="0"/><v>2</v></c></row><row r="3"><c r="A3"><f t="shared" ref="A3:B4" si="3" ca="1">A3+B$3+$A3+$A$3+SUM(A3:B3)&amp;"A3"+'Other Sheet'!A3+[Book.xlsx]Sheet1!A3+Table1[A3]</f><v>1</v></c></row><row r="4"><c r="B4"><f t="shared" si="3" aca="1" bx="1"/><v>999</v></c></row><row r="5"><c r="A5"><f t="shared" ref="A5:B6" si="4">XFD5+$XFD5+XFD$5+$XFD$5+XFD:XFD+$XFD:XFD+1048576:1048576+$1048576:1048576</f></c></row><row r="6"><c r="B6"><f t="shared" si="4"/><v>111</v></c></row><row r="7"><c r="A7"><f t="dataTable" ref="A7:B8" dt2D="1" dtr="1" del1="0" del2="0" r1="A1" r2="B1">TABLE(A1,B1)</f><v>5</v></c></row></sheetData></worksheet>)");
        const fastxlsx::detail::CellRecord* array_formula =
            formula_metadata_store.try_cell(1, 1);
        check(array_formula != nullptr && array_formula->kind == fastxlsx::CellValueKind::Formula
                && array_formula->text_value == "PI()",
            "worksheet loader should flatten formula metadata when formula text is present");
        const fastxlsx::detail::CellRecord* shared_cached =
            formula_metadata_store.try_cell(2, 1);
        check(shared_cached != nullptr && shared_cached->kind == fastxlsx::CellValueKind::Number
                && shared_cached->number_value == 2.0,
            "worksheet loader should materialize cached values for unresolved metadata-only shared formulas");
        const fastxlsx::detail::CellRecord* shared_base =
            formula_metadata_store.try_cell(3, 1);
        check(shared_base != nullptr && shared_base->kind == fastxlsx::CellValueKind::Formula
                && shared_base->text_value
                    == R"(A3+B$3+$A3+$A$3+SUM(A3:B3)&"A3"+'Other Sheet'!A3+[Book.xlsx]Sheet1!A3+Table1[A3])",
            "worksheet loader should materialize shared formula definitions as plain formula text");
        const fastxlsx::detail::CellRecord* shared_follower =
            formula_metadata_store.try_cell(4, 2);
        check(shared_follower != nullptr
                && shared_follower->kind == fastxlsx::CellValueKind::Formula
                && shared_follower->text_value
                    == R"(B4+C$3+$A4+$A$3+SUM(B4:C4)&"A3"+'Other Sheet'!B4+[Book.xlsx]Sheet1!B4+Table1[A3])",
            "worksheet loader should translate shared formula followers while preserving absolute refs and skipped tokens");
        const fastxlsx::detail::CellRecord* shared_edge_follower =
            formula_metadata_store.try_cell(6, 2);
        check(shared_edge_follower != nullptr
                && shared_edge_follower->kind == fastxlsx::CellValueKind::Formula
                && shared_edge_follower->text_value
                    == "#REF!+$XFD6+#REF!+$XFD$5+#REF!+#REF!+#REF!+#REF!",
            "worksheet loader should translate out-of-range relative shared formula and axis-range references to #REF!");
        const fastxlsx::detail::CellRecord* data_table_formula =
            formula_metadata_store.try_cell(7, 1);
        check(data_table_formula != nullptr
                && data_table_formula->kind == fastxlsx::CellValueKind::Formula
                && data_table_formula->text_value == "TABLE(A1,B1)",
            "worksheet loader should tolerate known dataTable formula metadata attributes");
    }
    {
        const fastxlsx::detail::CellStore shared_formula_matrix =
            fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData>)"
                R"(<row r="1">)"
                R"(<c r="A1"><f t="shared" ref="A1:C2" si="1">A1+Sheet1!A1+'O''Brien'!A1+SUM(A1:B1)+LOG10(A1)+A1foo+_A1+A1_+R1C1+Table1[A1]+SUM(A:A)+SUM(1:1)+SUM(Sheet1!A:A)+SUM('Other Sheet'!1:1)+SUM($A:B)+SUM($1:2)</f><v>1</v></c>)"
                R"(<c r="B1"><f t="shared" ref="B1:D1" si="2">C1+D$1+$C1+$C$1</f><v>2</v></c>)"
                R"(<c r="C1"><f t="shared" si="1"/><v>777</v></c>)"
                R"(<c r="D1"><f t="shared" si="2"/><v>888</v></c>)"
                R"(</row>)"
                R"(<row r="2"><c r="A2"><f t="shared" si="1"/><v>999</v></c></row>)"
                R"(<row r="3"><c r="A3"><f t="shared" ref="A3:B3" si="1">Z3+1</f><v>3</v></c><c r="B3"><f t="shared" si="1"/><v>4</v></c></row>)"
                R"(<row r="4"><c r="A4"><f t="shared" si="77"/><v>77</v></c></row>)"
                R"(</sheetData></worksheet>)");
        const auto expect_formula = [&](std::uint32_t row, std::uint32_t column,
                                        std::string_view expected, const char* message) {
            const fastxlsx::detail::CellRecord* record =
                shared_formula_matrix.try_cell(row, column);
            check(record != nullptr && record->kind == fastxlsx::CellValueKind::Formula
                    && record->text_value == expected,
                message);
        };
        expect_formula(1, 3,
            "C1+Sheet1!C1+'O''Brien'!C1+SUM(C1:D1)+LOG10(C1)+A1foo+_A1+A1_+R1C1+Table1[A1]+SUM(C:C)+SUM(1:1)+SUM(Sheet1!C:C)+SUM('Other Sheet'!1:1)+SUM($A:D)+SUM($1:2)",
            "worksheet loader should translate multiple followers without touching names or structured refs");
        expect_formula(1, 4, "E1+F$1+$C1+$C$1",
            "worksheet loader should isolate interleaved shared formula indexes");
        expect_formula(2, 1,
            "A2+Sheet1!A2+'O''Brien'!A2+SUM(A2:B2)+LOG10(A2)+A1foo+_A1+A1_+R1C1+Table1[A1]+SUM(A:A)+SUM(2:2)+SUM(Sheet1!A:A)+SUM('Other Sheet'!2:2)+SUM($A:B)+SUM($1:3)",
            "worksheet loader should translate row-offset shared formula followers");
        expect_formula(3, 2, "AA3+1",
            "worksheet loader should use the latest source-order shared formula definition for later followers");
        const fastxlsx::detail::CellRecord* unresolved =
            shared_formula_matrix.try_cell(4, 1);
        check(unresolved != nullptr && unresolved->kind == fastxlsx::CellValueKind::Number
                && unresolved->number_value == 77.0,
            "worksheet loader should keep unresolved shared formula followers on cached scalar fallback");
    }
    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1"><f t="shared" si="0"/></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject formula cells without a materializable value");
    const std::vector<std::string_view> invalid_shared_formula_indexes {
        "", "-1", "+1", "1.0", "18446744073709551616"};
    for (std::string_view invalid_index : invalid_shared_formula_indexes) {
        const std::string xml =
            std::string(R"(<worksheet><sheetData><row r="1"><c r="A1"><f t="shared" si=")")
            + std::string(invalid_index)
            + R"(">1</f></c></row></sheetData></worksheet>)";
        check_fastxlsx_error(
            [&] {
                (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(xml);
            },
            "worksheet loader should reject invalid shared formula indexes");
    }
    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1"><f foo="1">1</f></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject unsupported formula attributes");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>a</t><t>b</t></is></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject duplicate inline text elements");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t foo="1">a</t></is></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject unsupported inline text attributes");

    {
        const fastxlsx::detail::CellStore rich_inline_store =
            fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><r><rPr><b/><color rgb="FFFF0000"/></rPr><t>rich-</t></r><r><t>A&amp;B </t></r><r><t xml:space="preserve"> kept </t></r><rPh sb="0" eb="1"><t>ignored-phonetic</t></rPh><phoneticPr fontId="1"/><extLst><ext uri="{fastxlsx-test}"><t>ignored-ext</t></ext></extLst></is></c></row></sheetData></worksheet>)");
        const fastxlsx::detail::CellRecord* rich_inline =
            rich_inline_store.find_cell(1, 1);
        check(rich_inline != nullptr && rich_inline->kind == fastxlsx::CellValueKind::Text,
            "worksheet loader should materialize source inline rich text as text");
        check(rich_inline->text_value == "rich-A&B  kept ",
            "worksheet loader should flatten inline rich text and ignore phonetic/ext text");
    }

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>direct-</t><r><t>rich</t></r></is></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject mixed direct and rich inline text");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><rPr><b/></rPr></is></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject inline rich text properties outside a run");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><r><rPr><t>not-text</t></rPr><t>rich</t></r></is></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject value markup inside inline rich text properties");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><r><t>rich</t></is></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject unclosed inline rich text runs");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><extLst><ext uri="{fastxlsx-test}"><t>ignored</t></extLst></is></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject unclosed ignored inline rich text metadata");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>a<!--hidden-->b</t></is></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject comments inside source cells");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>a<?fx hidden?>b</t></is></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject processing instructions inside source cells");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t><![CDATA[hidden]]></t></is></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject unsupported markup inside source cells");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1"><c r="B1"><v>1</v></c></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject nested cells");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><c r="A1"><v>1</v></c></sheetData></worksheet>)");
        },
        "worksheet loader should reject cells outside row elements");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c><v>1</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject cells without explicit references");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="XFE1"><v>1</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject cell references beyond Excel limits");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A0"><v>1</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject zero row cell references");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1048577"><v>1</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject cell references beyond the last Excel row");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="1A"><v>1</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject non-column-first cell references");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r=A1><v>1</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject unquoted cell reference attributes");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1><v>1</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject unterminated cell reference attributes");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" r="B1"><v>1</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject duplicate cell reference attributes");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1" r="2"><c r="A1"><v>1</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject duplicate row reference attributes");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" t="n" t="b"><v>1</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject duplicate cell type attributes");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</v></c><c r="B2"><v>2</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject row / cell reference mismatches");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="0"><c r="A1"><v>1</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject zero row numbers");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1048577"><c r="A1048577"><v>1</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject row numbers beyond Excel limits");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="bad"><c r="A1"><v>1</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject non-numeric row numbers");

    {
        const fastxlsx::detail::CellStore formula_cached_result_store =
            fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" t="b"><f>TRUE()</f><v>1</v></c></row></sheetData></worksheet>)");
        const fastxlsx::detail::CellRecord* formula =
            formula_cached_result_store.find_cell(1, 1);
        check(formula != nullptr && formula->kind == fastxlsx::CellValueKind::Formula,
            "worksheet loader should materialize typed cached-result formula cells as formulas");
        check(formula->text_value == "TRUE()",
            "worksheet loader should preserve formula text and ignore typed cached results");
    }

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1e999</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject non-finite numeric values");

    check_fastxlsx_error(
        [&] {
            (void)fastxlsx::detail::load_cell_store_from_worksheet_xml(
                R"(<worksheet><sheetData><row r="1"><c r="A1" t="b"><v>2</v></c></row></sheetData></worksheet>)");
        },
        "worksheet loader should reject invalid boolean payloads");
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

    const auto output_path = fastxlsx::test::artifact_dir() / "fastxlsx-phase1-minimal.xlsx";
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
        fastxlsx::test::artifact_dir() / "fastxlsx-document-properties.xlsx";
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
        fastxlsx::test::artifact_dir() / "fastxlsx-formula-row-height.xlsx";
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
            fastxlsx::test::artifact_dir() / "fastxlsx-empty-worksheet.xlsx";
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
            fastxlsx::test::artifact_dir() / "fastxlsx-empty-row-worksheet.xlsx";
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
            fastxlsx::test::artifact_dir() / "fastxlsx-max-columns.xlsx";
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
        workbook.save(fastxlsx::test::artifact_dir() / "invalid-empty.xlsx");
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
                    fastxlsx::test::artifact_dir() / "invalid-too-wide-row.xlsx");
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
        test_workbook_sheet_inspection_helpers();
        test_workbook_sheet_addition_uniqueness();
        test_workbook_sheet_removal_helpers();
        test_workbook_sheet_rename_helpers();
        test_workbook_memory_diagnostics();
        test_internal_cell_store_sparse_boundary();
        test_internal_cell_store_guardrails();
        test_internal_materialized_worksheet_session();
        test_internal_materialized_worksheet_session_shifts_sparse_records();
        test_internal_materialized_worksheet_session_shift_noops_preserve_state();
        test_internal_materialized_worksheet_session_shift_failures_preserve_state();
        test_internal_materialized_worksheet_session_registry();
        test_internal_cell_store_sheet_data_serialization();
        test_internal_cell_store_worksheet_loader();
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
