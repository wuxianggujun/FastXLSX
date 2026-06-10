#include <fastxlsx/detail/worksheet_transformer.hpp>

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
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

using fastxlsx::detail::WorksheetCellReplacement;
using fastxlsx::detail::WorksheetTransformAction;
using fastxlsx::detail::WorksheetTransformActionKind;
using fastxlsx::detail::WorksheetTransformSummary;

struct CapturedTransform {
    WorksheetTransformSummary summary;
    std::vector<WorksheetTransformAction> actions;
};

struct EmittedWorksheet {
    WorksheetTransformSummary summary;
    std::string xml;
};

CapturedTransform collect_actions(
    const std::string& xml, std::span<const WorksheetCellReplacement> replacements)
{
    CapturedTransform captured;
    captured.summary = fastxlsx::detail::scan_cell_replacement_actions(
        xml, replacements, [&](const WorksheetTransformAction& action) {
            captured.actions.push_back(action);
        });
    return captured;
}

EmittedWorksheet emit_worksheet(
    const std::string& xml, std::span<const WorksheetCellReplacement> replacements)
{
    EmittedWorksheet emitted;
    emitted.summary = fastxlsx::detail::emit_cell_replacement_worksheet(
        xml, replacements, [&](std::string_view chunk) { emitted.xml += chunk; });
    return emitted;
}

std::vector<std::string_view> replacement_order(const std::vector<WorksheetTransformAction>& actions)
{
    std::vector<std::string_view> order;
    for (const WorksheetTransformAction& action : actions) {
        if (action.kind == WorksheetTransformActionKind::ReplaceCell) {
            order.push_back(action.cell_reference);
        }
    }
    return order;
}

bool has_pass_through_raw(
    const std::vector<WorksheetTransformAction>& actions, std::string_view raw)
{
    for (const WorksheetTransformAction& action : actions) {
        if (action.kind == WorksheetTransformActionKind::PassThrough && action.raw_xml == raw) {
            return true;
        }
    }
    return false;
}

const WorksheetTransformAction& find_replace(
    const std::vector<WorksheetTransformAction>& actions, std::string_view cell_reference)
{
    for (const WorksheetTransformAction& action : actions) {
        if (action.kind == WorksheetTransformActionKind::ReplaceCell
            && action.cell_reference == cell_reference) {
            return action;
        }
    }
    throw TestFailure("expected replacement action not found");
}

void test_transformer_emits_replace_cell_action_and_skips_original_cell_payload()
{
    const std::string xml =
        R"(<worksheet><sheetData><row r="1">)"
        R"(<c r="A1"><v>old-a</v></c>)"
        R"(<c r="B1"><v>old-b</v></c>)"
        R"(</row></sheetData></worksheet>)";
    const std::string replacement_xml =
        R"(<c r="B1" t="inlineStr"><is><t>new-b</t></is></c>)";
    const std::array replacements {
        WorksheetCellReplacement { "B1", replacement_xml },
    };

    const CapturedTransform captured = collect_actions(xml, replacements);

    check(captured.summary.matched_replacement_count == 1,
        "transformer should report one matched replacement");
    check(captured.summary.missing_cell_references.empty(),
        "transformer should not report missing matched replacement");

    const WorksheetTransformAction& replacement = find_replace(captured.actions, "B1");
    check(replacement.row_number == "1", "replacement action should expose source row");
    check(replacement.replacement_cell_xml == replacement_xml,
        "replacement action should carry caller cell XML");

    check(has_pass_through_raw(captured.actions, "old-a"),
        "non-target cell value should remain pass-through");
    check(!has_pass_through_raw(captured.actions, "old-b"),
        "target cell original value should be consumed by replacement action");
}

void test_transformer_orders_replacements_by_source_xml()
{
    const std::string xml =
        R"(<worksheet><sheetData><row r="1">)"
        R"(<c r="A1"><v>1</v></c>)"
        R"(<c r="B1"><v>2</v></c>)"
        R"(<c r="C1"><v>3</v></c>)"
        R"(</row></sheetData></worksheet>)";
    const std::array replacements {
        WorksheetCellReplacement { "C1", R"(<c r="C1"><v>30</v></c>)" },
        WorksheetCellReplacement { "A1", R"(<c r="A1"><v>10</v></c>)" },
    };

    const CapturedTransform captured = collect_actions(xml, replacements);
    const std::vector<std::string_view> order = replacement_order(captured.actions);

    check(order.size() == 2, "transformer should emit two replacement actions");
    check(order[0] == "A1", "first replacement should follow source order");
    check(order[1] == "C1", "second replacement should follow source order");
    check(captured.summary.matched_replacement_count == 2,
        "transformer should count both matched replacements");
}

void test_transformer_reports_missing_and_rejects_invalid_replacements()
{
    const std::string xml =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData></worksheet>)";
    const std::array missing_replacements {
        WorksheetCellReplacement { "C3", R"(<c r="C3"><v>missing</v></c>)" },
    };
    const CapturedTransform captured = collect_actions(xml, missing_replacements);

    check(captured.summary.matched_replacement_count == 0,
        "missing target should not count as matched");
    check(captured.summary.missing_cell_references.size() == 1,
        "missing target should be reported");
    check(captured.summary.missing_cell_references[0] == "C3",
        "missing target should retain caller reference");

    bool duplicate_failed = false;
    try {
        const std::array duplicate_replacements {
            WorksheetCellReplacement { "A1", R"(<c r="A1"><v>1</v></c>)" },
            WorksheetCellReplacement { "A1", R"(<c r="A1"><v>2</v></c>)" },
        };
        (void)collect_actions(xml, duplicate_replacements);
    } catch (const std::exception&) {
        duplicate_failed = true;
    }
    check(duplicate_failed, "duplicate replacement selectors should fail preflight");

    bool empty_payload_failed = false;
    try {
        const std::array empty_replacements {
            WorksheetCellReplacement { "A1", "" },
        };
        (void)collect_actions(xml, empty_replacements);
    } catch (const std::exception&) {
        empty_payload_failed = true;
    }
    check(empty_payload_failed, "empty replacement payload should fail preflight");
}

void test_transformer_emits_rewritten_chunks_with_raw_text_preserved()
{
    const std::string xml =
        "<?xml version=\"1.0\"?>\n"
        "<worksheet>\n"
        "  <sheetData>\n"
        "    <row r=\"1\"><c r=\"A1\"><v>old-a</v></c><c r=\"B1\"><v>old-b</v></c></row>\n"
        "  </sheetData>\n"
        "</worksheet>";
    const std::string replacement_xml =
        R"(<c r="B1" t="inlineStr"><is><t>new-b</t></is></c>)";
    const std::array replacements {
        WorksheetCellReplacement { "B1", replacement_xml },
    };

    const EmittedWorksheet emitted = emit_worksheet(xml, replacements);
    const std::string expected =
        "<?xml version=\"1.0\"?>\n"
        "<worksheet>\n"
        "  <sheetData>\n"
        "    <row r=\"1\"><c r=\"A1\"><v>old-a</v></c>"
        "<c r=\"B1\" t=\"inlineStr\"><is><t>new-b</t></is></c></row>\n"
        "  </sheetData>\n"
        "</worksheet>";

    check(emitted.xml == expected,
        "output emitter should preserve pass-through raw text and replace target cell");
    check(emitted.summary.matched_replacement_count == 1,
        "output emitter should return transform summary");
    check(emitted.summary.missing_cell_references.empty(),
        "output emitter should not report matched target as missing");
}

} // namespace

int main()
{
    try {
        test_transformer_emits_replace_cell_action_and_skips_original_cell_payload();
        test_transformer_orders_replacements_by_source_xml();
        test_transformer_reports_missing_and_rejects_invalid_replacements();
        test_transformer_emits_rewritten_chunks_with_raw_text_preserved();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
