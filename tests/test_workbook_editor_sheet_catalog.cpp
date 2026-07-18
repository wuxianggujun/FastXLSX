#include "../src/workbook_editor_sheet_catalog.hpp"

#include <iostream>
#include <optional>
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

void check_equal(std::string_view actual, std::string_view expected, const char* message)
{
    if (actual != expected) {
        std::string detail(message);
        detail += " actual=[";
        detail += actual;
        detail += "] expected=[";
        detail += expected;
        detail += "]";
        throw TestFailure(detail);
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

void test_catalog_plan_tracks_source_and_current_names()
{
    fastxlsx::detail::WorkbookEditorSheetCatalogPlan plan({"Data", "Other"});

    check_names(plan.source_names(), {"Data", "Other"},
        "sheet catalog plan should preserve source names");
    check_names(plan.current_names(), {"Data", "Other"},
        "sheet catalog plan should start with source names as current names");
    check(plan.has_source("Data"), "sheet catalog plan should find source names");
    check(!plan.has_source("Renamed"),
        "sheet catalog plan should not treat planned names as source names");
    check(plan.has_current("Other"), "sheet catalog plan should find current names");
    check(!plan.has_current("Missing"), "sheet catalog plan should reject missing current names");
}

void test_catalog_plan_records_and_reverts_renames()
{
    fastxlsx::detail::WorkbookEditorSheetCatalogPlan plan({"Data", "Other"});

    plan.record_rename("Data", "Renamed");
    check_names(plan.source_names(), {"Data", "Other"},
        "sheet catalog plan rename should not mutate source names");
    check_names(plan.current_names(), {"Renamed", "Other"},
        "sheet catalog plan should expose renamed current names");

    const std::vector<fastxlsx::detail::WorkbookEditorSheetCatalogEntry> renamed_catalog =
        plan.entries();
    check(renamed_catalog.size() == 2,
        "sheet catalog plan should keep source catalog cardinality after rename");
    check_equal(renamed_catalog[0].source_name, "Data",
        "sheet catalog plan should keep source name after rename");
    check_equal(renamed_catalog[0].planned_name, "Renamed",
        "sheet catalog plan should expose planned name after rename");
    check(renamed_catalog[0].renamed,
        "sheet catalog plan should mark renamed catalog entries");
    check(plan.has_current("Renamed"),
        "sheet catalog plan should find renamed current names");
    check(!plan.has_current("Data"),
        "sheet catalog plan should not keep old source name as current after rename");

    const std::optional<std::string> source_name = plan.source_name_for_current("Renamed");
    check(source_name.has_value(),
        "sheet catalog plan should map current names back to source names");
    check_equal(*source_name, "Data",
        "sheet catalog plan should map renamed current name to original source name");

    plan.record_rename("Renamed", "Data");
    check_names(plan.current_names(), {"Data", "Other"},
        "sheet catalog plan should clear rename when planned name returns to source name");
    check(!plan.entries()[0].renamed,
        "sheet catalog plan should clear renamed flag after reverting to source name");
}

void test_catalog_plan_chained_renames_follow_current_name()
{
    fastxlsx::detail::WorkbookEditorSheetCatalogPlan plan({"Data", "Other"});

    plan.record_rename("Data", "First");
    plan.record_rename("First", "Second");

    check_names(plan.current_names(), {"Second", "Other"},
        "sheet catalog plan should chain renames through current names");
    const std::optional<std::string> source_name = plan.source_name_for_current("Second");
    check(source_name.has_value(),
        "sheet catalog plan should resolve chained current names");
    check_equal(*source_name, "Data",
        "sheet catalog plan should keep chained rename bound to original source name");
}

void test_catalog_plan_tracks_and_renames_added_sheets()
{
    fastxlsx::detail::WorkbookEditorSheetCatalogPlan plan({"Data", "Other"});

    plan.record_add("Added");
    check_names(plan.source_names(), {"Data", "Other"},
        "sheet catalog add should not mutate source names");
    check_names(plan.current_names(), {"Data", "Other", "Added"},
        "sheet catalog add should append the planned name");
    check(plan.is_added_current("Added"),
        "sheet catalog add should identify the added current name");
    check(!plan.source_name_for_current("Added").has_value(),
        "added worksheet should not pretend to have a source name");

    const auto added_catalog = plan.entries();
    check(added_catalog.size() == 3,
        "sheet catalog add should append one catalog entry");
    check(added_catalog[2].source_name.empty()
            && added_catalog[2].planned_name == "Added"
            && !added_catalog[2].renamed && added_catalog[2].added,
        "sheet catalog add should expose explicit added semantics");

    plan.record_rename("Added", "Renamed Added");
    check_names(plan.current_names(), {"Data", "Other", "Renamed Added"},
        "sheet catalog should rename an added current entry in place");
    check(plan.is_added_current("Renamed Added") && !plan.is_added_current("Added"),
        "sheet catalog should retain added identity after rename");
}

void test_catalog_plan_removes_planned_entries_without_mutating_source_names()
{
    fastxlsx::detail::WorkbookEditorSheetCatalogPlan plan({"Data", "Other"});
    plan.record_rename("Data", "Renamed Data");
    plan.record_add("Added");

    plan.record_remove("Renamed Data");
    check_names(plan.source_names(), {"Data", "Other"},
        "sheet catalog remove should preserve immutable source names");
    check_names(plan.current_names(), {"Other", "Added"},
        "sheet catalog remove should omit the source-backed planned entry");
    check(!plan.has_current("Data") && !plan.has_current("Renamed Data"),
        "sheet catalog remove should clear source and planned lookup for the target");
    check(plan.removed_entries().size() == 1
            && plan.removed_entries().front().source_name == "Data"
            && plan.removed_entries().front().planned_name == "Renamed Data",
        "sheet catalog remove should retain a public removal diagnostic");

    plan.record_remove("Added");
    check_names(plan.current_names(), {"Other"},
        "sheet catalog remove should erase a generated planned entry");
    check(plan.entries().size() == 1 && plan.entries().front().planned_name == "Other",
        "sheet catalog entries should expose only surviving planned worksheets");
    check(plan.removed_entries().size() == 2 && plan.removed_entries().back().added,
        "sheet catalog remove should identify a generated removal diagnostic");
}

} // namespace

int main()
{
    try {
        test_catalog_plan_tracks_source_and_current_names();
        test_catalog_plan_records_and_reverts_renames();
        test_catalog_plan_chained_renames_follow_current_name();
        test_catalog_plan_tracks_and_renames_added_sheets();
        test_catalog_plan_removes_planned_entries_without_mutating_source_names();
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
    return 0;
}
