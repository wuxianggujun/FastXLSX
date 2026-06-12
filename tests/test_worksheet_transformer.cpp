#include <fastxlsx/detail/worksheet_transformer.hpp>

#include <algorithm>
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

std::vector<std::string_view> split_every(const std::string& xml, std::size_t width)
{
    std::vector<std::string_view> chunks;
    for (std::size_t position = 0; position < xml.size(); position += width) {
        const std::size_t length = std::min(width, xml.size() - position);
        chunks.emplace_back(xml.data() + position, length);
    }
    return chunks;
}

EmittedWorksheet emit_chunked_worksheet(const std::string& xml,
    std::span<const WorksheetCellReplacement> replacements,
    std::size_t chunk_width,
    fastxlsx::detail::WorksheetEventReaderOptions reader_options = {})
{
    const std::vector<std::string_view> chunks = split_every(xml, chunk_width);
    EmittedWorksheet emitted;
    emitted.summary = fastxlsx::detail::emit_cell_replacement_worksheet_from_chunks(
        chunks, replacements, [&](std::string_view chunk) { emitted.xml += chunk; }, reader_options);
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

bool contains_text(std::string_view haystack, std::string_view needle)
{
    return haystack.find(needle) != std::string_view::npos;
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

bool collect_actions_fails(
    const std::string& xml, std::span<const WorksheetCellReplacement> replacements)
{
    try {
        (void)collect_actions(xml, replacements);
    } catch (const std::exception&) {
        return true;
    }
    return false;
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

void test_transformer_validates_replacement_cell_payload_root_and_reference()
{
    const std::string xml =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData></worksheet>)";

    const std::array non_cell_root {
        WorksheetCellReplacement { "A1", R"(<row r="1"><c r="A1"><v>2</v></c></row>)" },
    };
    check(collect_actions_fails(xml, non_cell_root),
        "replacement payload root should be a cell element");

    const std::array missing_reference {
        WorksheetCellReplacement { "A1", R"(<c><v>2</v></c>)" },
    };
    check(collect_actions_fails(xml, missing_reference),
        "replacement payload should include an unqualified r attribute");

    const std::array qualified_reference_only {
        WorksheetCellReplacement { "A1", R"(<c xmlns:x="urn:test" x:r="A1"><v>2</v></c>)" },
    };
    check(collect_actions_fails(xml, qualified_reference_only),
        "replacement payload should not accept qualified r attributes");

    const std::array mismatched_reference {
        WorksheetCellReplacement { "A1", R"(<c r="B1"><v>2</v></c>)" },
    };
    check(collect_actions_fails(xml, mismatched_reference),
        "replacement payload r attribute should match the selector");

    const std::array prefixed_cell {
        WorksheetCellReplacement {
            "A1", R"(<x:c xmlns:x="urn:test" r='A1'><x:v>2</x:v></x:c>)" },
    };
    const CapturedTransform captured = collect_actions(xml, prefixed_cell);
    check(captured.summary.matched_replacement_count == 1,
        "prefixed replacement cell element should be accepted by local-name");
    check(find_replace(captured.actions, "A1").replacement_cell_xml == prefixed_cell[0].replacement_cell_xml,
        "accepted prefixed payload should be forwarded unchanged");
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

void test_transformer_chunked_emitter_matches_full_emitter_across_boundaries()
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

    const EmittedWorksheet full = emit_worksheet(xml, replacements);
    const EmittedWorksheet chunked = emit_chunked_worksheet(xml, replacements, 5);

    check(chunked.xml == full.xml,
        "chunked output emitter should match full-buffer output across token boundaries");
    check(chunked.summary.matched_replacement_count == full.summary.matched_replacement_count,
        "chunked output emitter should preserve matched replacement count");
    check(chunked.summary.missing_cell_references == full.summary.missing_cell_references,
        "chunked output emitter should preserve missing replacement diagnostics");
}

void test_transformer_chunked_emitter_uses_bounded_window_not_full_document()
{
    std::string xml = R"(<worksheet><sheetData>)";
    for (int row = 1; row <= 24; ++row) {
        xml += R"(<row r=")";
        xml += std::to_string(row);
        xml += R"("><c r="A)";
        xml += std::to_string(row);
        xml += R"("><v>old-)";
        xml += std::to_string(row);
        xml += R"(</v></c></row>)";
    }
    xml += R"(</sheetData></worksheet>)";

    const std::string replacement_xml = R"(<c r="A17"><v>new-17</v></c>)";
    const std::array replacements {
        WorksheetCellReplacement { "A17", replacement_xml },
    };

    fastxlsx::detail::WorksheetEventReaderOptions options;
    options.max_window_bytes = 96;
    const EmittedWorksheet emitted = emit_chunked_worksheet(xml, replacements, 7, options);

    check(xml.size() > options.max_window_bytes,
        "chunked transformer fixture should be larger than the retained window");
    check(emitted.summary.matched_replacement_count == 1,
        "chunked transformer should match the replacement inside a bounded window");
    check(emitted.summary.missing_cell_references.empty(),
        "chunked transformer should not report matched replacement as missing");
    check(contains_text(emitted.xml, replacement_xml),
        "chunked transformer output should include the replacement cell");
    check(!contains_text(emitted.xml, R"(<c r="A17"><v>old-17</v></c>)"),
        "chunked transformer output should consume the old target cell");
    check(contains_text(emitted.xml, R"(<c r="A16"><v>old-16</v></c>)"),
        "chunked transformer output should preserve adjacent non-target cells");
}

void test_transformer_chunked_emitter_reports_missing_and_rejects_oversized_input()
{
    const std::string xml =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData></worksheet>)";
    const std::array missing_replacements {
        WorksheetCellReplacement { "C3", R"(<c r="C3"><v>missing</v></c>)" },
    };
    const EmittedWorksheet missing = emit_chunked_worksheet(xml, missing_replacements, 4);

    check(missing.xml == xml, "missing chunked replacement should pass through source XML");
    check(missing.summary.matched_replacement_count == 0,
        "missing chunked replacement should not count as matched");
    check(missing.summary.missing_cell_references.size() == 1,
        "missing chunked replacement should be reported");
    check(missing.summary.missing_cell_references[0] == "C3",
        "missing chunked replacement should retain caller reference");

    const std::string oversized_xml =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>)" + std::string(80, 'x')
        + R"(</v></c></row></sheetData></worksheet>)";
    const std::array replacements {
        WorksheetCellReplacement { "B1", R"(<c r="B1"><v>new</v></c>)" },
    };
    fastxlsx::detail::WorksheetEventReaderOptions options;
    options.max_window_bytes = 32;

    bool failed = false;
    try {
        (void)emit_chunked_worksheet(oversized_xml, replacements, 9, options);
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed,
        "chunked transformer should propagate oversized event-reader window failures");
}

} // namespace

int main()
{
    try {
        test_transformer_emits_replace_cell_action_and_skips_original_cell_payload();
        test_transformer_orders_replacements_by_source_xml();
        test_transformer_reports_missing_and_rejects_invalid_replacements();
        test_transformer_validates_replacement_cell_payload_root_and_reference();
        test_transformer_emits_rewritten_chunks_with_raw_text_preserved();
        test_transformer_chunked_emitter_matches_full_emitter_across_boundaries();
        test_transformer_chunked_emitter_uses_bounded_window_not_full_document();
        test_transformer_chunked_emitter_reports_missing_and_rejects_oversized_input();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
