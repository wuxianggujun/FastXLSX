#define FASTXLSX_PACKAGE_EDITOR_SHEETDATA_INCLUDE_CORE_TESTS 0
#define FASTXLSX_PACKAGE_EDITOR_SHEETDATA_INCLUDE_BY_NAME_TESTS 1
#define FASTXLSX_PACKAGE_EDITOR_SHEETDATA_INCLUDE_PLANNED_CATALOG_TESTS 0
#define FASTXLSX_PACKAGE_EDITOR_SHEETDATA_SHARD_NAME "sheetdata-by-name"
#define FASTXLSX_PACKAGE_EDITOR_SHEETDATA_RUN_TESTS()                                      \
    do {                                                                                   \
        test_package_editor_replaces_worksheet_sheet_data_from_chunk_source();              \
        test_package_editor_replaces_worksheet_sheet_data_by_sheet_name();                  \
        test_package_editor_replaces_worksheet_sheet_data_by_sheet_name_with_absolute_targets(); \
        test_package_editor_replaces_worksheet_sheet_data_by_sheet_name_with_dot_segment_targets(); \
    } while (false)

#include "test_package_editor_sheetdata_base.cpp"
