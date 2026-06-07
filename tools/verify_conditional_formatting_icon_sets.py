#!/usr/bin/env python3
"""Local QA for FastXLSX conditional-formatting icon sets.

This helper is intentionally local QA, not a runtime dependency and not a
default CI gate. It treats generated OpenXML as the source of truth, then uses
openpyxl as a reader-visible semantic check and XlsxWriter as an optional
reference writer for troubleshooting.
"""

from __future__ import annotations

import argparse
import json
import sys
import zipfile
from pathlib import Path
from typing import Any


EXPECTED_BASIC_SQREF = "A2:A10"
EXPECTED_MULTI_RANGE_SQREF = "A2:A3 C2:C3 E2:E3"
EXPECTED_ICON_SET = "3Arrows"
EXPECTED_CFVO = [("percent", 0.0), ("percent", 33.0), ("percent", 67.0)]
EXPECTED_PERCENTILE_CFVO = [("percentile", 10.0), ("percentile", 50.0), ("percentile", 90.0)]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read_zip_text(path: Path, name: str) -> str:
    with zipfile.ZipFile(path) as archive:
        return archive.read(name).decode("utf-8")


def zip_names(path: Path) -> set[str]:
    with zipfile.ZipFile(path) as archive:
        return set(archive.namelist())


def worksheet_relationship_entries(names: set[str]) -> list[str]:
    return sorted(
        name
        for name in names
        if name.startswith("xl/worksheets/_rels/") and name.endswith(".xml.rels")
    )


def verify_no_package_side_effects(path: Path, worksheet_rels_allowed: bool = False) -> dict[str, Any]:
    names = zip_names(path)
    required = [
        "[Content_Types].xml",
        "_rels/.rels",
        "docProps/core.xml",
        "docProps/app.xml",
        "xl/workbook.xml",
        "xl/_rels/workbook.xml.rels",
        "xl/worksheets/sheet1.xml",
    ]
    for name in required:
        require(name in names, f"missing package entry: {name}")

    forbidden = ["xl/styles.xml", "xl/metadata.xml", "xl/calcChain.xml"]
    rel_entries = worksheet_relationship_entries(names)
    if not worksheet_rels_allowed:
        require(not rel_entries, f"unexpected worksheet relationship entries: {rel_entries}")
    for name in forbidden:
        require(name not in names, f"unexpected package entry: {name}")

    content_types = read_zip_text(path, "[Content_Types].xml")
    for fragment in ["conditionalFormatting", "styles", "metadata"]:
        require(fragment not in content_types, f"unexpected content type fragment: {fragment}")

    workbook_rels = read_zip_text(path, "xl/_rels/workbook.xml.rels")
    require("styles" not in workbook_rels, "icon set should not add styles relationship")
    require("conditionalFormatting" not in workbook_rels,
            "icon set should not add workbook relationship")

    workbook_xml = read_zip_text(path, "xl/workbook.xml")
    require("<calcPr" not in workbook_xml, "icon set should not request recalculation")

    return {
        "required_entries": required,
        "forbidden_entries_absent": forbidden,
        "worksheet_relationship_entries": rel_entries,
    }


def expected_cfvo_fragment(expected_cfvo: list[tuple[str, float]]) -> str:
    return "".join(
        f'<cfvo type="{value_type}" val="{value:g}"/>'
        for value_type, value in expected_cfvo
    )


def expected_icon_set_fragment(
    sqref: str,
    priority: int = 1,
    attributes: str = "",
    expected_cfvo: list[tuple[str, float]] | None = None,
) -> str:
    if expected_cfvo is None:
        expected_cfvo = EXPECTED_CFVO
    return (
        f'<conditionalFormatting sqref="{sqref}">'
        f'<cfRule type="iconSet" priority="{priority}">'
        f'<iconSet iconSet="{EXPECTED_ICON_SET}"{attributes}>'
        f'{expected_cfvo_fragment(expected_cfvo)}</iconSet></cfRule></conditionalFormatting>'
    )


def verify_basic_package(path: Path) -> dict[str, Any]:
    side_effects = verify_no_package_side_effects(path)
    worksheet_xml = read_zip_text(path, "xl/worksheets/sheet1.xml")
    require("xmlns:r=" not in worksheet_xml,
            "icon-set-only worksheet should not declare relationship namespace")
    require('<dimension ref="A1:A10"/>' in worksheet_xml, "basic worksheet dimension mismatch")
    require(expected_icon_set_fragment(EXPECTED_BASIC_SQREF) in worksheet_xml,
            "basic icon set XML fragment mismatch")
    require(worksheet_xml.count("<conditionalFormatting ") == 1,
            "basic conditionalFormatting count mismatch")
    require(worksheet_xml.count("<cfRule ") == 1, "basic cfRule count mismatch")
    require(worksheet_xml.count("<iconSet ") == 1, "basic iconSet count mismatch")
    require(worksheet_xml.count("<cfvo ") == 3, "basic cfvo count mismatch")
    for forbidden_fragment in [
        "dxfId=",
        "<colorScale",
        "<dataBar",
        "<formula>",
        "<dxfs",
        "<extLst",
        "custom",
    ]:
        require(forbidden_fragment not in worksheet_xml,
                f"unexpected unsupported conditional-formatting fragment: {forbidden_fragment}")
    return {"side_effects": side_effects, "sqref": EXPECTED_BASIC_SQREF}


def verify_metadata_order_package(path: Path) -> dict[str, Any]:
    verify_no_package_side_effects(path, worksheet_rels_allowed=True)
    names = zip_names(path)
    require("xl/worksheets/_rels/sheet1.xml.rels" in names,
            "metadata-order workbook should keep relationship-backed objects")
    require("xl/tables/table1.xml" in names, "metadata-order workbook should keep table part")

    worksheet_xml = read_zip_text(path, "xl/worksheets/sheet1.xml")
    expected_order = (
        '</sheetData><mergeCells count="1"><mergeCell ref="A4:B4"/></mergeCells>'
        + expected_icon_set_fragment(
            EXPECTED_BASIC_SQREF,
            attributes=' showValue="0" reverse="1"',
        )
        + '<dataValidations count="1">'
    )
    require(expected_order in worksheet_xml,
            "icon set should be between mergeCells and dataValidations")
    require(
        '<hyperlinks><hyperlink ref="B2" r:id="rId1"/></hyperlinks>'
        '<tableParts count="1"><tablePart r:id="rId2"/></tableParts>' in worksheet_xml,
        "icon set should not consume worksheet-local relationship ids",
    )

    worksheet_rels = read_zip_text(path, "xl/worksheets/_rels/sheet1.xml.rels")
    require(worksheet_rels.count("<Relationship ") == 2,
            "metadata-order worksheet relationship count mismatch")
    require('Id="rId1"' in worksheet_rels and "relationships/hyperlink" in worksheet_rels,
            "hyperlink relationship should remain rId1")
    require('Id="rId2"' in worksheet_rels and "relationships/table" in worksheet_rels,
            "table relationship should remain rId2")
    return {"suffix_order": "mergeCells -> conditionalFormatting -> dataValidations"}


def verify_percentile_package(path: Path) -> dict[str, Any]:
    side_effects = verify_no_package_side_effects(path)
    worksheet_xml = read_zip_text(path, "xl/worksheets/sheet1.xml")
    require("xmlns:r=" not in worksheet_xml,
            "percentile icon set should not declare relationship namespace")
    require('<dimension ref="A1:A10"/>' in worksheet_xml,
            "percentile icon set worksheet dimension mismatch")
    require(
        expected_icon_set_fragment(
            EXPECTED_BASIC_SQREF,
            attributes=' showValue="0" reverse="1"',
            expected_cfvo=EXPECTED_PERCENTILE_CFVO,
        ) in worksheet_xml,
        "percentile icon set XML fragment mismatch",
    )
    require(worksheet_xml.count("<conditionalFormatting ") == 1,
            "percentile conditionalFormatting count mismatch")
    require(worksheet_xml.count("<cfRule ") == 1, "percentile cfRule count mismatch")
    require(worksheet_xml.count("<iconSet ") == 1, "percentile iconSet count mismatch")
    require(worksheet_xml.count("<cfvo ") == 3, "percentile cfvo count mismatch")
    return {
        "side_effects": side_effects,
        "sqref": EXPECTED_BASIC_SQREF,
        "cfvo": EXPECTED_PERCENTILE_CFVO,
        "show_value": False,
        "reverse": True,
    }


def verify_multi_range_package(path: Path) -> dict[str, Any]:
    side_effects = verify_no_package_side_effects(path)
    worksheet_xml = read_zip_text(path, "xl/worksheets/sheet1.xml")
    require("xmlns:r=" not in worksheet_xml,
            "multi-range icon set should not declare relationship namespace")
    require('<dimension ref="A1:E3"/>' in worksheet_xml, "multi-range worksheet dimension mismatch")
    require(expected_icon_set_fragment(EXPECTED_MULTI_RANGE_SQREF) in worksheet_xml,
            "multi-range icon set XML fragment mismatch")
    require(worksheet_xml.count("<conditionalFormatting ") == 1,
            "multi-range conditionalFormatting count mismatch")
    require(worksheet_xml.count("<cfRule ") == 1, "multi-range cfRule count mismatch")
    return {"side_effects": side_effects, "sqref": EXPECTED_MULTI_RANGE_SQREF}


def verify_priorities_package(path: Path) -> dict[str, Any]:
    verify_no_package_side_effects(path)
    first_xml = read_zip_text(path, "xl/worksheets/sheet1.xml")
    second_xml = read_zip_text(path, "xl/worksheets/sheet2.xml")

    require(first_xml.count("<conditionalFormatting ") == 3,
            "first worksheet conditional formatting count mismatch")
    require('<cfRule type="colorScale" priority="1">' in first_xml,
            "first worksheet color scale priority mismatch")
    require('<cfRule type="dataBar" priority="2">' in first_xml,
            "first worksheet data bar priority mismatch")
    require('<cfRule type="iconSet" priority="3">' in first_xml,
            "first worksheet icon set priority mismatch")
    require(
        '<cfvo type="num" val="0"/><cfvo type="num" val="5"/>'
        '<cfvo type="num" val="10"/>' in first_xml,
        "numeric icon set threshold XML mismatch",
    )
    require(second_xml.count("<conditionalFormatting ") == 1,
            "second worksheet conditional formatting count mismatch")
    require('<cfRule type="iconSet" priority="1">' in second_xml,
            "second worksheet priority should reset")
    require('priority="2"' not in second_xml,
            "second worksheet should not inherit first worksheet priority")
    return {"first_sheet_priorities": [1, 2, 3], "second_sheet_priorities": [1]}


def extract_openpyxl_rule(
    path: Path,
    sheet_name: str,
    expected_sqref: str,
    expected_cfvo: list[tuple[str, float]] | None = None,
    expected_show_value: bool | None = None,
    expected_reverse: bool | None = None,
) -> dict[str, Any]:
    import openpyxl  # type: ignore
    if expected_cfvo is None:
        expected_cfvo = EXPECTED_CFVO

    workbook = openpyxl.load_workbook(path, read_only=False, data_only=False)
    try:
        worksheet = workbook[sheet_name]
        rules = list(worksheet.conditional_formatting)
        require(len(rules) == 1, f"expected 1 conditional formatting range, got {len(rules)}")
        cf_range = rules[0]
        sqref = str(getattr(cf_range, "sqref", cf_range))
        require(sqref == expected_sqref,
                f"conditional formatting sqref mismatch: {sqref}")
        rule = worksheet.conditional_formatting[cf_range][0]
        require(rule.type == "iconSet", f"rule type mismatch: {rule.type!r}")
        require(rule.priority == 1, f"rule priority mismatch: {rule.priority!r}")
        icon_set = rule.iconSet
        require(icon_set.iconSet == EXPECTED_ICON_SET,
                f"openpyxl icon set mismatch: {icon_set.iconSet!r}")
        cfvo = [(point.type, float(point.val)) for point in icon_set.cfvo]
        require(cfvo == expected_cfvo, f"openpyxl cfvo mismatch: {cfvo!r}")
        if expected_show_value is not None:
            require(icon_set.showValue == expected_show_value,
                    f"openpyxl showValue mismatch: {icon_set.showValue!r}")
        if expected_reverse is not None:
            require(icon_set.reverse == expected_reverse,
                    f"openpyxl reverse mismatch: {icon_set.reverse!r}")
        return {
            "status": "opened",
            "sqref": sqref,
            "priority": rule.priority,
            "icon_set": icon_set.iconSet,
            "cfvo": cfvo,
            "show_value": icon_set.showValue,
            "reverse": icon_set.reverse,
        }
    finally:
        workbook.close()


def verify_openpyxl_basic(path: Path) -> dict[str, Any]:
    try:
        return extract_openpyxl_rule(path, "IconSet", EXPECTED_BASIC_SQREF)
    except ModuleNotFoundError:
        return {"status": "skipped", "reason": "Python module openpyxl is not installed"}


def verify_openpyxl_multi_range(path: Path) -> dict[str, Any]:
    try:
        return extract_openpyxl_rule(path, "IconSetRanges", EXPECTED_MULTI_RANGE_SQREF)
    except ModuleNotFoundError:
        return {"status": "skipped", "reason": "Python module openpyxl is not installed"}


def verify_openpyxl_percentile(path: Path) -> dict[str, Any]:
    try:
        return extract_openpyxl_rule(
            path,
            "IconSetPercentile",
            EXPECTED_BASIC_SQREF,
            EXPECTED_PERCENTILE_CFVO,
            expected_show_value=False,
            expected_reverse=True,
        )
    except ModuleNotFoundError:
        return {"status": "skipped", "reason": "Python module openpyxl is not installed"}


def create_xlsxwriter_reference(path: Path) -> dict[str, Any]:
    try:
        import xlsxwriter  # type: ignore
    except ModuleNotFoundError:
        return {"status": "skipped", "reason": "Python module xlsxwriter is not installed"}

    workbook = xlsxwriter.Workbook(str(path))
    try:
        worksheet = workbook.add_worksheet("IconSet")
        worksheet.write(0, 0, "Score")
        for index, value in enumerate(range(1, 10), start=1):
            worksheet.write_number(index, 0, value)
        worksheet.conditional_format(
            "A2:A10",
            {
                "type": "icon_set",
                "icon_style": "3_arrows",
            },
        )
    finally:
        workbook.close()

    worksheet_xml = read_zip_text(path, "xl/worksheets/sheet1.xml")
    require("<conditionalFormatting" in worksheet_xml,
            "XlsxWriter reference missing conditionalFormatting")
    require('iconSet iconSet="3Arrows"' in worksheet_xml,
            "XlsxWriter reference missing 3Arrows icon set")
    return {"status": "created", "path": str(path)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input",
        type=Path,
        default=Path(
            "build/windows-nmake-release/tests/"
            "fastxlsx-streaming-conditional-formatting-icon-set.xlsx"
        ),
        help="FastXLSX basic icon-set workbook to verify.",
    )
    parser.add_argument(
        "--metadata-order-input",
        type=Path,
        default=Path(
            "build/windows-nmake-release/tests/"
            "fastxlsx-streaming-conditional-formatting-icon-set-metadata-order.xlsx"
        ),
        help="FastXLSX icon set + relationship-backed metadata workbook.",
    )
    parser.add_argument(
        "--percentile-input",
        type=Path,
        default=Path(
            "build/windows-nmake-release/tests/"
            "fastxlsx-streaming-conditional-formatting-icon-set-percentile.xlsx"
        ),
        help="FastXLSX percentile-threshold conditional-formatting icon set workbook.",
    )
    parser.add_argument(
        "--multi-range-input",
        type=Path,
        default=Path(
            "build/windows-nmake-release/tests/"
            "fastxlsx-streaming-conditional-formatting-icon-set-multi-range.xlsx"
        ),
        help="FastXLSX multi-range conditional-formatting icon set workbook.",
    )
    parser.add_argument(
        "--priorities-input",
        type=Path,
        default=Path(
            "build/windows-nmake-release/tests/"
            "fastxlsx-streaming-conditional-formatting-icon-set-priorities.xlsx"
        ),
        help="FastXLSX workbook that verifies mixed color-scale/data-bar/icon-set priorities.",
    )
    parser.add_argument(
        "--work-dir",
        type=Path,
        default=Path("build/qa/conditional-formatting-icon-sets"),
        help="Directory for local QA reports and reference workbooks.",
    )
    args = parser.parse_args()

    input_path = args.input.resolve()
    metadata_order_path = args.metadata_order_input.resolve()
    percentile_path = args.percentile_input.resolve()
    multi_range_path = args.multi_range_input.resolve()
    priorities_path = args.priorities_input.resolve()
    work_dir = args.work_dir.resolve()
    for path in [input_path, metadata_order_path, percentile_path, multi_range_path, priorities_path]:
        require(path.exists(), f"input workbook does not exist: {path}")
    work_dir.mkdir(parents=True, exist_ok=True)

    report = {
        "fastxlsx_input": str(input_path),
        "fastxlsx_package": verify_basic_package(input_path),
        "metadata_order_input": str(metadata_order_path),
        "metadata_order": verify_metadata_order_package(metadata_order_path),
        "percentile_input": str(percentile_path),
        "percentile": verify_percentile_package(percentile_path),
        "multi_range_input": str(multi_range_path),
        "multi_range": verify_multi_range_package(multi_range_path),
        "priorities_input": str(priorities_path),
        "priorities": verify_priorities_package(priorities_path),
        "xlsx_libraries": {
            "openpyxl": verify_openpyxl_basic(input_path),
            "openpyxl_percentile": verify_openpyxl_percentile(percentile_path),
            "openpyxl_multi_range": verify_openpyxl_multi_range(multi_range_path),
            "xlsxwriter": create_xlsxwriter_reference(
                work_dir / "reference-xlsxwriter-conditional-formatting-icon-set.xlsx"
            ),
        },
    }

    report_path = work_dir / "report.json"
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise
