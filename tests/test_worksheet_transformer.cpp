#include <fastxlsx/detail/worksheet_transformer.hpp>

#include <algorithm>
#include <array>
#include <iostream>
#include <span>
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
using fastxlsx::detail::WorksheetCellReplacementPayload;
using fastxlsx::detail::WorksheetCellReplacementPlan;
using fastxlsx::detail::WorksheetTransformAction;
using fastxlsx::detail::WorksheetTransformActionKind;
using fastxlsx::detail::WorksheetTransformSummary;

struct CapturedTransform {
    WorksheetTransformSummary summary;
    struct Action {
        WorksheetTransformActionKind kind = WorksheetTransformActionKind::PassThrough;
        std::string raw_xml;
        std::string row_number;
        std::string cell_reference;
        std::string replacement_payload_xml;
    };
    std::vector<Action> actions;
};

struct EmittedWorksheet {
    WorksheetTransformSummary summary;
    std::string xml;
};

fastxlsx::detail::WorksheetInputChunkCallback make_string_chunk_source(
    const std::string& xml, std::size_t chunk_width)
{
    if (chunk_width == 0) {
        throw TestFailure("test chunk width must be nonzero");
    }

    return [&xml, chunk_width, position = std::size_t {0}](
               std::string& chunk) mutable {
        if (position >= xml.size()) {
            chunk.clear();
            return false;
        }
        const std::size_t length = std::min(chunk_width, xml.size() - position);
        chunk.assign(xml.data() + position, length);
        position += length;
        return true;
    };
}

WorksheetCellReplacement cell_replacement(
    std::string_view cell_reference, std::string_view materialized_xml)
{
    return WorksheetCellReplacement {
        cell_reference,
        WorksheetCellReplacementPayload::from_materialized_xml(materialized_xml),
    };
}

WorksheetCellReplacement chunked_cell_replacement(
    std::string_view cell_reference, std::span<const std::string_view> chunks)
{
    return WorksheetCellReplacement {
        cell_reference,
        WorksheetCellReplacementPayload::from_chunks(chunks),
    };
}

std::string payload_xml(const WorksheetCellReplacementPayload& payload)
{
    std::string xml;
    payload.for_each_chunk([&](std::string_view chunk) { xml += chunk; });
    return xml;
}

CapturedTransform collect_actions(
    const std::string& xml, std::span<const WorksheetCellReplacement> replacements)
{
    CapturedTransform captured;
    const WorksheetCellReplacementPlan replacement_plan =
        fastxlsx::detail::make_worksheet_cell_replacement_plan(replacements);
    fastxlsx::detail::WorksheetInputChunkCallback source =
        make_string_chunk_source(xml, std::max<std::size_t>(xml.size(), 1U));
    captured.summary = fastxlsx::detail::scan_cell_replacement_actions_from_chunk_source(
        source, replacement_plan, [&](const WorksheetTransformAction& action) {
            captured.actions.push_back(CapturedTransform::Action {
                action.kind,
                std::string(action.raw_xml),
                std::string(action.row_number),
                std::string(action.cell_reference),
                payload_xml(action.replacement_payload),
            });
        });
    return captured;
}

EmittedWorksheet emit_worksheet(
    const std::string& xml, std::span<const WorksheetCellReplacement> replacements)
{
    EmittedWorksheet emitted;
    const WorksheetCellReplacementPlan replacement_plan =
        fastxlsx::detail::make_worksheet_cell_replacement_plan(replacements);
    fastxlsx::detail::WorksheetInputChunkCallback source =
        make_string_chunk_source(xml, std::max<std::size_t>(xml.size(), 1U));
    emitted.summary = fastxlsx::detail::emit_cell_replacement_worksheet_from_chunk_source(
        source, replacement_plan, [&](std::string_view chunk) { emitted.xml += chunk; });
    return emitted;
}

EmittedWorksheet emit_chunked_worksheet(const std::string& xml,
    std::span<const WorksheetCellReplacement> replacements,
    std::size_t chunk_width,
    fastxlsx::detail::WorksheetEventReaderOptions reader_options = {})
{
    EmittedWorksheet emitted;
    const WorksheetCellReplacementPlan replacement_plan =
        fastxlsx::detail::make_worksheet_cell_replacement_plan(replacements);
    fastxlsx::detail::WorksheetInputChunkCallback source =
        make_string_chunk_source(xml, chunk_width);
    emitted.summary = fastxlsx::detail::emit_cell_replacement_worksheet_from_chunk_source(
        source, replacement_plan, [&](std::string_view chunk) { emitted.xml += chunk; }, reader_options);
    return emitted;
}

EmittedWorksheet emit_source_worksheet(const std::string& xml,
    std::span<const WorksheetCellReplacement> replacements,
    std::size_t chunk_width,
    fastxlsx::detail::WorksheetEventReaderOptions reader_options = {})
{
    EmittedWorksheet emitted;
    const WorksheetCellReplacementPlan replacement_plan =
        fastxlsx::detail::make_worksheet_cell_replacement_plan(replacements);
    fastxlsx::detail::WorksheetInputChunkCallback source =
        make_string_chunk_source(xml, chunk_width);
    emitted.summary = fastxlsx::detail::emit_cell_replacement_worksheet_from_chunk_source(
        source,
        replacement_plan,
        [&](std::string_view chunk) { emitted.xml += chunk; },
        reader_options);
    return emitted;
}

EmittedWorksheet emit_upsert_worksheet(const std::string& xml,
    std::span<const WorksheetCellReplacement> replacements,
    std::size_t chunk_width)
{
    EmittedWorksheet emitted;
    const WorksheetCellReplacementPlan replacement_plan =
        fastxlsx::detail::make_worksheet_cell_replacement_plan(replacements);
    fastxlsx::detail::WorksheetInputChunkCallback source =
        make_string_chunk_source(xml, chunk_width);
    emitted.summary = fastxlsx::detail::emit_cell_replacement_worksheet_from_chunk_source(
        source,
        replacement_plan,
        [&](std::string_view chunk) { emitted.xml += chunk; },
        {},
        fastxlsx::detail::WorksheetCellReplacementMode::ReplaceOrInsert);
    return emitted;
}

CapturedTransform collect_actions_with_plan(
    const std::string& xml,
    const WorksheetCellReplacementPlan& replacement_plan,
    std::size_t chunk_width)
{
    CapturedTransform captured;
    fastxlsx::detail::WorksheetInputChunkCallback source =
        make_string_chunk_source(xml, chunk_width);
    captured.summary = fastxlsx::detail::scan_cell_replacement_actions_from_chunk_source(
        source,
        replacement_plan,
        [&](const WorksheetTransformAction& action) {
            captured.actions.push_back(CapturedTransform::Action {
                action.kind,
                std::string(action.raw_xml),
                std::string(action.row_number),
                std::string(action.cell_reference),
                payload_xml(action.replacement_payload),
            });
        });
    return captured;
}

EmittedWorksheet emit_source_worksheet_with_plan(
    const std::string& xml,
    const WorksheetCellReplacementPlan& replacement_plan,
    std::size_t chunk_width)
{
    EmittedWorksheet emitted;
    fastxlsx::detail::WorksheetInputChunkCallback source =
        make_string_chunk_source(xml, chunk_width);
    emitted.summary = fastxlsx::detail::emit_cell_replacement_worksheet_from_chunk_source(
        source,
        replacement_plan,
        [&](std::string_view chunk) { emitted.xml += chunk; });
    return emitted;
}

std::vector<std::string> replacement_order(const std::vector<CapturedTransform::Action>& actions)
{
    std::vector<std::string> order;
    for (const CapturedTransform::Action& action : actions) {
        if (action.kind == WorksheetTransformActionKind::ReplaceCell) {
            order.push_back(action.cell_reference);
        }
    }
    return order;
}

bool has_pass_through_raw(
    const std::vector<CapturedTransform::Action>& actions, std::string_view raw)
{
    for (const CapturedTransform::Action& action : actions) {
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

const CapturedTransform::Action& find_replace(
    const std::vector<CapturedTransform::Action>& actions, std::string_view cell_reference)
{
    for (const CapturedTransform::Action& action : actions) {
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

void test_replacement_payload_replays_chunks_without_direct_materialized_accessor()
{
    const std::string replacement_xml = R"(<c r="A1"><v>42</v></c>)";
    const WorksheetCellReplacementPayload payload =
        WorksheetCellReplacementPayload::from_materialized_xml(replacement_xml);
    std::size_t chunk_count = 0;
    std::string replayed_xml;

    check(!payload.empty(), "replacement payload should report non-empty backing");
    check(payload.byte_size() == replacement_xml.size(),
        "replacement payload should expose byte size for preflight");
    payload.for_each_chunk([&](std::string_view chunk) {
        ++chunk_count;
        replayed_xml += chunk;
    });

    check(chunk_count == 1, "current materialized payload backing should replay as one chunk");
    check(replayed_xml == replacement_xml,
        "replacement payload replay should preserve caller XML exactly");
}

void test_chunked_replacement_payload_preflights_and_replays_chunks()
{
    const std::array<std::string_view, 3> replacement_chunks {
        R"(<c r="A1">)",
        R"(<v>chunked</v>)",
        R"(</c>)",
    };
    const std::array replacements {
        chunked_cell_replacement("A1", replacement_chunks),
    };
    const WorksheetCellReplacementPlan replacement_plan =
        fastxlsx::detail::make_worksheet_cell_replacement_plan(replacements);
    const auto replacement = replacement_plan.replacement_payloads_by_reference.find("A1");
    check(replacement != replacement_plan.replacement_payloads_by_reference.end(),
        "chunked replacement payload should be accepted by preflight");

    std::size_t chunk_count = 0;
    std::string replayed_xml;
    replacement->second.for_each_chunk([&](std::string_view chunk) {
        ++chunk_count;
        replayed_xml += chunk;
    });
    check(chunk_count == replacement_chunks.size(),
        "chunked replacement payload should preserve caller chunk boundaries");
    check(replayed_xml == R"(<c r="A1"><v>chunked</v></c>)",
        "chunked replacement payload replay should preserve XML bytes");

    const std::string source_xml =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>old</v></c></row></sheetData></worksheet>)";
    const EmittedWorksheet emitted =
        emit_source_worksheet_with_plan(source_xml, replacement_plan, 11);
    check(emitted.summary.matched_replacement_count == 1,
        "chunked replacement payload should still match the source cell");
    check(emitted.xml
            == R"(<worksheet><sheetData><row r="1"><c r="A1"><v>chunked</v></c></row></sheetData></worksheet>)",
        "chunked replacement payload should emit through chunk replay");
}

void test_chunked_replacement_payload_rejects_total_size_over_limit()
{
    std::string prefix = R"(<c r="A1"><v>)";
    std::string body(
        fastxlsx::detail::worksheet_replacement_cell_xml_materialization_byte_limit, 'x');
    std::string suffix = R"(</v></c>)";
    const std::array<std::string_view, 3> replacement_chunks {
        prefix,
        body,
        suffix,
    };
    const std::array replacements {
        chunked_cell_replacement("A1", replacement_chunks),
    };

    bool failed = false;
    try {
        (void)fastxlsx::detail::make_worksheet_cell_replacement_plan(replacements);
    } catch (const std::exception& error) {
        failed = true;
        check(contains_text(error.what(), "single-cell materialized payload limit"),
            "chunked oversized replacement payload should name the materialization limit");
    }
    check(failed, "chunked replacement payload total size should fail preflight");
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
        cell_replacement("B1", replacement_xml),
    };

    const CapturedTransform captured = collect_actions(xml, replacements);

    check(captured.summary.matched_replacement_count == 1,
        "transformer should report one matched replacement");
    check(captured.summary.missing_cell_references.empty(),
        "transformer should not report missing matched replacement");

    const CapturedTransform::Action& replacement = find_replace(captured.actions, "B1");
    check(replacement.row_number == "1", "replacement action should expose source row");
    check(replacement.replacement_payload_xml == replacement_xml,
        "replacement action should carry caller payload XML");

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
        cell_replacement("C1", R"(<c r="C1"><v>30</v></c>)"),
        cell_replacement("A1", R"(<c r="A1"><v>10</v></c>)"),
    };

    const CapturedTransform captured = collect_actions(xml, replacements);
    const std::vector<std::string> order = replacement_order(captured.actions);

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
        cell_replacement("C3", R"(<c r="C3"><v>missing</v></c>)"),
    };
    const CapturedTransform captured = collect_actions(xml, missing_replacements);

    check(captured.summary.matched_replacement_count == 0,
        "missing target should not count as matched");
    check(captured.summary.missing_cell_references.size() == 1,
        "missing target should be reported");
    check(captured.summary.missing_cell_references[0] == "C3",
        "missing target should retain caller reference");

    std::string missing_selector = "D4";
    std::string missing_payload = R"(<c r="D4"><v>missing</v></c>)";
    const std::array missing_view_replacements {
        cell_replacement(missing_selector, missing_payload),
    };
    const CapturedTransform missing_view_captured =
        collect_actions(xml, missing_view_replacements);
    missing_selector = "A1";
    missing_payload.clear();
    check(missing_view_captured.summary.missing_cell_references.size() == 1,
        "missing target summary should own diagnostic references");
    check(missing_view_captured.summary.missing_cell_references[0] == "D4",
        "missing target summary should not expose internal non-owning selector views");

    bool duplicate_failed = false;
    try {
        const std::array duplicate_replacements {
            cell_replacement("A1", R"(<c r="A1"><v>1</v></c>)"),
            cell_replacement("A1", R"(<c r="A1"><v>2</v></c>)"),
        };
        (void)collect_actions(xml, duplicate_replacements);
    } catch (const std::exception&) {
        duplicate_failed = true;
    }
    check(duplicate_failed, "duplicate replacement selectors should fail preflight");

    bool empty_payload_failed = false;
    try {
        const std::array empty_replacements {
            cell_replacement("A1", ""),
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
        cell_replacement("A1", R"(<row r="1"><c r="A1"><v>2</v></c></row>)"),
    };
    check(collect_actions_fails(xml, non_cell_root),
        "replacement payload root should be a cell element");

    const std::array missing_reference {
        cell_replacement("A1", R"(<c><v>2</v></c>)"),
    };
    check(collect_actions_fails(xml, missing_reference),
        "replacement payload should include an unqualified r attribute");

    const std::array qualified_reference_only {
        cell_replacement("A1", R"(<c xmlns:x="urn:test" x:r="A1"><v>2</v></c>)"),
    };
    check(collect_actions_fails(xml, qualified_reference_only),
        "replacement payload should not accept qualified r attributes");

    const std::array mismatched_reference {
        cell_replacement("A1", R"(<c r="B1"><v>2</v></c>)"),
    };
    check(collect_actions_fails(xml, mismatched_reference),
        "replacement payload r attribute should match the selector");

    struct InvalidPayloadStructureCase {
        WorksheetCellReplacement replacement;
        std::string_view expected_error;
        const char* message;
    };
    const std::array<std::string_view, 2> truncated_chunked_payload {
        R"(<c r="A1">)",
        "<v",
    };
    const std::array invalid_structure_cases {
        InvalidPayloadStructureCase {
            cell_replacement("A1", R"(<c r="A1"><v>2</v>)"),
            "closing tag is missing",
            "replacement payload should reject an unclosed cell root",
        },
        InvalidPayloadStructureCase {
            cell_replacement("A1", R"(<c r="A1"><v></c>)"),
            "closing tag does not match start tag",
            "replacement payload should reject mismatched nested tags",
        },
        InvalidPayloadStructureCase {
            cell_replacement("A1", R"(<c r="A1"/><c r="A1"/>)"),
            "multiple root elements",
            "replacement payload should reject multiple roots",
        },
        InvalidPayloadStructureCase {
            chunked_cell_replacement("A1", truncated_chunked_payload),
            "tag is truncated",
            "chunked replacement payload should reject truncated child tags",
        },
    };
    for (const InvalidPayloadStructureCase& invalid_case : invalid_structure_cases) {
        bool failed = false;
        try {
            const std::array replacements {
                invalid_case.replacement,
            };
            (void)collect_actions(xml, replacements);
        } catch (const std::exception& error) {
            failed = true;
            check(contains_text(error.what(), "replacement cell XML"),
                "replacement structure preflight error should name replacement XML");
            check(contains_text(error.what(), invalid_case.expected_error),
                "replacement structure preflight should explain the malformed XML");
        }
        check(failed, invalid_case.message);
    }

    const std::array prefixed_cell {
        cell_replacement("A1", R"(<x:c xmlns:x="urn:test" r='A1'><x:v>2</x:v></x:c>)"),
    };
    const CapturedTransform captured = collect_actions(xml, prefixed_cell);
    check(captured.summary.matched_replacement_count == 1,
        "prefixed replacement cell element should be accepted by local-name");
    check(find_replace(captured.actions, "A1").replacement_payload_xml
            == payload_xml(prefixed_cell[0].replacement_payload),
        "accepted prefixed payload should be forwarded unchanged");
}

void test_transformer_requires_preflighted_replacement_plan()
{
    const std::array invalid_replacements {
        cell_replacement("A1", R"(<not-a-cell/>)"),
    };

    bool invalid_payload_failed = false;
    try {
        (void)fastxlsx::detail::make_worksheet_cell_replacement_plan(invalid_replacements);
    } catch (const std::exception& error) {
        invalid_payload_failed = true;
        check(contains_text(error.what(), "root must be a cell element"),
            "replacement plan preflight should report invalid cell payloads");
    }
    check(invalid_payload_failed,
        "replacement plan preflight should be the only path for validating payloads");
}

void test_transformer_reuses_prevalidated_replacement_plan_for_scan_and_emit()
{
    const std::string xml =
        R"(<worksheet><sheetData><row r="1">)"
        R"(<c r="A1"><v>old-a</v></c>)"
        R"(<c r="B1"><v>old-b</v></c>)"
        R"(</row></sheetData></worksheet>)";
    const std::string replacement_a = R"(<c r="A1"><v>10</v></c>)";
    const std::string replacement_b = R"(<c r="B1" t="inlineStr"><is><t>bee</t></is></c>)";
    const std::array replacements {
        cell_replacement("B1", replacement_b),
        cell_replacement("A1", replacement_a),
    };
    const WorksheetCellReplacementPlan replacement_plan =
        fastxlsx::detail::make_worksheet_cell_replacement_plan(replacements);

    const CapturedTransform captured = collect_actions_with_plan(xml, replacement_plan, 6);
    const EmittedWorksheet emitted = emit_source_worksheet_with_plan(xml, replacement_plan, 5);

    check(captured.summary.matched_replacement_count == 2,
        "reused replacement plan should match both cells during scan");
    check(captured.summary.missing_cell_references.empty(),
        "reused replacement plan scan should not report missing cells");
    check(replacement_order(captured.actions) == std::vector<std::string>({"A1", "B1"}),
        "reused replacement plan scan should keep source-order actions");
    check(emitted.summary.matched_replacement_count == 2,
        "reused replacement plan should match both cells during emit");
    check(emitted.summary.missing_cell_references.empty(),
        "reused replacement plan emit should not report missing cells");
    check(contains_text(emitted.xml, replacement_a),
        "reused replacement plan output should include first replacement payload");
    check(contains_text(emitted.xml, replacement_b),
        "reused replacement plan output should include second replacement payload");
    check(!contains_text(emitted.xml, "old-a") && !contains_text(emitted.xml, "old-b"),
        "reused replacement plan output should consume original target payloads");
}

void test_transformer_rejects_oversized_materialized_replacement_cell_payload()
{
    const std::string xml =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData></worksheet>)";
    std::string oversized_payload = R"(<c r="A1"><v>)";
    oversized_payload.append(
        fastxlsx::detail::worksheet_replacement_cell_xml_materialization_byte_limit, 'x');
    oversized_payload += R"(</v></c>)";
    const std::array replacements {
        cell_replacement("A1", oversized_payload),
    };

    bool failed = false;
    try {
        (void)collect_actions(xml, replacements);
    } catch (const std::exception& error) {
        failed = true;
        check(contains_text(error.what(), "single-cell materialized payload limit"),
            "oversized replacement cell payload error should name the materialization limit");
    }
    check(failed, "oversized replacement cell payload should fail preflight");
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
        cell_replacement("B1", replacement_xml),
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

void test_transformer_chunked_emitter_matches_single_chunk_emitter_across_boundaries()
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
        cell_replacement("B1", replacement_xml),
    };

    const EmittedWorksheet single_chunk = emit_worksheet(xml, replacements);
    const EmittedWorksheet chunked = emit_chunked_worksheet(xml, replacements, 5);

    check(chunked.xml == single_chunk.xml,
        "chunked output emitter should match single-chunk output across token boundaries");
    check(chunked.summary.matched_replacement_count == single_chunk.summary.matched_replacement_count,
        "chunked output emitter should preserve matched replacement count");
    check(chunked.summary.missing_cell_references == single_chunk.summary.missing_cell_references,
        "chunked output emitter should preserve missing replacement diagnostics");
}

void test_transformer_chunk_source_emitter_matches_single_chunk_emitter_across_boundaries()
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
        cell_replacement("B1", replacement_xml),
    };

    const EmittedWorksheet single_chunk = emit_worksheet(xml, replacements);
    const EmittedWorksheet source = emit_source_worksheet(xml, replacements, 4);

    check(source.xml == single_chunk.xml,
        "chunk-source output emitter should match single-chunk output across token boundaries");
    check(source.summary.matched_replacement_count == single_chunk.summary.matched_replacement_count,
        "chunk-source output emitter should preserve matched replacement count");
    check(source.summary.missing_cell_references == single_chunk.summary.missing_cell_references,
        "chunk-source output emitter should preserve missing replacement diagnostics");
}

void test_transformer_output_preserves_self_closing_pass_through_once()
{
    const std::string xml =
        R"(<worksheet><sheetData><row r="1"><c r="A1"/><c r="B1"><v>old</v></c></row><row r="2"/></sheetData></worksheet>)";
    const std::string replacement_xml = R"(<c r="B1"><v>new</v></c>)";
    const std::array replacements {
        cell_replacement("B1", replacement_xml),
    };

    const EmittedWorksheet emitted = emit_source_worksheet(xml, replacements, 4);

    check(emitted.xml
            == R"(<worksheet><sheetData><row r="1"><c r="A1"/><c r="B1"><v>new</v></c></row><row r="2"/></sheetData></worksheet>)",
        "output emitter should not duplicate pass-through self-closing cells or rows");
    check(emitted.summary.matched_replacement_count == 1,
        "self-closing pass-through output should still match the target replacement");
    check(emitted.summary.missing_cell_references.empty(),
        "self-closing pass-through output should not report matched replacements as missing");

    const std::array missing_replacements {
        cell_replacement("C3", R"(<c r="C3"><v>missing</v></c>)"),
    };
    const std::string empty_sheet_data_xml = R"(<worksheet><sheetData/></worksheet>)";
    const EmittedWorksheet empty_sheet_data =
        emit_source_worksheet(empty_sheet_data_xml, missing_replacements, 3);

    check(empty_sheet_data.xml == empty_sheet_data_xml,
        "output emitter should not duplicate self-closing sheetData when passing through");
    check(empty_sheet_data.summary.missing_cell_references.size() == 1,
        "self-closing sheetData pass-through should preserve missing replacement diagnostics");
}

void test_transformer_upsert_replaces_existing_and_inserts_missing_cells_and_rows()
{
    const std::string xml =
        R"(<worksheet><sheetData>)"
        R"(<row r="1"><c r="A1"><v>old-a</v></c><c r="C1"><v>old-c</v></c></row>)"
        R"(<row r="3"><c r="B3"><v>old-b3</v></c></row>)"
        R"(</sheetData></worksheet>)";
    const std::array replacements {
        cell_replacement("D4", R"(<c r="D4"><v>new-d4</v></c>)"),
        cell_replacement("A2", R"(<c r="A2"><v>new-a2</v></c>)"),
        cell_replacement("B1", R"(<c r="B1"><v>new-b1</v></c>)"),
        cell_replacement("C3", R"(<c r="C3"><v>new-c3</v></c>)"),
        cell_replacement("A1", R"(<c r="A1"><v>new-a1</v></c>)"),
    };

    const EmittedWorksheet emitted = emit_upsert_worksheet(xml, replacements, 9);

    check(emitted.xml
            == R"(<worksheet><sheetData><row r="1"><c r="A1"><v>new-a1</v></c><c r="B1"><v>new-b1</v></c><c r="C1"><v>old-c</v></c></row><row r="2"><c r="A2"><v>new-a2</v></c></row><row r="3"><c r="B3"><v>old-b3</v></c><c r="C3"><v>new-c3</v></c></row><row r="4"><c r="D4"><v>new-d4</v></c></row></sheetData></worksheet>)",
        "upsert should replace existing targets and insert missing cells/rows in row-column order");
    check(emitted.summary.matched_replacement_count == 1,
        "upsert should count matched existing-cell replacements");
    check(emitted.summary.inserted_cell_count == 4,
        "upsert should count inserted cells separately from matched replacements");
    check(emitted.summary.missing_cell_references.empty(),
        "upsert should not report synthesized cells as missing");
}

void test_transformer_upsert_expands_self_closing_sheet_data_and_rows()
{
    const std::array sheet_data_replacements {
        cell_replacement("B2", R"(<c r="B2"><v>new-b2</v></c>)"),
    };
    const EmittedWorksheet sheet_data =
        emit_upsert_worksheet(R"(<worksheet><sheetData/></worksheet>)",
            sheet_data_replacements, 3);

    check(sheet_data.xml
            == R"(<worksheet><sheetData><row r="2"><c r="B2"><v>new-b2</v></c></row></sheetData></worksheet>)",
        "upsert should expand self-closing sheetData to host inserted rows");
    check(sheet_data.summary.inserted_cell_count == 1,
        "self-closing sheetData upsert should report one inserted cell");

    const std::array row_replacements {
        cell_replacement("C2", R"(<c r="C2"><v>new-c2</v></c>)"),
    };
    const EmittedWorksheet row =
        emit_upsert_worksheet(R"(<worksheet><sheetData><row r="2"/></sheetData></worksheet>)",
            row_replacements, 4);

    check(row.xml
            == R"(<worksheet><sheetData><row r="2"><c r="C2"><v>new-c2</v></c></row></sheetData></worksheet>)",
        "upsert should expand self-closing rows to host inserted cells");
    check(row.summary.inserted_cell_count == 1,
        "self-closing row upsert should report one inserted cell");
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
        cell_replacement("A17", replacement_xml),
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
        cell_replacement("C3", R"(<c r="C3"><v>missing</v></c>)"),
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
        cell_replacement("B1", R"(<c r="B1"><v>new</v></c>)"),
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

void test_transformer_rejects_mismatched_source_cell_value_boundaries()
{
    const std::string xml =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</f></c></row></sheetData></worksheet>)";
    const std::array replacements {
        cell_replacement("B2", R"(<c r="B2"><v>missing</v></c>)"),
    };

    bool failed = false;
    try {
        (void)emit_source_worksheet(xml, replacements, 4);
    } catch (const std::exception& error) {
        failed = true;
        check(contains_text(error.what(), "mismatched cell value boundary"),
            "transformer should preserve event-reader cell value boundary diagnostics");
    }
    check(failed,
        "transformer should reject source worksheets with mismatched value wrappers");
}

void test_transformer_rejects_invalid_source_core_element_nesting()
{
    const std::string xml =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><c r="B1"/></c></row></sheetData></worksheet>)";
    const std::array replacements {
        cell_replacement("B2", R"(<c r="B2"><v>missing</v></c>)"),
    };

    bool failed = false;
    try {
        (void)emit_source_worksheet(xml, replacements, 5);
    } catch (const std::exception& error) {
        failed = true;
        check(contains_text(error.what(), "invalid cell boundary"),
            "transformer should preserve event-reader nested cell diagnostics");
    }
    check(failed,
        "transformer should reject source worksheets with nested cell elements");
}

} // namespace

int main()
{
    try {
        test_replacement_payload_replays_chunks_without_direct_materialized_accessor();
        test_chunked_replacement_payload_preflights_and_replays_chunks();
        test_chunked_replacement_payload_rejects_total_size_over_limit();
        test_transformer_emits_replace_cell_action_and_skips_original_cell_payload();
        test_transformer_orders_replacements_by_source_xml();
        test_transformer_reports_missing_and_rejects_invalid_replacements();
        test_transformer_validates_replacement_cell_payload_root_and_reference();
        test_transformer_requires_preflighted_replacement_plan();
        test_transformer_reuses_prevalidated_replacement_plan_for_scan_and_emit();
        test_transformer_rejects_oversized_materialized_replacement_cell_payload();
        test_transformer_emits_rewritten_chunks_with_raw_text_preserved();
        test_transformer_chunked_emitter_matches_single_chunk_emitter_across_boundaries();
        test_transformer_chunk_source_emitter_matches_single_chunk_emitter_across_boundaries();
        test_transformer_output_preserves_self_closing_pass_through_once();
        test_transformer_upsert_replaces_existing_and_inserts_missing_cells_and_rows();
        test_transformer_upsert_expands_self_closing_sheet_data_and_rows();
        test_transformer_chunked_emitter_uses_bounded_window_not_full_document();
        test_transformer_chunked_emitter_reports_missing_and_rejects_oversized_input();
        test_transformer_rejects_mismatched_source_cell_value_boundaries();
        test_transformer_rejects_invalid_source_core_element_nesting();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
