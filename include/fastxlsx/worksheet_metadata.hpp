#pragma once

/// @file worksheet_metadata.hpp
/// Shared public value types for worksheet metadata APIs.

#include <optional>
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

/// Worksheet data-validation value type.
///
/// API mode: Streaming new-workbook metadata and Patch existing-workbook
/// metadata. The value selects the OpenXML `type` attribute only; FastXLSX
/// does not validate cell contents, parse formulas, or evaluate rules.
enum class DataValidationType {
    Whole,
    Decimal,
    List,
    Date,
    Time,
    TextLength,
    Custom,
};

/// Optional comparison operator for worksheet data validation.
///
/// Operators are serialized only when supplied by DataValidationRule. List and
/// custom rules do not accept operators in the current narrow API.
enum class DataValidationOperator {
    Between,
    NotBetween,
    Equal,
    NotEqual,
    GreaterThan,
    LessThan,
    GreaterThanOrEqual,
    LessThanOrEqual,
};

/// Optional Excel error-alert style for worksheet data validation.
///
/// The style is serialized only when DataValidationRule::error_style is set.
/// It does not create styles.xml and does not validate cell values.
enum class DataValidationErrorStyle {
    Stop,
    Warning,
    Information,
};

/// A worksheet-local data-validation rule shared by Streaming and Patch APIs.
///
/// FastXLSX copies formula and prompt/error text into writer/editor-owned state.
/// Formula text is written as XML text but is not parsed, evaluated, or checked
/// against cell contents. The rule does not imply formula recalculation,
/// relationships, content types, styles, or structural range synchronization.
struct DataValidationRule {
    /// Validation kind written as the OpenXML `type` attribute.
    DataValidationType type = DataValidationType::List;

    /// Optional comparison operator written as the OpenXML `operator`
    /// attribute. Required for between/notBetween formulas and rejected for
    /// list/custom rules in the current implementation.
    std::optional<DataValidationOperator> operator_type;

    /// First formula or list source. Required by the current implementation.
    std::string formula1;

    /// Second formula for between/notBetween operators. Must be empty for
    /// single-formula operators.
    std::string formula2;

    /// Writes `allowBlank="1"` when true. Omitted when false.
    bool allow_blank = false;

    /// Writes `showDropDown="1"` for list validations when true. OpenXML uses
    /// the inverted name: this hides Excel's in-cell dropdown arrow.
    bool hide_dropdown_arrow = false;

    /// Writes `showInputMessage="1"` when true. Omitted when false.
    bool show_input_message = false;

    /// Writes `showErrorMessage="1"` when true. Omitted when false.
    bool show_error_message = false;

    /// Optional error-alert style written as `errorStyle`.
    std::optional<DataValidationErrorStyle> error_style;

    /// Optional input prompt title written as `promptTitle`.
    std::string prompt_title;

    /// Optional input prompt text written as `prompt`.
    std::string prompt;

    /// Optional error alert title written as `errorTitle`.
    std::string error_title;

    /// Optional error alert text written as `error`.
    std::string error;
};

} // namespace fastxlsx
