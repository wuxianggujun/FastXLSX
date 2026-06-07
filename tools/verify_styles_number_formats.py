#!/usr/bin/env python3
"""Local QA for FastXLSX streaming number-format styles.

This helper is intentionally local QA, not a runtime dependency and not a
default CI gate. It checks the generated OpenXML package, then uses openpyxl as
a reader-visible semantic check and XlsxWriter as an optional reference writer.
"""

from __future__ import annotations

import argparse
import json
import sys
import zipfile
from pathlib import Path
from typing import Any


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read_zip_text(path: Path, name: str) -> str:
    with zipfile.ZipFile(path) as archive:
        return archive.read(name).decode("utf-8")


def zip_names(path: Path) -> set[str]:
    with zipfile.ZipFile(path) as archive:
        return set(archive.namelist())


def count(text: str, fragment: str) -> int:
    return text.count(fragment)


def verify_styles_package(path: Path) -> dict[str, Any]:
    names = zip_names(path)
    required = [
        "[Content_Types].xml",
        "xl/workbook.xml",
        "xl/_rels/workbook.xml.rels",
        "xl/worksheets/sheet1.xml",
        "xl/styles.xml",
    ]
    for name in required:
        require(name in names, f"styles sample missing package entry: {name}")

    require("xl/worksheets/_rels/sheet1.xml.rels" not in names,
            "styles sample should not create worksheet relationships")
    require("xl/sharedStrings.xml" not in names,
            "styles number-format sample should not create sharedStrings.xml")

    content_types = read_zip_text(path, "[Content_Types].xml")
    require(
        '<Override PartName="/xl/styles.xml" '
        'ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/>'
        in content_types,
        "styles content type override missing",
    )

    workbook_rels = read_zip_text(path, "xl/_rels/workbook.xml.rels")
    require(count(workbook_rels, "<Relationship ") == 2,
            "styles workbook relationship count mismatch")
    require(
        'Id="rId2" '
        'Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" '
        'Target="styles.xml"' in workbook_rels,
        "styles workbook relationship mismatch",
    )

    styles_xml = read_zip_text(path, "xl/styles.xml")
    require('<numFmts count="2">' in styles_xml, "custom numFmt count mismatch")
    require('<numFmt numFmtId="164" formatCode="$#,##0.00"/>' in styles_xml,
            "currency number format mismatch")
    require(
        '<numFmt numFmtId="165" formatCode="0.00 &quot;kg &amp; &lt;unit&gt;&quot;"/>'
        in styles_xml,
        "escaped number format mismatch",
    )
    require('<fonts count="1">' in styles_xml, "default fonts missing")
    require('<fills count="2">' in styles_xml, "default fills missing")
    require('<borders count="1">' in styles_xml, "default borders missing")
    require('<cellXfs count="3">' in styles_xml, "cellXfs count mismatch")
    require('numFmtId="164" fontId="0" fillId="0" borderId="0" xfId="0" '
            'applyNumberFormat="1"' in styles_xml,
            "first style xf mismatch")
    require('numFmtId="165" fontId="0" fillId="0" borderId="0" xfId="0" '
            'applyNumberFormat="1"' in styles_xml,
            "second style xf mismatch")

    worksheet_xml = read_zip_text(path, "xl/worksheets/sheet1.xml")
    require('<dimension ref="A1:F2"/>' in worksheet_xml, "worksheet dimension mismatch")
    require('<c r="A2" s="1"><v>1234.5</v></c>' in worksheet_xml,
            "styled currency cell mismatch")
    require('<c r="B2" s="2"><v>7.25</v></c>' in worksheet_xml,
            "styled escaped-format cell mismatch")
    require('<c r="C2"><v>9</v></c>' in worksheet_xml,
            "default numeric cell should omit style attribute")
    require('<c r="D2" s="2" t="inlineStr"><is><t>styled text</t></is></c>'
            in worksheet_xml,
            "styled inlineStr cell mismatch")
    require('<c r="E2" s="1" t="b"><v>1</v></c>' in worksheet_xml,
            "styled boolean cell mismatch")
    require('<c r="F2" s="1"><f>A2*2</f></c>' in worksheet_xml,
            "styled formula cell mismatch")
    require('s="0"' not in worksheet_xml, "default style should not be serialized as s=\"0\"")

    return {
        "verified_entries": required,
        "style_ids": {"currency": 1, "escaped_number_format": 2},
        "custom_number_format_ids": [164, 165],
    }


def verify_shared_styles_package(path: Path) -> dict[str, Any]:
    names = zip_names(path)
    for name in ["xl/sharedStrings.xml", "xl/styles.xml", "xl/worksheets/sheet1.xml"]:
        require(name in names, f"shared styles sample missing package entry: {name}")
    require("xl/worksheets/_rels/sheet1.xml.rels" not in names,
            "shared styles sample should not create worksheet relationships")

    workbook_rels = read_zip_text(path, "xl/_rels/workbook.xml.rels")
    require('Id="rId2" '
            'Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" '
            'Target="sharedStrings.xml"' in workbook_rels,
            "sharedStrings relationship should precede styles")
    require('Id="rId3" '
            'Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" '
            'Target="styles.xml"' in workbook_rels,
            "styles relationship should follow sharedStrings")

    worksheet_xml = read_zip_text(path, "xl/worksheets/sheet1.xml")
    require('<c r="A1" s="1" t="s"><v>0</v></c>' in worksheet_xml,
            "styled shared string cell mismatch")
    require('<c r="B1" t="s"><v>1</v></c>' in worksheet_xml,
            "plain shared string cell mismatch")

    return {
        "relationship_model": "workbook-local sheet, sharedStrings, then styles ids",
    }


def verify_with_openpyxl(path: Path, shared_path: Path) -> dict[str, Any]:
    try:
        import openpyxl  # type: ignore
    except ModuleNotFoundError:
        return {"status": "skipped", "reason": "Python module openpyxl is not installed"}

    workbook = openpyxl.load_workbook(path, read_only=False, data_only=False)
    try:
        sheet = workbook["Styles"]
        require(sheet["A2"].number_format == "$#,##0.00",
                f"A2 number format mismatch: {sheet['A2'].number_format!r}")
        require(sheet["B2"].number_format == '0.00 "kg & <unit>"',
                f"B2 number format mismatch: {sheet['B2'].number_format!r}")
        require(sheet["C2"].number_format == "General",
                f"C2 default number format mismatch: {sheet['C2'].number_format!r}")
        require(sheet["D2"].number_format == '0.00 "kg & <unit>"',
                f"D2 string style number format mismatch: {sheet['D2'].number_format!r}")
        require(sheet["F2"].data_type == "f", "F2 should remain a formula cell")
        require(sheet["F2"].number_format == "$#,##0.00",
                f"F2 formula style mismatch: {sheet['F2'].number_format!r}")
    finally:
        workbook.close()

    shared_workbook = openpyxl.load_workbook(shared_path, read_only=False, data_only=False)
    try:
        shared = shared_workbook["StyledShared"]
        require(shared["A1"].value == "styled shared", "styled shared string value mismatch")
        require(shared["A1"].number_format == "@",
                f"styled shared string format mismatch: {shared['A1'].number_format!r}")
        require(shared["B1"].number_format == "General",
                f"plain shared string format mismatch: {shared['B1'].number_format!r}")
    finally:
        shared_workbook.close()

    return {
        "status": "opened",
        "styles": {
            "A2": "$#,##0.00",
            "B2": '0.00 "kg & <unit>"',
            "C2": "General",
            "F2": "$#,##0.00",
        },
        "shared_styles": {"A1": "@", "B1": "General"},
    }


def create_xlsxwriter_reference(path: Path) -> dict[str, Any]:
    try:
        import xlsxwriter  # type: ignore
    except ModuleNotFoundError:
        return {"status": "skipped", "reason": "Python module xlsxwriter is not installed"}

    workbook = xlsxwriter.Workbook(path)
    try:
        sheet = workbook.add_worksheet("Styles")
        currency = workbook.add_format({"num_format": "$#,##0.00"})
        escaped = workbook.add_format({"num_format": '0.00 "kg & <unit>"'})
        sheet.write_row(0, 0, ["Currency", "Escaped", "Default", "Text", "Bool", "Formula"])
        sheet.write_number(1, 0, 1234.5, currency)
        sheet.write_number(1, 1, 7.25, escaped)
        sheet.write_number(1, 2, 9.0)
        sheet.write_string(1, 3, "styled text", escaped)
        sheet.write_boolean(1, 4, True, currency)
        sheet.write_formula(1, 5, "=A2*2", currency)
    finally:
        workbook.close()

    return {"status": "created", "path": str(path)}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--input",
        type=Path,
        default=Path("build/windows-nmake-release/tests/fastxlsx-streaming-styles-number-formats.xlsx"),
        help="FastXLSX styles workbook to verify.",
    )
    parser.add_argument(
        "--shared-input",
        type=Path,
        default=Path("build/windows-nmake-release/tests/fastxlsx-streaming-styles-shared-strings.xlsx"),
        help="FastXLSX sharedStrings + styles workbook to verify.",
    )
    parser.add_argument(
        "--work-dir",
        type=Path,
        default=Path("build/qa/styles-number-formats"),
        help="Directory for local QA reports and reference workbooks.",
    )
    args = parser.parse_args()

    input_path = args.input.resolve()
    shared_input_path = args.shared_input.resolve()
    work_dir = args.work_dir.resolve()
    require(input_path.exists(), f"input workbook does not exist: {input_path}")
    require(shared_input_path.exists(), f"shared input workbook does not exist: {shared_input_path}")
    work_dir.mkdir(parents=True, exist_ok=True)

    report = {
        "fastxlsx_input": str(input_path),
        "fastxlsx_shared_input": str(shared_input_path),
        "fastxlsx_package": verify_styles_package(input_path),
        "fastxlsx_shared_package": verify_shared_styles_package(shared_input_path),
        "xlsx_libraries": {
            "openpyxl": verify_with_openpyxl(input_path, shared_input_path),
            "xlsxwriter": create_xlsxwriter_reference(
                work_dir / "reference-xlsxwriter-styles-number-formats.xlsx"),
        },
    }

    report_path = work_dir / "report.json"
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:  # noqa: BLE001
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
