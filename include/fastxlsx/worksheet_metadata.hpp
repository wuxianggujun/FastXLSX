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
/// API mode: Streaming new-workbook metadata, Patch existing-workbook
/// metadata, and bounded existing-workbook read projection. The value selects
/// or represents the OpenXML `type` attribute only; FastXLSX does not validate
/// cell contents, parse formulas, or evaluate rules.
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
/// Operators map to the OpenXML `operator` attribute. List and custom rules do
/// not accept operators in the current narrow API.
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
/// The style maps to the OpenXML `errorStyle` attribute. It does not create
/// styles.xml and does not validate cell values.
enum class DataValidationErrorStyle {
    Stop,
    Warning,
    Information,
};

/// A worksheet-local data-validation rule shared by Streaming, Patch, and
/// bounded read APIs.
///
/// FastXLSX copies formula and prompt/error text into writer/editor state or an
/// owning bounded-reader callback value. Formula text is not parsed, evaluated,
/// or checked against cell contents. The rule does not imply formula
/// recalculation, relationships, content types, styles, or structural range
/// synchronization.
struct DataValidationRule {
    /// Validation kind mapped to the OpenXML `type` attribute.
    DataValidationType type = DataValidationType::List;

    /// Optional comparison operator mapped to the OpenXML `operator`
    /// attribute. Required for between/notBetween formulas and rejected for
    /// list/custom rules in the current implementation.
    std::optional<DataValidationOperator> operator_type;

    /// First formula or list source. Required by the current implementation.
    std::string formula1;

    /// Second formula for between/notBetween operators. Must be empty for
    /// single-formula operators.
    std::string formula2;

    /// Maps to `allowBlank`; writers omit the attribute when false.
    bool allow_blank = false;

    /// Maps to `showDropDown` for list validations. OpenXML uses the inverted
    /// name: true hides Excel's in-cell dropdown arrow.
    bool hide_dropdown_arrow = false;

    /// Maps to `showInputMessage`; writers omit the attribute when false.
    bool show_input_message = false;

    /// Maps to `showErrorMessage`; writers omit the attribute when false.
    bool show_error_message = false;

    /// Optional error-alert style mapped to `errorStyle`.
    std::optional<DataValidationErrorStyle> error_style;

    /// Optional input prompt title mapped to `promptTitle`.
    std::string prompt_title;

    /// Optional input prompt text mapped to `prompt`.
    std::string prompt;

    /// Optional error alert title mapped to `errorTitle`.
    std::string error_title;

    /// Optional error alert text mapped to `error`.
    std::string error;
};

} // namespace fastxlsx
