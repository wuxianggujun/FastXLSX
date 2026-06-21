#include "../src/workbook_editor_pending_edits.hpp"

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

void check_names(
    const std::vector<std::string>& actual,
    const std::vector<std::string>& expected,
    const char* message)
{
    if (actual != expected) {
        throw TestFailure(message);
    }
}

void test_pending_payloads_start_empty()
{
    fastxlsx::detail::WorkbookEditorPendingSheetDataPayloads payloads;

    check(payloads.empty(), "pending payloads should start empty");
    check(!payloads.contains("Data"), "empty pending payloads should not contain any sheet");
    check(payloads.find("Data") == nullptr, "empty pending payload lookup should return null");
    check(payloads.cell_count() == 0, "empty pending payloads should have zero cells");
    check(payloads.estimated_memory_usage() == 0,
        "empty pending payloads should have zero memory estimate");
    check_names(payloads.worksheet_names({"Data"}), {},
        "empty pending payloads should report no worksheet names");
}

void test_pending_payloads_record_and_replace_diagnostics()
{
    fastxlsx::detail::WorkbookEditorPendingSheetDataPayloads payloads;

    payloads.record("Data", 2, 20);
    payloads.record("Other", 3, 30);
    check(!payloads.empty(), "recorded pending payloads should not be empty");
    check(payloads.contains("Data"), "pending payloads should find recorded sheet");
    check(payloads.cell_count() == 5, "pending payloads should sum replacement cells");
    check(payloads.estimated_memory_usage() == 50,
        "pending payloads should sum estimated memory usage");
    check_names(payloads.worksheet_names({"Other", "Data"}), {"Other", "Data"},
        "pending payload names should follow current catalog order first");

    payloads.record("Data", 7, 70);
    const auto* data_payload = payloads.find("Data");
    check(data_payload != nullptr, "replaced pending payload should still be present");
    check(data_payload->cell_count == 7,
        "recording the same sheet should replace its cell diagnostic");
    check(payloads.cell_count() == 10,
        "replacing one pending sheet should update aggregate cell count");
    check(payloads.estimated_memory_usage() == 100,
        "replacing one pending sheet should update aggregate memory estimate");
}

void test_pending_payload_names_preserve_orphan_diagnostics()
{
    fastxlsx::detail::WorkbookEditorPendingSheetDataPayloads payloads;

    payloads.record("Detached", 1, 10);
    payloads.record("Data", 2, 20);
    payloads.record("Other", 3, 30);

    check_names(payloads.worksheet_names({"Other"}), {"Other", "Data", "Detached"},
        "pending payload names should append non-catalog diagnostics in map order");
}

void test_pending_payloads_migrate_after_rename()
{
    fastxlsx::detail::WorkbookEditorPendingSheetDataPayloads payloads;

    payloads.record("Data", 2, 20);
    payloads.record("Other", 3, 30);
    payloads.migrate("Data", "Renamed");

    check(!payloads.contains("Data"),
        "migrating a pending payload should remove the old sheet name");
    check(payloads.contains("Renamed"),
        "migrating a pending payload should expose the new sheet name");
    const auto* renamed_payload = payloads.find("Renamed");
    check(renamed_payload != nullptr && renamed_payload->cell_count == 2 &&
            renamed_payload->estimated_memory_usage == 20,
        "migrating a pending payload should preserve diagnostics");
    check_names(payloads.worksheet_names({"Renamed", "Other"}), {"Renamed", "Other"},
        "migrated pending payload names should follow renamed catalog order");

    payloads.migrate("Missing", "Ignored");
    check(!payloads.contains("Ignored"),
        "migrating a missing payload should be a no-op");

    payloads.migrate("Renamed", "Renamed");
    check(payloads.contains("Renamed"),
        "same-name migration should keep the pending payload");
}

} // namespace

int main()
{
    try {
        test_pending_payloads_start_empty();
        test_pending_payloads_record_and_replace_diagnostics();
        test_pending_payload_names_preserve_orphan_diagnostics();
        test_pending_payloads_migrate_after_rename();
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
    return 0;
}
