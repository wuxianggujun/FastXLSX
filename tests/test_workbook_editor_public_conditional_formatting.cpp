#include "../src/package_editor.hpp"
#include <fastxlsx/worksheet_reader.hpp>

#include "test_workbook_editor_facade_common.hpp"

#include <algorithm>
#include <limits>
#include <map>

class ScopedConditionalFormatReplacementStagedHook {
public:
    explicit ScopedConditionalFormatReplacementStagedHook(
        fastxlsx::detail::PackageEditorWorksheetPartReplacementStagedHook hook)
    {
        fastxlsx::detail::testing_set_package_editor_worksheet_part_replacement_staged_hook(
            hook);
    }

    ~ScopedConditionalFormatReplacementStagedHook()
    {
        fastxlsx::detail::testing_set_package_editor_worksheet_part_replacement_staged_hook(
            nullptr);
    }

    ScopedConditionalFormatReplacementStagedHook(
        const ScopedConditionalFormatReplacementStagedHook&) = delete;
    ScopedConditionalFormatReplacementStagedHook& operator=(
        const ScopedConditionalFormatReplacementStagedHook&) = delete;
};

void fail_after_conditional_format_staging()
{
    throw fastxlsx::FastXlsxError("injected conditional formatting staging failure");
}

std::filesystem::path write_source_with_conditional_formatting(std::string_view name)
{
    const std::filesystem::path path = artifact(name);
    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    const fastxlsx::StyleId formula_style =
        writer.add_style(fastxlsx::CellStyle {"0.00"});
    auto data = writer.add_worksheet("Data");
    data.append_row({fastxlsx::CellView::text("Score"),
        fastxlsx::CellView::formula("1+1").with_style(formula_style)});
    data.append_row({fastxlsx::CellView::number(10.0), fastxlsx::CellView::number(2.0)});

    fastxlsx::TwoColorScaleRule initial;
    initial.lower.color = fastxlsx::ArgbColor {0xFF, 0xF8, 0x69, 0x6B};
    initial.upper.color = fastxlsx::ArgbColor {0xFF, 0x63, 0xBE, 0x7B};
    data.add_conditional_color_scale({2, 1, 10, 1}, initial);

    fastxlsx::DataValidationRule validation;
    validation.type = fastxlsx::DataValidationType::Whole;
    validation.operator_type = fastxlsx::DataValidationOperator::Between;
    validation.formula1 = "1";
    validation.formula2 = "100";
    data.add_data_validation({2, 2, 10, 2}, validation);
    data.add_external_hyperlink(1, 1, "https://example.invalid/source");

    auto untouched = writer.add_worksheet("Untouched");
    untouched.append_row({fastxlsx::CellView::text("keep")});
    writer.close();
    return path;
}

std::string xml_element_fragment(
    const std::string& xml, std::string_view opening, std::string_view closing)
{
    const std::size_t begin = xml.find(opening);
    const std::size_t closing_begin =
        begin == std::string::npos ? std::string::npos : xml.find(closing, begin);
    if (begin == std::string::npos || closing_begin == std::string::npos) {
        throw std::runtime_error("required XML fragment is missing");
    }
    return xml.substr(begin, closing_begin + closing.size() - begin);
}

std::size_t count_occurrences(std::string_view text, std::string_view fragment)
{
    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = text.find(fragment, offset)) != std::string_view::npos) {
        ++count;
        offset += fragment.size();
    }
    return count;
}

const fastxlsx::WorkbookEditorWorksheetEditSummary* find_edit_summary(
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary>& summaries,
    std::string_view planned_name)
{
    const auto found = std::find_if(summaries.begin(), summaries.end(),
        [planned_name](const auto& summary) {
            return summary.planned_name == planned_name;
        });
    return found == summaries.end() ? nullptr : &*found;
}

template <typename Mutation>
void expect_conditional_format_source_rejection(
    const std::map<std::string, std::string>& baseline_entries,
    std::string_view artifact_name, Mutation mutation, std::string_view label)
{
    auto entries = baseline_entries;
    mutation(entries.at("xl/worksheets/sheet1.xml"));
    const std::filesystem::path source = artifact(artifact_name);
    fastxlsx::test::write_stored_zip_entries(source, entries);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    const std::string failure_message = std::string(label) + " should fail";
    check(threw_fastxlsx_error([&] {
        editor.add_conditional_data_bar(
            "Data", fastxlsx::CellRange {2, 3, 10, 3}, {});
    }), failure_message);
    check(!editor.has_pending_changes() && !editor.has_unsaved_changes()
            && editor.pending_change_count() == 0
            && editor.pending_worksheet_edits().empty(),
        std::string(label) + " should not publish package or public state");
}

void test_append_all_rule_kinds_priority_and_preservation()
{
    const std::filesystem::path source = write_source_with_conditional_formatting(
        "fastxlsx-workbook-editor-conditional-formatting-source.xlsx");
    auto source_entries = fastxlsx::test::read_zip_entries(source);
    replace_first_or_throw(source_entries.at("xl/worksheets/sheet1.xml"),
        R"(priority="1")", R"(priority="7")");
    const std::string calc_chain =
        R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?><calcChain xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><c r="B1" i="1"/></calcChain>)";
    source_entries.emplace("xl/calcChain.xml", calc_chain);
    replace_first_or_throw(source_entries.at("[Content_Types].xml"), "</Types>",
        R"(<Override PartName="/xl/calcChain.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.calcChain+xml"/></Types>)");
    replace_first_or_throw(source_entries.at("xl/_rels/workbook.xml.rels"),
        "</Relationships>",
        R"(<Relationship Id="rId99" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="calcChain.xml"/></Relationships>)");
    source_entries.emplace("custom/opaque.bin", "conditional formatting unknown entry");
    fastxlsx::test::write_stored_zip_entries(source, source_entries);

    const std::string source_sheet_data = xml_element_fragment(
        source_entries.at("xl/worksheets/sheet1.xml"), "<sheetData", "</sheetData>");
    const std::string source_untouched_worksheet =
        source_entries.at("xl/worksheets/sheet2.xml");
    const std::string source_relationships =
        source_entries.at("xl/worksheets/_rels/sheet1.xml.rels");
    const std::string source_workbook_relationships =
        source_entries.at("xl/_rels/workbook.xml.rels");
    const std::string source_content_types = source_entries.at("[Content_Types].xml");
    const std::string source_workbook_xml = source_entries.at("xl/workbook.xml");
    const std::string source_styles = source_entries.at("xl/styles.xml");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    fastxlsx::TwoColorScaleRule two_color;
    two_color.lower.type = fastxlsx::ColorScaleValueType::Percent;
    two_color.lower.value = 10.0;
    two_color.lower.color = fastxlsx::ArgbColor {0xFF, 0xFF, 0xEB, 0x84};
    two_color.upper.type = fastxlsx::ColorScaleValueType::Percent;
    two_color.upper.value = 90.0;
    two_color.upper.color = fastxlsx::ArgbColor {0xFF, 0x5A, 0x8A, 0xD6};
    editor.add_conditional_color_scale(
        "Data", {{2, 1, 4, 1}, {6, 1, 8, 1}}, two_color);

    fastxlsx::ThreeColorScaleRule three_color;
    three_color.lower.color = fastxlsx::ArgbColor {0xFF, 0xF8, 0x69, 0x6B};
    three_color.upper.color = fastxlsx::ArgbColor {0xFF, 0x63, 0xBE, 0x7B};
    editor.add_conditional_color_scale(
        "Data", fastxlsx::CellRange {2, 2, 10, 2}, three_color);

    fastxlsx::DataBarRule data_bar;
    data_bar.show_value = false;
    data_bar.color = fastxlsx::ArgbColor {0xFF, 0x11, 0x88, 0xCC};
    editor.add_conditional_data_bar(
        "Data", {{2, 3, 4, 3}, {6, 3, 8, 3}}, data_bar);

    fastxlsx::IconSetRule icon_set;
    icon_set.value_type = fastxlsx::IconSetValueType::Percentile;
    icon_set.thresholds = {10.0, 50.0, 90.0};
    icon_set.show_value = false;
    icon_set.reverse = true;
    editor.add_conditional_icon_set(
        "Data", fastxlsx::CellRange {2, 4, 10, 4}, icon_set);

    const auto summaries = editor.pending_worksheet_edits();
    const auto* summary = find_edit_summary(summaries, "Data");
    check(summary != nullptr && summary->conditional_format_count == 4,
        "four conditional-formatting additions should be visible in public diagnostics");
    check(editor.pending_change_count() == 4 && editor.has_unsaved_changes(),
        "conditional-formatting calls should advance pending and unsaved state");
    check(threw_fastxlsx_error([&] { editor.remove_worksheet("Data"); }),
        "worksheet removal should reject queued conditional-formatting edits");

    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-conditional-formatting-output.xlsx");
    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    const std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check(count_occurrences(worksheet_xml, "<conditionalFormatting ") == 5,
        "source rule plus four additions should produce five conditionalFormatting elements");
    check_contains(worksheet_xml,
        R"(<conditionalFormatting sqref="A2:A4 A6:A8"><cfRule type="colorScale" priority="8"><colorScale><cfvo type="percent" val="10"/><cfvo type="percent" val="90"/><color rgb="FFFFEB84"/><color rgb="FF5A8AD6"/></colorScale></cfRule></conditionalFormatting>)",
        "two-color multi-range Patch XML or priority mismatch");
    check_contains(worksheet_xml,
        R"(<conditionalFormatting sqref="B2:B10"><cfRule type="colorScale" priority="9"><colorScale><cfvo type="min"/><cfvo type="percentile" val="50"/><cfvo type="max"/><color rgb="FFF8696B"/><color rgb="FFFFEB84"/><color rgb="FF63BE7B"/></colorScale></cfRule></conditionalFormatting>)",
        "three-color Patch XML or priority mismatch");
    check_contains(worksheet_xml,
        R"(<conditionalFormatting sqref="C2:C4 C6:C8"><cfRule type="dataBar" priority="10"><dataBar showValue="0"><cfvo type="min"/><cfvo type="max"/><color rgb="FF1188CC"/></dataBar></cfRule></conditionalFormatting>)",
        "data-bar Patch XML or priority mismatch");
    check_contains(worksheet_xml,
        R"(<conditionalFormatting sqref="D2:D10"><cfRule type="iconSet" priority="11"><iconSet iconSet="3Arrows" showValue="0" reverse="1"><cfvo type="percentile" val="10"/><cfvo type="percentile" val="50"/><cfvo type="percentile" val="90"/></iconSet></cfRule></conditionalFormatting>)",
        "icon-set Patch XML or priority mismatch");
    check(worksheet_xml.find("<conditionalFormatting")
            < worksheet_xml.find("<dataValidations"),
        "conditional formatting should remain before data validations in schema order");
    check(worksheet_xml.find("<dataValidations") < worksheet_xml.find("<hyperlinks"),
        "existing data validations and hyperlinks should retain schema order");
    check(xml_element_fragment(worksheet_xml, "<sheetData", "</sheetData>")
            == source_sheet_data,
        "conditional formatting should preserve formula and styled cell XML exactly");
    check(entries.at("xl/worksheets/sheet2.xml") == source_untouched_worksheet,
        "conditional formatting should preserve untouched worksheet XML exactly");
    check(entries.at("xl/worksheets/_rels/sheet1.xml.rels") == source_relationships,
        "conditional formatting should preserve worksheet relationships exactly");
    check(entries.at("xl/_rels/workbook.xml.rels") == source_workbook_relationships,
        "conditional formatting should preserve workbook relationships exactly");
    check(entries.at("[Content_Types].xml") == source_content_types,
        "conditional formatting should preserve content types exactly");
    check(entries.at("xl/workbook.xml") == source_workbook_xml,
        "conditional formatting should preserve workbook calculation metadata exactly");
    check(entries.at("xl/styles.xml") == source_styles,
        "conditional formatting should preserve styles/dxf infrastructure exactly");
    check(entries.at("xl/calcChain.xml") == calc_chain,
        "conditional formatting should preserve calcChain bytes exactly");
    check(entries.at("custom/opaque.bin") == "conditional formatting unknown entry",
        "conditional formatting should preserve unknown package entries");

    fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(output);
    std::vector<fastxlsx::WorksheetConditionalFormatKind> kinds;
    std::vector<std::uint32_t> priorities;
    fastxlsx::WorksheetConditionalFormatReadCallbacks callbacks;
    callbacks.on_conditional_format =
        [&](const fastxlsx::WorksheetConditionalFormatView& value) {
            kinds.push_back(value.kind);
            priorities.push_back(value.priority);
        };
    const auto read_summary =
        reader.read_worksheet_conditional_formats("Data", callbacks);
    check(read_summary.conditional_format_count == 5 && kinds.size() == 5,
        "bounded reader should reopen all source and appended conditional formats");
    check(priorities == std::vector<std::uint32_t>({7, 8, 9, 10, 11}),
        "reopened conditional-format priorities should preserve source order and max+1 allocation");
    check(kinds == std::vector<fastxlsx::WorksheetConditionalFormatKind>({
            fastxlsx::WorksheetConditionalFormatKind::TwoColorScale,
            fastxlsx::WorksheetConditionalFormatKind::TwoColorScale,
            fastxlsx::WorksheetConditionalFormatKind::ThreeColorScale,
            fastxlsx::WorksheetConditionalFormatKind::DataBar,
            fastxlsx::WorksheetConditionalFormatKind::IconSet}),
        "reopened conditional-format kinds should match the staged rules");
}

void test_rename_added_worksheet_and_reopen()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-conditional-formatting-rename-source.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    fastxlsx::TwoColorScaleRule scale;
    scale.lower.color = fastxlsx::ArgbColor {0xFF, 0xFF, 0x00, 0x00};
    scale.upper.color = fastxlsx::ArgbColor {0xFF, 0x00, 0xB0, 0x50};
    editor.add_conditional_color_scale(
        "Data", fastxlsx::CellRange {1, 1, 4, 1}, scale);
    editor.rename_sheet("Data", "Renamed Data");

    editor.add_worksheet("Added");
    fastxlsx::IconSetRule icons;
    editor.add_conditional_icon_set(
        "Added", fastxlsx::CellRange {1, 1, 5, 1}, icons);
    editor.rename_sheet("Added", "Renamed Added");

    const auto summaries = editor.pending_worksheet_edits();
    const auto* renamed = find_edit_summary(summaries, "Renamed Data");
    const auto* added = find_edit_summary(summaries, "Renamed Added");
    check(renamed != nullptr && renamed->renamed
            && renamed->conditional_format_count == 1,
        "source worksheet rename should migrate conditional-format diagnostics");
    check(added != nullptr && added->added && !added->renamed
            && added->conditional_format_count == 1,
        "added worksheet rename should migrate conditional-format diagnostics");

    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-conditional-formatting-rename-output.xlsx");
    editor.save_as(output);
    const auto entries = fastxlsx::test::read_zip_entries(output);
    check_contains(entries.at("xl/worksheets/sheet1.xml"),
        R"(<cfRule type="colorScale" priority="1">)",
        "renamed source worksheet should retain its conditional format");
    check_contains(entries.at("xl/worksheets/sheet3.xml"),
        R"(<cfRule type="iconSet" priority="1">)",
        "renamed added worksheet should retain its conditional format");

    const std::filesystem::path reopened_output = artifact(
        "fastxlsx-workbook-editor-conditional-formatting-reopened-output.xlsx");
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::DataBarRule data_bar;
    reopened.add_conditional_data_bar(
        "Renamed Data", fastxlsx::CellRange {1, 2, 4, 2}, data_bar);
    reopened.save_as(reopened_output);
    check_contains(fastxlsx::test::read_zip_entries(reopened_output)
            .at("xl/worksheets/sheet1.xml"),
        R"(<cfRule type="dataBar" priority="2">)",
        "reopened Patch edit should continue the worksheet priority sequence");
}

void test_invalid_input_staging_failure_and_save_retry()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-conditional-formatting-failure-source.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    fastxlsx::TwoColorScaleRule scale;
    check(threw_fastxlsx_error([&] {
        editor.add_conditional_color_scale(
            "Data", fastxlsx::CellRange {0, 1, 2, 1}, scale);
    }), "invalid conditional-format range should fail");
    check(threw_fastxlsx_error([&] {
        editor.add_conditional_color_scale(
            "Data", std::span<const fastxlsx::CellRange> {}, scale);
    }), "empty conditional-format range list should fail");

    fastxlsx::DataBarRule invalid_bar;
    invalid_bar.lower.type = fastxlsx::DataBarValueType::Number;
    invalid_bar.lower.value = std::numeric_limits<double>::quiet_NaN();
    check(threw_fastxlsx_error([&] {
        editor.add_conditional_data_bar(
            "Data", fastxlsx::CellRange {1, 1, 2, 1}, invalid_bar);
    }), "non-finite data-bar endpoint should fail");

    fastxlsx::IconSetRule invalid_icons;
    invalid_icons.thresholds = {0.0, 80.0, 20.0};
    check(threw_fastxlsx_error([&] {
        editor.add_conditional_icon_set(
            "Data", fastxlsx::CellRange {1, 1, 2, 1}, invalid_icons);
    }), "non-ascending icon thresholds should fail");
    check(!editor.has_pending_changes() && editor.pending_worksheet_edits().empty(),
        "invalid conditional-format calls should not publish state");

    editor.add_conditional_color_scale(
        "Data", fastxlsx::CellRange {1, 1, 2, 1}, scale);
    {
        ScopedConditionalFormatReplacementStagedHook hook(
            fail_after_conditional_format_staging);
        check(threw_fastxlsx_error([&] {
            editor.add_conditional_data_bar(
                "Data", fastxlsx::CellRange {1, 2, 2, 2}, {});
        }), "injected conditional-format staging failure should escape");
    }
    check(editor.pending_worksheet_edits().front().conditional_format_count == 1,
        "staging failure should not publish a second conditional-format diagnostic");
    editor.add_conditional_data_bar(
        "Data", fastxlsx::CellRange {1, 2, 2, 2}, {});
    check(editor.pending_worksheet_edits().front().conditional_format_count == 2,
        "successful retry should publish the second conditional format");
    check(!editor.last_edit_error().has_value(),
        "successful conditional-format retry should clear the previous error");

    check(threw_fastxlsx_error([&] {
        editor.save_as(artifact("fastxlsx-workbook-editor-conditional-formatting-missing")
            / "parent" / "output.xlsx");
    }), "conditional-format save should fail when the output parent is missing");
    check(editor.has_unsaved_changes(),
        "failed save should retain conditional-format retry state");

    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-conditional-formatting-retry-output.xlsx");
    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& worksheet_xml =
        output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<cfRule type="colorScale" priority="1">)",
        "retry output should retain the first conditional format");
    check_contains(worksheet_xml, R"(<cfRule type="dataBar" priority="2">)",
        "failed staging should not consume priority before retry");
    check(worksheet_xml.find(R"(priority="3")") == std::string::npos,
        "failed staging should not leave a hidden conditional-format rule");
}

void test_priority_overflow_fails_without_state_pollution()
{
    const std::filesystem::path source = write_source_with_conditional_formatting(
        "fastxlsx-workbook-editor-conditional-formatting-overflow-source.xlsx");
    auto entries = fastxlsx::test::read_zip_entries(source);
    replace_first_or_throw(entries.at("xl/worksheets/sheet1.xml"),
        R"(priority="1")", R"(priority="4294967295")");
    fastxlsx::test::write_stored_zip_entries(source, entries);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(threw_fastxlsx_error([&] {
        editor.add_conditional_data_bar(
            "Data", fastxlsx::CellRange {2, 3, 10, 3}, {});
    }), "maximum source priority should reject an unrepresentable next priority");
    check(!editor.has_pending_changes() && !editor.has_unsaved_changes()
            && editor.pending_worksheet_edits().empty(),
        "priority overflow should fail before package and public state publication");
}

void test_source_structure_audit_and_encoded_priority()
{
    const std::filesystem::path baseline = write_source_with_conditional_formatting(
        "fastxlsx-workbook-editor-conditional-formatting-audit-baseline.xlsx");
    const auto baseline_entries = fastxlsx::test::read_zip_entries(baseline);

    expect_conditional_format_source_rejection(baseline_entries,
        "fastxlsx-workbook-editor-conditional-formatting-wrong-root-namespace.xlsx",
        [](std::string& worksheet_xml) {
            replace_first_or_throw(worksheet_xml,
                "http://schemas.openxmlformats.org/spreadsheetml/2006/main",
                "urn:fastxlsx:not-spreadsheetml");
        },
        "foreign worksheet root namespace");

    expect_conditional_format_source_rejection(baseline_entries,
        "fastxlsx-workbook-editor-conditional-formatting-wrong-sheet-data-qname.xlsx",
        [](std::string& worksheet_xml) {
            replace_first_or_throw(worksheet_xml, "<sheetData>",
                R"(<x:sheetData xmlns:x="urn:fastxlsx:foreign">)");
            replace_first_or_throw(
                worksheet_xml, "</sheetData>", "</x:sheetData>");
        },
        "foreign sheetData QName");

    expect_conditional_format_source_rejection(baseline_entries,
        "fastxlsx-workbook-editor-conditional-formatting-mismatched-root-close.xlsx",
        [](std::string& worksheet_xml) {
            replace_first_or_throw(
                worksheet_xml, "</worksheet>", "</x:worksheet>");
        },
        "mismatched worksheet closing QName");

    expect_conditional_format_source_rejection(baseline_entries,
        "fastxlsx-workbook-editor-conditional-formatting-foreign-container.xlsx",
        [](std::string& worksheet_xml) {
            replace_first_or_throw(worksheet_xml, "<conditionalFormatting ",
                R"(<x:conditionalFormatting xmlns:x="urn:fastxlsx:foreign" )");
            replace_first_or_throw(worksheet_xml, "</conditionalFormatting>",
                "</x:conditionalFormatting>");
        },
        "foreign conditionalFormatting QName");

    expect_conditional_format_source_rejection(baseline_entries,
        "fastxlsx-workbook-editor-conditional-formatting-direct-child.xlsx",
        [](std::string& worksheet_xml) {
            replace_first_or_throw(worksheet_xml,
                "</cfRule></conditionalFormatting>",
                "</cfRule><extLst/></conditionalFormatting>");
        },
        "unsupported conditionalFormatting direct child");

    expect_conditional_format_source_rejection(baseline_entries,
        "fastxlsx-workbook-editor-conditional-formatting-schema-order.xlsx",
        [](std::string& worksheet_xml) {
            const std::string conditional_formatting = xml_element_fragment(
                worksheet_xml, "<conditionalFormatting", "</conditionalFormatting>");
            const std::string data_validations = xml_element_fragment(
                worksheet_xml, "<dataValidations", "</dataValidations>");
            const std::size_t conditional_formatting_offset =
                worksheet_xml.find(conditional_formatting);
            const std::size_t data_validations_offset =
                worksheet_xml.find(data_validations);
            if (conditional_formatting_offset == std::string::npos
                || data_validations_offset
                    != conditional_formatting_offset + conditional_formatting.size()) {
                throw std::runtime_error(
                    "conditional-format schema-order fixture is not contiguous");
            }
            worksheet_xml.replace(conditional_formatting_offset,
                conditional_formatting.size() + data_validations.size(),
                data_validations + conditional_formatting);
        },
        "out-of-order worksheet suffix metadata");

    expect_conditional_format_source_rejection(baseline_entries,
        "fastxlsx-workbook-editor-conditional-formatting-missing-priority.xlsx",
        [](std::string& worksheet_xml) {
            replace_first_or_throw(worksheet_xml, R"( priority="1")", "");
        },
        "missing conditional-format priority");

    expect_conditional_format_source_rejection(baseline_entries,
        "fastxlsx-workbook-editor-conditional-formatting-duplicate-priority-attribute.xlsx",
        [](std::string& worksheet_xml) {
            replace_first_or_throw(worksheet_xml, R"(priority="1")",
                R"(priority="1" priority="2")");
        },
        "duplicate conditional-format priority attribute");

    expect_conditional_format_source_rejection(baseline_entries,
        "fastxlsx-workbook-editor-conditional-formatting-invalid-encoded-priority.xlsx",
        [](std::string& worksheet_xml) {
            replace_first_or_throw(
                worksheet_xml, R"(priority="1")", R"(priority="&#x41;")");
        },
        "non-decimal encoded conditional-format priority");

    expect_conditional_format_source_rejection(baseline_entries,
        "fastxlsx-workbook-editor-conditional-formatting-invalid-uppercase-charref.xlsx",
        [](std::string& worksheet_xml) {
            replace_first_or_throw(
                worksheet_xml, R"(priority="1")", R"(priority="&#X31;")");
        },
        "invalid uppercase hexadecimal CharRef marker");

    const auto verify_encoded_priority = [&](std::string_view encoded_priority,
                                             std::string_view artifact_label) {
        auto encoded_entries = baseline_entries;
        replace_first_or_throw(encoded_entries.at("xl/worksheets/sheet1.xml"),
            R"(priority="1")", "priority=\"" + std::string(encoded_priority) + "\"");
        const std::filesystem::path encoded_source = artifact(
            "fastxlsx-workbook-editor-conditional-formatting-encoded-"
            + std::string(artifact_label) + "-priority-source.xlsx");
        fastxlsx::test::write_stored_zip_entries(encoded_source, encoded_entries);

        fastxlsx::WorkbookEditor encoded_editor =
            fastxlsx::WorkbookEditor::open(encoded_source);
        encoded_editor.add_conditional_data_bar(
            "Data", fastxlsx::CellRange {2, 3, 10, 3}, {});
        const std::filesystem::path encoded_output = artifact(
            "fastxlsx-workbook-editor-conditional-formatting-encoded-"
            + std::string(artifact_label) + "-priority-output.xlsx");
        encoded_editor.save_as(encoded_output);

        const auto encoded_output_entries =
            fastxlsx::test::read_zip_entries(encoded_output);
        const std::string& encoded_output_xml =
            encoded_output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(encoded_output_xml,
            "priority=\"" + std::string(encoded_priority) + "\"",
            std::string(artifact_label)
                + " encoded source priority should remain byte-exact");
        check_contains(encoded_output_xml,
            R"(<cfRule type="dataBar" priority="2">)",
            std::string(artifact_label)
                + " encoded source priority should allocate the next numeric priority");
    };

    verify_encoded_priority("&#49;", "decimal");
    verify_encoded_priority("&#x31;", "hexadecimal");
}

void test_preserves_opaque_advanced_multi_rule_source()
{
    const std::filesystem::path source = write_source_with_conditional_formatting(
        "fastxlsx-workbook-editor-conditional-formatting-opaque-source.xlsx");
    auto entries = fastxlsx::test::read_zip_entries(source);
    const std::string opaque_rule =
        R"(<cfRule type="cellIs" operator="greaterThan" dxfId="4" priority="9"><formula>A2&amp;B2&gt;5</formula></cfRule>)";
    replace_first_or_throw(entries.at("xl/worksheets/sheet1.xml"),
        "</colorScale></cfRule></conditionalFormatting>",
        "</colorScale></cfRule>" + opaque_rule + "</conditionalFormatting>");
    fastxlsx::test::write_stored_zip_entries(source, entries);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.add_conditional_data_bar(
        "Data", fastxlsx::CellRange {2, 3, 10, 3}, {});
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-conditional-formatting-opaque-output.xlsx");
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& worksheet_xml =
        output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, opaque_rule,
        "advanced source rule payload should remain byte-exact and opaque");
    check_contains(worksheet_xml, R"(<cfRule type="dataBar" priority="10">)",
        "new rule priority should include opaque rules in the source maximum");
    check(count_occurrences(worksheet_xml, "<conditionalFormatting ") == 2
            && count_occurrences(worksheet_xml, "<cfRule ") == 3,
        "multiple source rules should remain in their original container before one appended container");

    const std::filesystem::path duplicate_source = write_source_with_conditional_formatting(
        "fastxlsx-workbook-editor-conditional-formatting-duplicate-priority-source.xlsx");
    auto duplicate_entries = fastxlsx::test::read_zip_entries(duplicate_source);
    replace_first_or_throw(duplicate_entries.at("xl/worksheets/sheet1.xml"),
        "</colorScale></cfRule></conditionalFormatting>",
        R"(</colorScale></cfRule><cfRule type="expression" priority="1"><formula>A2&gt;0</formula></cfRule></conditionalFormatting>)");
    fastxlsx::test::write_stored_zip_entries(duplicate_source, duplicate_entries);
    fastxlsx::WorkbookEditor duplicate_editor =
        fastxlsx::WorkbookEditor::open(duplicate_source);
    check(threw_fastxlsx_error([&] {
        duplicate_editor.add_conditional_data_bar(
            "Data", fastxlsx::CellRange {2, 3, 10, 3}, {});
    }), "duplicate source priority should fail instead of being renumbered");
    check(!duplicate_editor.has_pending_changes()
            && duplicate_editor.pending_worksheet_edits().empty(),
        "duplicate source priority should fail before state publication");
}

void test_remove_conditional_format_lifecycle()
{
    const std::filesystem::path source = write_source_with_conditional_formatting(
        "fastxlsx-workbook-editor-conditional-formatting-removal-source.xlsx");
    auto source_entries = fastxlsx::test::read_zip_entries(source);
    source_entries.emplace("custom/conditional-format-removal-opaque.bin", "keep");
    fastxlsx::test::write_stored_zip_entries(source, source_entries);
    source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string source_sheet_data = xml_element_fragment(
        source_entries.at("xl/worksheets/sheet1.xml"), "<sheetData", "</sheetData>");
    const std::string source_relationships =
        source_entries.at("xl/worksheets/_rels/sheet1.xml.rels");
    const std::string source_content_types = source_entries.at("[Content_Types].xml");
    const std::string source_styles = source_entries.at("xl/styles.xml");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(threw_fastxlsx_error([&] { editor.remove_conditional_format("Data", 1); }),
        "out-of-range conditional-format removal should fail");
    check(!editor.has_pending_changes() && editor.pending_worksheet_edits().empty(),
        "out-of-range removal should not publish state");
    editor.remove_conditional_format("Data", 0);
    const auto* removal_summary = find_edit_summary(
        editor.pending_worksheet_edits(), "Data");
    check(removal_summary != nullptr
            && removal_summary->conditional_format_count == 0
            && removal_summary->conditional_format_removal_count == 1,
        "source conditional-format removal should expose an independent diagnostic count");

    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-conditional-formatting-removal-output.xlsx");
    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& output_worksheet = output_entries.at("xl/worksheets/sheet1.xml");
    check(count_occurrences(output_worksheet, "<conditionalFormatting ") == 0,
        "removing the final conditional-format should remove its complete container");
    check(xml_element_fragment(output_worksheet, "<sheetData", "</sheetData>")
            == source_sheet_data,
        "conditional-format removal should preserve cell XML exactly");
    check(output_entries.at("xl/worksheets/_rels/sheet1.xml.rels") == source_relationships,
        "conditional-format removal should preserve worksheet relationships");
    check(output_entries.at("[Content_Types].xml") == source_content_types,
        "conditional-format removal should preserve content types");
    check(output_entries.at("xl/styles.xml") == source_styles,
        "conditional-format removal should preserve styles and dxfs");
    check(output_entries.at("custom/conditional-format-removal-opaque.bin") == "keep",
        "conditional-format removal should preserve unknown package entries");

    fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(output);
    check(reader.read_worksheet_conditional_formats("Data", {}).conditional_format_count == 0,
        "reopened worksheet should expose no conditional formats after removal");

    fastxlsx::WorkbookEditor sequence_editor = fastxlsx::WorkbookEditor::open(source);
    sequence_editor.add_conditional_data_bar(
        "Data", fastxlsx::CellRange {1, 3, 2, 3}, {});
    sequence_editor.add_conditional_icon_set(
        "Data", fastxlsx::CellRange {1, 4, 2, 4}, {});
    sequence_editor.remove_conditional_format("Data", 1);
    sequence_editor.remove_conditional_format("Data", 1);
    sequence_editor.remove_conditional_format("Data", 0);
    const auto* sequence_summary = find_edit_summary(
        sequence_editor.pending_worksheet_edits(), "Data");
    check(sequence_summary != nullptr
            && sequence_summary->conditional_format_count == 2
            && sequence_summary->conditional_format_removal_count == 3,
        "same-session additions and removals should follow the current effective index");
    const std::filesystem::path sequence_output = artifact(
        "fastxlsx-workbook-editor-conditional-formatting-removal-sequence-output.xlsx");
    sequence_editor.save_as(sequence_output);
    check(count_occurrences(
            fastxlsx::test::read_zip_entries(sequence_output).at("xl/worksheets/sheet1.xml"),
            "<conditionalFormatting ") == 0,
        "effective-index removal should remove each added and source container");

    fastxlsx::WorkbookEditor added_editor = fastxlsx::WorkbookEditor::open(source);
    added_editor.add_worksheet("Added");
    fastxlsx::TwoColorScaleRule added_scale;
    added_editor.add_conditional_color_scale(
        "Added", fastxlsx::CellRange {1, 1, 2, 1}, added_scale);
    added_editor.rename_sheet("Added", "Renamed Added");
    added_editor.remove_conditional_format("Renamed Added", 0);
    const auto* added_summary = find_edit_summary(
        added_editor.pending_worksheet_edits(), "Renamed Added");
    check(added_summary != nullptr && added_summary->added
            && added_summary->conditional_format_removal_count == 1,
        "conditional-format removal should support renamed added worksheets");

    fastxlsx::WorkbookEditor retry_editor = fastxlsx::WorkbookEditor::open(source);
    {
        ScopedConditionalFormatReplacementStagedHook hook(
            fail_after_conditional_format_staging);
        check(threw_fastxlsx_error([&] {
            retry_editor.remove_conditional_format("Data", 0);
        }), "injected conditional-format removal staging failure should escape");
    }
    check(retry_editor.pending_worksheet_edits().empty()
            && !retry_editor.has_pending_changes(),
        "removal staging failure should not publish diagnostics or package state");
    retry_editor.remove_conditional_format("Data", 0);
    check(retry_editor.pending_worksheet_edits().front()
            .conditional_format_removal_count == 1,
        "conditional-format removal should remain retryable after staging failure");

    auto unsupported_entries = source_entries;
    replace_first_or_throw(unsupported_entries.at("xl/worksheets/sheet1.xml"),
        "</colorScale></cfRule></conditionalFormatting>",
        R"(</colorScale></cfRule><cfRule type="cellIs" priority="9"><formula>A1&gt;0</formula></cfRule></conditionalFormatting>)");
    const std::filesystem::path unsupported_source = artifact(
        "fastxlsx-workbook-editor-conditional-formatting-removal-unsupported.xlsx");
    fastxlsx::test::write_stored_zip_entries(unsupported_source, unsupported_entries);
    fastxlsx::WorkbookEditor unsupported_editor =
        fastxlsx::WorkbookEditor::open(unsupported_source);
    check(threw_fastxlsx_error([&] {
        unsupported_editor.remove_conditional_format("Data", 0);
    }), "multiple-rule or advanced conditional-format removal should fail strictly");
    check(unsupported_editor.pending_worksheet_edits().empty()
            && !unsupported_editor.has_pending_changes(),
        "unsupported conditional-format removal should not publish state");
}

} // namespace

int main()
{
    try {
        test_append_all_rule_kinds_priority_and_preservation();
        test_rename_added_worksheet_and_reopen();
        test_invalid_input_staging_failure_and_save_retry();
        test_priority_overflow_fails_without_state_pollution();
        test_source_structure_audit_and_encoded_priority();
        test_preserves_opaque_advanced_multi_rule_source();
        test_remove_conditional_format_lifecycle();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr,
            "%d WorkbookEditor conditional formatting checks failed\n", g_failures);
        return 1;
    }
    std::puts("All WorkbookEditor conditional formatting tests passed");
    return 0;
}
