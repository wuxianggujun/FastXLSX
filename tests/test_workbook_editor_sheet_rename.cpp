#include "../src/workbook_editor_sheet_rename.hpp"

#include <fastxlsx/detail/cell_store.hpp>
#include <fastxlsx/workbook.hpp>

#include <iostream>
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

void check_names(
    const std::vector<std::string>& actual,
    const std::vector<std::string>& expected,
    const char* message)
{
    if (actual != expected) {
        throw TestFailure(message);
    }
}

bool throws_fastxlsx_error(auto&& callable)
{
    try {
        callable();
    } catch (const fastxlsx::FastXlsxError&) {
        return true;
    }
    return false;
}

void materialize_session(
    fastxlsx::detail::MaterializedWorksheetSessionRegistry& registry,
    std::string planned_name)
{
    fastxlsx::detail::CellStore store;
    registry.materialize(std::move(planned_name), std::move(store));
}

void test_rename_preflight_accepts_unmaterialized_target()
{
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    materialize_session(registry, "Other");

    fastxlsx::detail::validate_workbook_editor_sheet_rename_preflight(
        registry, "Data");
}

void test_rename_preflight_rejects_materialized_target()
{
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    materialize_session(registry, "Data");

    check(throws_fastxlsx_error([&] {
        fastxlsx::detail::validate_workbook_editor_sheet_rename_preflight(
            registry, "Data");
    }), "rename preflight should reject a materialized planned worksheet");
}

void test_rename_state_updates_catalog_and_pending_payloads()
{
    fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data", "Other"});
    fastxlsx::detail::WorkbookEditorPendingSheetDataPayloads pending_payloads;
    pending_payloads.record("Data", 3, 128);

    fastxlsx::detail::record_workbook_editor_sheet_rename_state(
        catalog, pending_payloads, "Data", "Renamed");

    check_names(catalog.current_names(), {"Renamed", "Other"},
        "rename state should update the planned catalog name");
    check(!catalog.has_current("Data"), "rename state should remove the old planned name");
    check(catalog.has_current("Renamed"), "rename state should expose the new planned name");
    check(!pending_payloads.contains("Data"),
        "rename state should migrate pending payloads away from the old name");
    check(pending_payloads.contains("Renamed"),
        "rename state should migrate pending payloads to the new name");
    check(pending_payloads.cell_count() == 3,
        "rename state should preserve pending replacement cell totals");
    check(pending_payloads.estimated_memory_usage() == 128,
        "rename state should preserve pending replacement memory totals");
}

void test_rename_state_restores_source_name()
{
    fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data", "Other"});
    fastxlsx::detail::WorkbookEditorPendingSheetDataPayloads pending_payloads;
    pending_payloads.record("Data", 1, 64);

    fastxlsx::detail::record_workbook_editor_sheet_rename_state(
        catalog, pending_payloads, "Data", "Temporary");
    fastxlsx::detail::record_workbook_editor_sheet_rename_state(
        catalog, pending_payloads, "Temporary", "Data");

    check_names(catalog.current_names(), {"Data", "Other"},
        "rename-back state should restore the source catalog name");
    check(pending_payloads.contains("Data"),
        "rename-back state should migrate pending payloads back to the source name");
    check(!pending_payloads.contains("Temporary"),
        "rename-back state should remove the transient pending payload name");
}

} // namespace

int main()
{
    try {
        test_rename_preflight_accepts_unmaterialized_target();
        test_rename_preflight_rejects_materialized_target();
        test_rename_state_updates_catalog_and_pending_payloads();
        test_rename_state_restores_source_name();
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
    return 0;
}
