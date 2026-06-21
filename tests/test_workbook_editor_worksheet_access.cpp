#include "../src/workbook_editor_worksheet_access.hpp"

#include <fastxlsx/cell_value.hpp>

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

class TestFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void check(bool condition, const char* message)
{
    if (!condition) {
        throw TestFailure(message);
    }
}

bool throws_fastxlsx_error(auto&& callable)
{
    try {
        callable();
    } catch (const fastxlsx::FastXlsxError&) {
        return true;
    }
    return false;
}

void check_coordinate(
    std::string_view reference,
    std::uint32_t expected_row,
    std::uint32_t expected_column)
{
    const fastxlsx::detail::WorksheetEditorCellCoordinate coordinate =
        fastxlsx::detail::parse_worksheet_editor_a1_cell_reference(reference);
    check(coordinate.row == expected_row, "A1 parser returned unexpected row");
    check(coordinate.column == expected_column, "A1 parser returned unexpected column");
}

void test_a1_parser_accepts_uppercase_excel_bounds()
{
    check_coordinate("A1", 1, 1);
    check_coordinate("Z9", 9, 26);
    check_coordinate("AA10", 10, 27);
    check_coordinate("XFD1048576", 1048576, 16384);
}

void test_a1_parser_rejects_non_strict_references()
{
    check(throws_fastxlsx_error([] {
        (void)fastxlsx::detail::parse_worksheet_editor_a1_cell_reference("");
    }), "A1 parser should reject empty references");
    check(throws_fastxlsx_error([] {
        (void)fastxlsx::detail::parse_worksheet_editor_a1_cell_reference("a1");
    }), "A1 parser should reject lowercase references");
    check(throws_fastxlsx_error([] {
        (void)fastxlsx::detail::parse_worksheet_editor_a1_cell_reference("A0");
    }), "A1 parser should reject row zero");
    check(throws_fastxlsx_error([] {
        (void)fastxlsx::detail::parse_worksheet_editor_a1_cell_reference("A1B");
    }), "A1 parser should reject trailing text");
    check(throws_fastxlsx_error([] {
        (void)fastxlsx::detail::parse_worksheet_editor_a1_cell_reference("XFE1");
    }), "A1 parser should reject columns beyond Excel limits");
    check(throws_fastxlsx_error([] {
        (void)fastxlsx::detail::parse_worksheet_editor_a1_cell_reference("A1048577");
    }), "A1 parser should reject rows beyond Excel limits");
}

void test_coordinate_and_range_validation()
{
    fastxlsx::detail::validate_worksheet_editor_cell_coordinate(1, 1);
    fastxlsx::detail::validate_worksheet_editor_cell_coordinate(1048576, 16384);
    fastxlsx::detail::validate_worksheet_editor_cell_range(fastxlsx::CellRange {
        1,
        1,
        2,
        2,
    });

    check(throws_fastxlsx_error([] {
        fastxlsx::detail::validate_worksheet_editor_cell_coordinate(0, 1);
    }), "coordinate validation should reject row zero");
    check(throws_fastxlsx_error([] {
        fastxlsx::detail::validate_worksheet_editor_cell_coordinate(1, 0);
    }), "coordinate validation should reject column zero");
    check(throws_fastxlsx_error([] {
        fastxlsx::detail::validate_worksheet_editor_cell_range(fastxlsx::CellRange {
            2,
            1,
            1,
            2,
        });
    }), "range validation should reject inverted rows");
}

void test_public_snapshot_mapping_preserves_coordinates_and_values()
{
    const std::vector<fastxlsx::detail::MaterializedCellSnapshot> internal_snapshots {
        fastxlsx::detail::MaterializedCellSnapshot {
            fastxlsx::detail::CellPosition {2, 3},
            fastxlsx::CellValue::text("hello"),
        },
        fastxlsx::detail::MaterializedCellSnapshot {
            fastxlsx::detail::CellPosition {4, 5},
            fastxlsx::CellValue::number(42.0),
        },
    };

    const std::vector<fastxlsx::WorksheetCellSnapshot> public_snapshots =
        fastxlsx::detail::public_snapshots_from_materialized_cells(internal_snapshots);
    check(public_snapshots.size() == 2, "snapshot mapping should preserve size");
    check(public_snapshots[0].reference.row == 2 && public_snapshots[0].reference.column == 3,
        "snapshot mapping should preserve first coordinate");
    check(public_snapshots[0].value.kind() == fastxlsx::CellValueKind::Text &&
            public_snapshots[0].value.text_value() == "hello",
        "snapshot mapping should preserve text value");
    check(public_snapshots[1].reference.row == 4 && public_snapshots[1].reference.column == 5,
        "snapshot mapping should preserve second coordinate");
    check(public_snapshots[1].value.kind() == fastxlsx::CellValueKind::Number &&
            public_snapshots[1].value.number_value() == 42.0,
        "snapshot mapping should preserve number value");
}

} // namespace

int main()
{
    try {
        test_a1_parser_accepts_uppercase_excel_bounds();
        test_a1_parser_rejects_non_strict_references();
        test_coordinate_and_range_validation();
        test_public_snapshot_mapping_preserves_coordinates_and_values();
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
    return 0;
}
