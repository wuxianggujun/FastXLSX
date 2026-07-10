#pragma once

namespace fastxlsx::detail {

#ifdef FASTXLSX_ENABLE_TEST_HOOKS
void run_testing_workbook_editor_save_as_staged_hook();
#endif

} // namespace fastxlsx::detail
