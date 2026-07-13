#pragma once

#include "workbook_editor_state.hpp"

namespace fastxlsx::detail {

// Internal diagnostics bridge for tests and benchmarks that need to verify the
// PackageEditor plan selected by the public WorkbookEditor facade. This is not a
// public API surface and should not be used by library callers.
struct WorkbookEditorPackagePlanAccessor {
    [[nodiscard]] static PackageEditorOutputPlan planned_output(
        const WorkbookEditor& editor,
        PackageWriterOptions options = {PackageWriterBackend::StoredZipBootstrap})
    {
        if (editor.impl_ == nullptr) {
            throw FastXlsxError("WorkbookEditor is not open");
        }
        return editor.impl_->editor.planned_output(options);
    }

    static void set_package_writer_telemetry(
        WorkbookEditor& editor, PackageWriterTelemetry* telemetry)
    {
        if (editor.impl_ == nullptr) {
            throw FastXlsxError("WorkbookEditor is not open");
        }
        editor.impl_->package_writer_telemetry = telemetry;
    }
};

} // namespace fastxlsx::detail
