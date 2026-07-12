#pragma once

#include <string>

namespace fastxlsx {

/// Small core/app document-property metadata for workbook output.
///
/// API modes: small new-workbook metadata, Streaming metadata, and Patch
/// existing-workbook metadata. Values are copied into the owning API's state and
/// serialized only into `docProps/core.xml` and `docProps/app.xml` during
/// save()/close()/save_as(). WorkbookEditor replaces those two bounded metadata
/// parts and maintains their package relationships/content types. This value does
/// not create or edit custom document properties and does not affect worksheet
/// row/cell streaming or In-memory cell behavior.
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
