#pragma once

#include <string>

namespace fastxlsx {

/// Small document-property metadata for new workbook output.
///
/// API mode: small workbook metadata shared by the in-memory Workbook path and
/// the Streaming WorkbookWriter path. Values are copied into workbook state and
/// serialized only into `docProps/core.xml` and `docProps/app.xml` during
/// save()/close(). This does not create custom document properties, does not
/// edit existing XLSX files, and does not affect worksheet row/cell streaming
/// behavior.
struct DocumentProperties {
    /// Core property creator. Written as `dc:creator`.
    std::string creator = "FastXLSX";

    /// Core property last modified author. Written as `cp:lastModifiedBy`.
    std::string last_modified_by = "FastXLSX";

    /// Optional core title. Omitted when empty.
    std::string title;

    /// Optional core subject. Omitted when empty.
    std::string subject;

    /// Optional core description. Omitted when empty.
    std::string description;

    /// Optional core keywords text. Omitted when empty.
    std::string keywords;

    /// Optional core category. Omitted when empty.
    std::string category;

    /// Extended property application name. Written as `Application`.
    std::string application = "FastXLSX";

    /// Extended property application version. Written as `AppVersion`.
    std::string app_version = "0.1";
};

} // namespace fastxlsx
