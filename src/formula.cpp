#include <fastxlsx/detail/formula.hpp>

#include <fastxlsx/detail/xml.hpp>

#include <optional>

namespace fastxlsx::detail {
namespace {

constexpr std::uint32_t excel_max_column = 16384U;
constexpr std::uint32_t excel_max_row = 1048576U;

enum class FormulaAxisKind {
    Row,
    Column,
};

struct FormulaReferenceToken {
    std::size_t length = 0;
    FormulaCellReference cell;
};

struct FormulaCellRangeToken {
    std::size_t length = 0;
    FormulaReferenceToken first;
    FormulaReferenceToken last;
};

struct FormulaAxisReferenceToken {
    FormulaAxisKind kind = FormulaAxisKind::Column;
    std::size_t length = 0;
    std::uint32_t value = 0;
    bool absolute = false;
};

struct FormulaAxisRangeToken {
    std::size_t length = 0;
    FormulaAxisReferenceToken first;
    FormulaAxisReferenceToken last;
};

bool is_ascii_alpha(char ch) noexcept
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

bool is_ascii_digit(char ch) noexcept
{
    return ch >= '0' && ch <= '9';
}

std::uint32_t uppercase_column_value(char ch) noexcept
{
    if (ch >= 'a' && ch <= 'z') {
        ch = static_cast<char>(ch - 'a' + 'A');
    }
    return static_cast<std::uint32_t>(ch - 'A' + 1);
}

bool is_formula_name_char(char ch) noexcept
{
    return is_ascii_alpha(ch) || is_ascii_digit(ch) || ch == '_' || ch == '.';
}

bool is_formula_identifier_start(char ch) noexcept
{
    return is_ascii_alpha(ch) || ch == '_';
}

bool has_formula_reference_left_boundary(std::string_view formula, std::size_t position)
{
    if (position == 0) {
        return true;
    }
    return !is_formula_name_char(formula[position - 1]);
}

bool has_formula_reference_right_boundary(std::string_view formula, std::size_t position)
{
    if (position >= formula.size()) {
        return true;
    }
    const char next = formula[position];
    return !is_formula_name_char(next) && next != '(' && next != '!';
}

bool is_unquoted_sheet_qualifier_boundary(char ch) noexcept
{
    switch (ch) {
    case ' ':
    case '\t':
    case '\r':
    case '\n':
    case '"':
    case '\'':
    case '(':
    case ')':
    case ',':
    case ';':
    case '+':
    case '-':
    case '*':
    case '/':
    case '^':
    case '&':
    case '=':
    case '<':
    case '>':
    case '%':
    case '{':
    case '}':
        return true;
    default:
        return false;
    }
}

bool is_formula_space(char ch) noexcept
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

bool is_formula_operator_char(char ch) noexcept
{
    switch (ch) {
    case '+':
    case '-':
    case '*':
    case '/':
    case '^':
    case '&':
    case '=':
    case '<':
    case '>':
    case '%':
    case ':':
    case '!':
        return true;
    default:
        return false;
    }
}

bool is_formula_punctuation_char(char ch) noexcept
{
    switch (ch) {
    case '(':
    case ')':
    case ',':
    case ';':
    case '{':
    case '}':
        return true;
    default:
        return false;
    }
}

std::size_t formula_whitespace_length(std::string_view formula, std::size_t position)
{
    const std::size_t start = position;
    while (position < formula.size() && is_formula_space(formula[position])) {
        ++position;
    }
    return position - start;
}

std::optional<std::size_t> find_opening_quoted_sheet_name(
    std::string_view formula, std::size_t closing_quote)
{
    std::size_t position = closing_quote;
    while (position > 0) {
        --position;
        if (formula[position] != '\'') {
            continue;
        }
        if (position > 0 && formula[position - 1] == '\'') {
            --position;
            continue;
        }
        return position;
    }
    return std::nullopt;
}

FormulaSheetQualifier detect_sheet_qualifier(
    std::string_view formula, std::size_t reference_offset)
{
    if (reference_offset == 0 || formula[reference_offset - 1] != '!') {
        return {};
    }

    const std::size_t bang = reference_offset - 1;
    if (bang == 0) {
        return {};
    }

    if (formula[bang - 1] == '\'') {
        const std::optional<std::size_t> opening =
            find_opening_quoted_sheet_name(formula, bang - 1);
        if (!opening.has_value()) {
            return {};
        }
        return FormulaSheetQualifier {
            true,
            *opening,
            reference_offset - *opening,
            *opening + 1,
            bang - *opening - 2,
            true,
        };
    }

    std::size_t start = bang;
    while (start > 0 && !is_unquoted_sheet_qualifier_boundary(formula[start - 1])) {
        --start;
    }
    if (start == bang) {
        return {};
    }

    return FormulaSheetQualifier {
        true,
        start,
        reference_offset - start,
        start,
        bang - start,
        false,
    };
}

std::optional<FormulaReferenceToken> parse_formula_reference_token(
    std::string_view formula, std::size_t position)
{
    const std::size_t start = position;
    if (!has_formula_reference_left_boundary(formula, start)) {
        return std::nullopt;
    }

    bool column_absolute = false;
    if (position < formula.size() && formula[position] == '$') {
        column_absolute = true;
        ++position;
    }

    std::uint64_t column = 0;
    std::size_t column_letters = 0;
    while (position < formula.size() && is_ascii_alpha(formula[position])) {
        column = column * 26U + uppercase_column_value(formula[position]);
        ++column_letters;
        ++position;
    }
    if (column_letters == 0 || column == 0 || column > excel_max_column) {
        return std::nullopt;
    }

    bool row_absolute = false;
    if (position < formula.size() && formula[position] == '$') {
        row_absolute = true;
        ++position;
    }

    const std::size_t row_begin = position;
    std::uint64_t row = 0;
    while (position < formula.size() && is_ascii_digit(formula[position])) {
        row = row * 10U + static_cast<std::uint32_t>(formula[position] - '0');
        ++position;
    }
    if (position == row_begin || row == 0 || row > excel_max_row) {
        return std::nullopt;
    }
    if (!has_formula_reference_right_boundary(formula, position)) {
        return std::nullopt;
    }

    return FormulaReferenceToken {
        position - start,
        FormulaCellReference {
            static_cast<std::uint32_t>(row),
            static_cast<std::uint32_t>(column),
            row_absolute,
            column_absolute,
        },
    };
}

std::optional<FormulaCellRangeToken> parse_formula_cell_range_token(
    std::string_view formula, std::size_t position)
{
    const std::size_t start = position;
    const std::optional<FormulaReferenceToken> first =
        parse_formula_reference_token(formula, position);
    if (!first.has_value()) {
        return std::nullopt;
    }
    position += first->length;
    if (position >= formula.size() || formula[position] != ':') {
        return std::nullopt;
    }
    ++position;

    const std::optional<FormulaReferenceToken> last =
        parse_formula_reference_token(formula, position);
    if (!last.has_value()) {
        return std::nullopt;
    }
    position += last->length;
    return FormulaCellRangeToken {position - start, *first, *last};
}

std::optional<FormulaAxisReferenceToken> parse_formula_axis_reference_token(
    std::string_view formula, std::size_t position)
{
    const std::size_t start = position;
    bool absolute = false;
    if (position < formula.size() && formula[position] == '$') {
        absolute = true;
        ++position;
    }

    if (position < formula.size() && is_ascii_alpha(formula[position])) {
        std::uint64_t column = 0;
        std::size_t column_letters = 0;
        while (position < formula.size() && is_ascii_alpha(formula[position])) {
            column = column * 26U + uppercase_column_value(formula[position]);
            ++column_letters;
            ++position;
        }
        if (column_letters == 0 || column == 0 || column > excel_max_column) {
            return std::nullopt;
        }
        return FormulaAxisReferenceToken {FormulaAxisKind::Column, position - start,
            static_cast<std::uint32_t>(column), absolute};
    }

    if (position < formula.size() && is_ascii_digit(formula[position])) {
        std::uint64_t row = 0;
        while (position < formula.size() && is_ascii_digit(formula[position])) {
            row = row * 10U + static_cast<std::uint32_t>(formula[position] - '0');
            ++position;
        }
        if (row == 0 || row > excel_max_row) {
            return std::nullopt;
        }
        return FormulaAxisReferenceToken {FormulaAxisKind::Row, position - start,
            static_cast<std::uint32_t>(row), absolute};
    }

    return std::nullopt;
}

std::optional<FormulaAxisRangeToken> parse_formula_axis_range_token(
    std::string_view formula, std::size_t position)
{
    const std::size_t start = position;
    if (!has_formula_reference_left_boundary(formula, start)) {
        return std::nullopt;
    }

    const std::optional<FormulaAxisReferenceToken> first =
        parse_formula_axis_reference_token(formula, position);
    if (!first.has_value()) {
        return std::nullopt;
    }
    position += first->length;
    if (position >= formula.size() || formula[position] != ':') {
        return std::nullopt;
    }
    ++position;

    const std::optional<FormulaAxisReferenceToken> last =
        parse_formula_axis_reference_token(formula, position);
    if (!last.has_value() || last->kind != first->kind) {
        return std::nullopt;
    }
    position += last->length;
    if (!has_formula_reference_right_boundary(formula, position)) {
        return std::nullopt;
    }

    return FormulaAxisRangeToken {position - start, *first, *last};
}

void append_formula_column_reference(std::string& output, std::uint32_t column)
{
    char letters[3] {};
    std::size_t count = 0;
    while (column > 0) {
        --column;
        letters[count] = static_cast<char>('A' + (column % 26U));
        ++count;
        column /= 26U;
    }
    while (count > 0) {
        --count;
        output += letters[count];
    }
}

std::optional<std::uint32_t> translate_formula_axis(
    std::uint32_t value, std::int64_t delta, bool absolute, std::uint32_t limit)
{
    if (absolute) {
        return value;
    }
    const std::int64_t translated = static_cast<std::int64_t>(value) + delta;
    if (translated < 1 || translated > static_cast<std::int64_t>(limit)) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(translated);
}

void append_translated_formula_reference(
    std::string& output,
    const FormulaCellReference& cell,
    FormulaTranslationDelta delta)
{
    const std::optional<std::uint32_t> translated_column = translate_formula_axis(
        cell.column, delta.column_delta, cell.column_absolute, excel_max_column);
    const std::optional<std::uint32_t> translated_row = translate_formula_axis(
        cell.row, delta.row_delta, cell.row_absolute, excel_max_row);
    if (!translated_column.has_value() || !translated_row.has_value()) {
        output += "#REF!";
        return;
    }

    if (cell.column_absolute) {
        output += '$';
    }
    append_formula_column_reference(output, *translated_column);
    if (cell.row_absolute) {
        output += '$';
    }
    append_unsigned_decimal(output, *translated_row);
}

void append_translated_formula_cell_range(
    std::string& output,
    const FormulaCellReference& first,
    const FormulaCellReference& last,
    FormulaTranslationDelta delta)
{
    append_translated_formula_reference(output, first, delta);
    output += ':';
    append_translated_formula_reference(output, last, delta);
}

void append_formula_axis_reference(
    std::string& output, FormulaAxisKind kind, std::uint32_t value, bool absolute)
{
    if (absolute) {
        output += '$';
    }
    if (kind == FormulaAxisKind::Column) {
        append_formula_column_reference(output, value);
    } else {
        append_unsigned_decimal(output, value);
    }
}

void append_translated_formula_axis_range(
    std::string& output,
    FormulaReferenceKind kind,
    const FormulaAxisReference& first,
    const FormulaAxisReference& last,
    FormulaTranslationDelta delta)
{
    const bool column_range = kind == FormulaReferenceKind::WholeColumnRange;
    const FormulaAxisKind axis_kind =
        column_range ? FormulaAxisKind::Column : FormulaAxisKind::Row;
    const std::int64_t axis_delta = column_range ? delta.column_delta : delta.row_delta;
    const std::uint32_t limit = column_range ? excel_max_column : excel_max_row;

    const std::optional<std::uint32_t> translated_first =
        translate_formula_axis(first.value, axis_delta, first.absolute, limit);
    const std::optional<std::uint32_t> translated_last =
        translate_formula_axis(last.value, axis_delta, last.absolute, limit);
    if (!translated_first.has_value() || !translated_last.has_value()) {
        output += "#REF!";
        return;
    }

    append_formula_axis_reference(output, axis_kind, *translated_first, first.absolute);
    output += ':';
    append_formula_axis_reference(output, axis_kind, *translated_last, last.absolute);
}

void skip_quoted_formula_string(std::string_view formula, std::size_t& position)
{
    ++position;
    while (position < formula.size()) {
        if (formula[position] == '"') {
            ++position;
            if (position < formula.size() && formula[position] == '"') {
                ++position;
                continue;
            }
            return;
        }
        ++position;
    }
}

std::size_t quoted_formula_string_length(std::string_view formula, std::size_t position)
{
    const std::size_t start = position;
    skip_quoted_formula_string(formula, position);
    return position - start;
}

void skip_quoted_sheet_name(std::string_view formula, std::size_t& position)
{
    ++position;
    while (position < formula.size()) {
        if (formula[position] == '\'') {
            ++position;
            if (position < formula.size() && formula[position] == '\'') {
                ++position;
                continue;
            }
            return;
        }
        ++position;
    }
}

std::size_t quoted_sheet_name_length(std::string_view formula, std::size_t position)
{
    const std::size_t start = position;
    skip_quoted_sheet_name(formula, position);
    return position - start;
}

void skip_bracketed_formula_token(std::string_view formula, std::size_t& position)
{
    ++position;
    while (position < formula.size()) {
        const bool closed = formula[position] == ']';
        ++position;
        if (closed) {
            return;
        }
    }
}

std::size_t bracketed_formula_token_length(std::string_view formula, std::size_t position)
{
    const std::size_t start = position;
    skip_bracketed_formula_token(formula, position);
    return position - start;
}

std::size_t formula_identifier_length(std::string_view formula, std::size_t position)
{
    if (position >= formula.size() || !is_formula_identifier_start(formula[position])) {
        return 0;
    }
    const std::size_t start = position;
    ++position;
    while (position < formula.size() && is_formula_name_char(formula[position])) {
        ++position;
    }
    return position - start;
}

std::size_t formula_number_length(std::string_view formula, std::size_t position)
{
    const std::size_t start = position;
    if (position >= formula.size() || !is_ascii_digit(formula[position])) {
        return 0;
    }
    while (position < formula.size() && is_ascii_digit(formula[position])) {
        ++position;
    }
    if (position < formula.size() && formula[position] == '.') {
        const std::size_t decimal_point = position++;
        while (position < formula.size() && is_ascii_digit(formula[position])) {
            ++position;
        }
        if (position == decimal_point + 1) {
            return decimal_point - start;
        }
    }
    if (position < formula.size() && (formula[position] == 'E' || formula[position] == 'e')) {
        const std::size_t exponent = position++;
        if (position < formula.size() && (formula[position] == '+' || formula[position] == '-')) {
            ++position;
        }
        const std::size_t exponent_digits = position;
        while (position < formula.size() && is_ascii_digit(formula[position])) {
            ++position;
        }
        if (position == exponent_digits) {
            return exponent - start;
        }
    }
    return position - start;
}

FormulaReference make_axis_range_reference(
    std::string_view formula, std::size_t offset, const FormulaAxisRangeToken& token)
{
    const FormulaReferenceKind kind =
        token.first.kind == FormulaAxisKind::Column
        ? FormulaReferenceKind::WholeColumnRange
        : FormulaReferenceKind::WholeRowRange;
    return FormulaReference {
        kind,
        offset,
        token.length,
        detect_sheet_qualifier(formula, offset),
        {},
        {},
        FormulaAxisReference {token.first.value, token.first.absolute},
        FormulaAxisReference {token.last.value, token.last.absolute},
    };
}

FormulaReference make_cell_range_reference(
    std::string_view formula, std::size_t offset, const FormulaCellRangeToken& token)
{
    return FormulaReference {
        FormulaReferenceKind::CellRange,
        offset,
        token.length,
        detect_sheet_qualifier(formula, offset),
        token.first.cell,
        token.last.cell,
        {},
        {},
    };
}

FormulaReference make_cell_reference(
    std::string_view formula, std::size_t offset, const FormulaReferenceToken& token)
{
    return FormulaReference {
        FormulaReferenceKind::Cell,
        offset,
        token.length,
        detect_sheet_qualifier(formula, offset),
        token.cell,
        {},
        {},
        {},
    };
}

} // namespace

std::vector<FormulaToken> tokenize_formula(std::string_view formula)
{
    std::vector<FormulaToken> tokens;
    std::size_t position = 0;
    while (position < formula.size()) {
        const char ch = formula[position];

        if (is_formula_space(ch)) {
            const std::size_t length = formula_whitespace_length(formula, position);
            tokens.push_back(FormulaToken {FormulaTokenKind::Whitespace, position, length, {}});
            position += length;
            continue;
        }

        if (ch == '"') {
            const std::size_t length = quoted_formula_string_length(formula, position);
            tokens.push_back(FormulaToken {FormulaTokenKind::StringLiteral, position, length, {}});
            position += length;
            continue;
        }

        if (ch == '\'') {
            const std::size_t length = quoted_sheet_name_length(formula, position);
            tokens.push_back(FormulaToken {FormulaTokenKind::QuotedSheetName, position, length, {}});
            position += length;
            continue;
        }

        if (ch == '[') {
            const std::size_t length = bracketed_formula_token_length(formula, position);
            tokens.push_back(FormulaToken {FormulaTokenKind::BracketedToken, position, length, {}});
            position += length;
            continue;
        }

        if (const std::optional<FormulaAxisRangeToken> axis_range_token =
                parse_formula_axis_range_token(formula, position);
            axis_range_token.has_value()) {
            const FormulaReference reference =
                make_axis_range_reference(formula, position, *axis_range_token);
            tokens.push_back(FormulaToken {
                FormulaTokenKind::Reference,
                position,
                axis_range_token->length,
                reference,
            });
            position += axis_range_token->length;
            continue;
        }

        if (const std::optional<FormulaCellRangeToken> cell_range_token =
                parse_formula_cell_range_token(formula, position);
            cell_range_token.has_value()) {
            const FormulaReference reference =
                make_cell_range_reference(formula, position, *cell_range_token);
            tokens.push_back(FormulaToken {
                FormulaTokenKind::Reference,
                position,
                cell_range_token->length,
                reference,
            });
            position += cell_range_token->length;
            continue;
        }

        if (const std::optional<FormulaReferenceToken> reference_token =
                parse_formula_reference_token(formula, position);
            reference_token.has_value()) {
            const FormulaReference reference =
                make_cell_reference(formula, position, *reference_token);
            tokens.push_back(FormulaToken {
                FormulaTokenKind::Reference,
                position,
                reference_token->length,
                reference,
            });
            position += reference_token->length;
            continue;
        }

        if (const std::size_t length = formula_number_length(formula, position); length > 0) {
            tokens.push_back(FormulaToken {FormulaTokenKind::Number, position, length, {}});
            position += length;
            continue;
        }

        if (const std::size_t length = formula_identifier_length(formula, position); length > 0) {
            const std::size_t next = position + length;
            const FormulaTokenKind kind =
                next < formula.size() && formula[next] == '('
                ? FormulaTokenKind::Function
                : FormulaTokenKind::Identifier;
            tokens.push_back(FormulaToken {kind, position, length, {}});
            position += length;
            continue;
        }

        if (is_formula_operator_char(ch)) {
            tokens.push_back(FormulaToken {FormulaTokenKind::Operator, position, 1, {}});
            ++position;
            continue;
        }

        if (is_formula_punctuation_char(ch)) {
            tokens.push_back(FormulaToken {FormulaTokenKind::Punctuation, position, 1, {}});
            ++position;
            continue;
        }

        tokens.push_back(FormulaToken {FormulaTokenKind::Unknown, position, 1, {}});
        ++position;
    }
    return tokens;
}

std::vector<FormulaReference> scan_formula_references(std::string_view formula)
{
    std::vector<FormulaReference> references;
    for (const FormulaToken& token : tokenize_formula(formula)) {
        if (token.kind == FormulaTokenKind::Reference) {
            references.push_back(token.reference);
        }
    }
    return references;
}

std::string translate_formula_references(
    std::string_view formula, FormulaTranslationDelta delta)
{
    if (delta.row_delta == 0 && delta.column_delta == 0) {
        return std::string(formula);
    }

    const std::vector<FormulaReference> references = scan_formula_references(formula);
    if (references.empty()) {
        return std::string(formula);
    }

    std::string translated;
    translated.reserve(formula.size());
    std::size_t cursor = 0;
    for (const FormulaReference& reference : references) {
        translated.append(formula.substr(cursor, reference.offset - cursor));
        if (reference.kind == FormulaReferenceKind::Cell) {
            append_translated_formula_reference(translated, reference.first_cell, delta);
        } else if (reference.kind == FormulaReferenceKind::CellRange) {
            append_translated_formula_cell_range(
                translated, reference.first_cell, reference.last_cell, delta);
        } else {
            append_translated_formula_axis_range(translated, reference.kind,
                reference.first_axis, reference.last_axis, delta);
        }
        cursor = reference.offset + reference.length;
    }
    translated.append(formula.substr(cursor));
    return translated;
}

} // namespace fastxlsx::detail
