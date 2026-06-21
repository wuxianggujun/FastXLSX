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

} // namespace

int main()
{
    try {
        test_catalog_plan_tracks_source_and_current_names();
        test_catalog_plan_records_and_reverts_renames();
        test_catalog_plan_chained_renames_follow_current_name();
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
    return 0;
}
