#define FASTXLSX_PACKAGE_EDITOR_SHEETDATA_INCLUDE_CORE_TESTS 0
#define FASTXLSX_PACKAGE_EDITOR_SHEETDATA_INCLUDE_BY_NAME_TESTS 0
#define FASTXLSX_PACKAGE_EDITOR_SHEETDATA_INCLUDE_PLANNED_CATALOG_TESTS 1
#define FASTXLSX_PACKAGE_EDITOR_SHEETDATA_SHARD_NAME "sheetdata-planned-catalog"
#define FASTXLSX_PACKAGE_EDITOR_SHEETDATA_RUN_TESTS()                                      \
    do {                                                                                   \
        test_package_editor_replaces_worksheet_by_sheet_name();                            \
        test_package_editor_replaces_worksheet_by_planned_workbook_sheet_name();            \
        test_package_editor_replaces_sheet_data_by_planned_workbook_sheet_name();           \
        test_package_editor_planned_workbook_catalog_respects_relationship_namespace();     \
    } while (false)

#include "test_package_editor_sheetdata_base.cpp"
