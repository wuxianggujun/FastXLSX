#pragma once

/// @file worksheet_metadata.hpp
/// Shared public value types for worksheet metadata APIs.

#include <string>

namespace fastxlsx {

/// Optional display metadata for a worksheet hyperlink.
///
/// Empty strings are omitted. Non-empty strings are copied into writer/editor
/// state and emitted as worksheet `<hyperlink>` attributes. These options do
/// not write cell text, create hyperlink styles, validate target reachability,
/// or imply external relationship creation.
struct HyperlinkOptions {
    /// Optional display text written as the OpenXML `display` attribute.
    std::string display;

    /// Optional screen-tip text written as the OpenXML `tooltip` attribute.
    std::string tooltip;
};

} // namespace fastxlsx
