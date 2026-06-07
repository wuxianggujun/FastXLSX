#!/usr/bin/env python3
"""Local image metadata QA for FastXLSX workbooks.

This helper is intentionally outside CTest. It verifies FastXLSX drawing XML
semantics directly, then uses Python XLSX libraries only as local QA/reference
tools. These libraries are not FastXLSX runtime dependencies.
"""

from __future__ import annotations

import argparse
import json
import sys
import tempfile
import zipfile
from pathlib import Path
from typing import Any
from xml.etree import ElementTree


EXPECTED_PICTURES: list[dict[str, str | None]] = [
    {
        "id": "1",
        "name": 'Logo "A&B<1>\'',
        "description": 'Alt "quoted" & <tag> \'owner\'',
        "edit_as": "oneCell",
        "from": "0,0,111,222",
        "to": "2,2,333,444",
    },
    {
        "id": "2",
        "name": "NamedOnly",
        "description": None,
        "edit_as": "absolute",
        "from": "0,2,0,0",
        "to": "2,4,0,0",
    },
    {
        "id": "3",
        "name": "Picture 3",
        "description": None,
        "edit_as": "twoCell",
        "from": "0,4,0,0",
        "to": "2,6,0,0",
    },
]

NAMESPACES = {
    "xdr": "http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing",
    "rel": "http://schemas.openxmlformats.org/package/2006/relationships",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read_zip_text(path: Path, name: str) -> str:
    with zipfile.ZipFile(path) as archive:
        with archive.open(name) as entry:
            return entry.read().decode("utf-8")


def read_zip_bytes(path: Path, name: str) -> bytes:
    with zipfile.ZipFile(path) as archive:
        with archive.open(name) as entry:
            return entry.read()


def zip_names(path: Path) -> set[str]:
    with zipfile.ZipFile(path) as archive:
        return set(archive.namelist())


def relationship_count(xml: str) -> int:
    root = ElementTree.fromstring(xml)
    return len(root.findall("rel:Relationship", NAMESPACES))


def marker_signature(anchor: ElementTree.Element, marker_name: str) -> str:
    marker = anchor.find(f"xdr:{marker_name}", NAMESPACES)
    require(marker is not None, f"missing xdr:{marker_name}")
    parts = []
    for child_name in ["col", "row", "colOff", "rowOff"]:
        child = marker.find(f"xdr:{child_name}", NAMESPACES)
        require(child is not None and child.text is not None,
                f"missing xdr:{marker_name}/xdr:{child_name}")
        parts.append(child.text)
    return ",".join(parts)


def verify_fastxlsx_package(path: Path) -> dict[str, Any]:
    names = zip_names(path)
    required = [
        "[Content_Types].xml",
        "_rels/.rels",
        "xl/workbook.xml",
        "xl/_rels/workbook.xml.rels",
        "xl/worksheets/sheet1.xml",
        "xl/worksheets/_rels/sheet1.xml.rels",
        "xl/drawings/drawing1.xml",
        "xl/drawings/_rels/drawing1.xml.rels",
        "xl/media/image1.png",
        "xl/media/image2.png",
        "xl/media/image3.png",
    ]
    for name in required:
        require(name in names, f"missing package entry: {name}")
    require("xl/drawings/drawing2.xml" not in names, "unexpected second drawing part")
    require("xl/worksheets/_rels/sheet2.xml.rels" not in names, "unexpected second worksheet rels")

    content_types = read_zip_text(path, "[Content_Types].xml")
    require(
        content_types.count('<Default Extension="png" ContentType="image/png"/>') == 1,
        "PNG content type default mismatch",
    )
    require(
        '<Override PartName="/xl/drawings/drawing1.xml" '
        'ContentType="application/vnd.openxmlformats-officedocument.drawing+xml"/>'
        in content_types,
        "drawing content type override missing",
    )

    worksheet_xml = read_zip_text(path, "xl/worksheets/sheet1.xml")
    require(
        '</sheetData><drawing r:id="rId1"/></worksheet>' in worksheet_xml,
        "worksheet drawing relationship id mismatch",
    )

    worksheet_rels = read_zip_text(path, "xl/worksheets/_rels/sheet1.xml.rels")
    require(relationship_count(worksheet_rels) == 1, "worksheet relationship count mismatch")
    require(
        'Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing" '
        'Target="../drawings/drawing1.xml"'
        in worksheet_rels,
        "worksheet drawing relationship target mismatch",
    )

    drawing_rels = read_zip_text(path, "xl/drawings/_rels/drawing1.xml.rels")
    require(relationship_count(drawing_rels) == 3, "drawing relationship count mismatch")
    for index in range(1, 4):
        require(
            f'Id="rId{index}"' in drawing_rels
            and f'Target="../media/image{index}.png"' in drawing_rels,
            f"drawing relationship rId{index} mismatch",
        )

    drawing_xml = read_zip_text(path, "xl/drawings/drawing1.xml")
    require(drawing_xml.count("<xdr:twoCellAnchor") == 3, "drawing anchor count mismatch")
    for edit_as in ["oneCell", "absolute", "twoCell"]:
        require(
            drawing_xml.count(f'editAs="{edit_as}"') == 1,
            f"drawing editAs {edit_as} count mismatch",
        )
    require(
        '<xdr:cNvPr id="1" name="Logo &quot;A&amp;B&lt;1&gt;&apos;" '
        'descr="Alt &quot;quoted&quot; &amp; &lt;tag&gt; &apos;owner&apos;"/>'
        in drawing_xml,
        "escaped image name/description XML mismatch",
    )
    require(
        '<xdr:cNvPr id="2" name="NamedOnly"/>' in drawing_xml,
        "named-only image XML mismatch",
    )
    require(
        '<xdr:cNvPr id="3" name="Picture 3"/>' in drawing_xml,
        "default image name XML mismatch",
    )
    require('name="NamedOnly" descr=' not in drawing_xml, "empty named-only description was emitted")
    require('name="Picture 3" descr=' not in drawing_xml, "empty default description was emitted")

    root = ElementTree.fromstring(drawing_xml)
    anchors = root.findall("xdr:twoCellAnchor", NAMESPACES)
    require(len(anchors) == 3, "parsed drawing anchor count mismatch")

    parsed: list[dict[str, str | None]] = []
    for anchor in anchors:
        properties = anchor.find("xdr:pic/xdr:nvPicPr/xdr:cNvPr", NAMESPACES)
        require(properties is not None, "missing xdr:cNvPr")
        parsed.append(
            {
                "id": properties.attrib.get("id"),
                "name": properties.attrib.get("name"),
                "description": properties.attrib.get("descr"),
                "edit_as": anchor.attrib.get("editAs"),
                "from": marker_signature(anchor, "from"),
                "to": marker_signature(anchor, "to"),
            }
        )
    require(parsed == EXPECTED_PICTURES, f"parsed image metadata mismatch: {parsed!r}")

    media_sizes = {
        name: len(read_zip_bytes(path, name))
        for name in ["xl/media/image1.png", "xl/media/image2.png", "xl/media/image3.png"]
    }
    for name, size in media_sizes.items():
        require(size > 0, f"empty media part: {name}")

    return {
        "verified_entries": required,
        "pictures": parsed,
        "media_sizes": media_sizes,
    }


def verify_with_openpyxl(path: Path) -> dict[str, Any]:
    try:
        import openpyxl  # type: ignore
    except ModuleNotFoundError:
        return {"status": "skipped", "reason": "Python module openpyxl is not installed"}

    workbook = openpyxl.load_workbook(path, read_only=False, data_only=False)
    try:
        require("ImageMetadata" in workbook.sheetnames, "missing ImageMetadata worksheet")
        worksheet = workbook["ImageMetadata"]
        images = list(getattr(worksheet, "_images", []))
        require(len(images) == 3, f"openpyxl image count mismatch: {len(images)}")
        return {
            "status": "opened",
            "sheetnames": workbook.sheetnames,
            "image_count": len(images),
        }
    finally:
        workbook.close()


def create_xlsxwriter_reference(media_file: Path, path: Path) -> dict[str, Any]:
    try:
        import xlsxwriter  # type: ignore
    except ModuleNotFoundError:
        return {"status": "skipped", "reason": "Python module xlsxwriter is not installed"}

    workbook = xlsxwriter.Workbook(str(path))
    worksheet = workbook.add_worksheet("ImageMetadata")
    worksheet.write("A1", "Reference image")
    worksheet.insert_image(
        "A2",
        str(media_file),
        {"description": 'Alt "quoted" & <tag> \'owner\''},
    )
    workbook.close()
    return {"status": "created", "path": str(path)}


def extract_reference_media(input_path: Path, work_dir: Path) -> Path:
    media_path = work_dir / "image1.png"
    media_path.write_bytes(read_zip_bytes(input_path, "xl/media/image1.png"))
    return media_path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input",
        type=Path,
        default=Path("build/windows-nmake-release/tests/fastxlsx-streaming-image-metadata.xlsx"),
        help="FastXLSX image metadata workbook to verify.",
    )
    parser.add_argument(
        "--work-dir",
        type=Path,
        default=Path("build/qa/image-metadata"),
        help="Directory for extracted media, reference files, and the JSON report.",
    )
    args = parser.parse_args()

    input_path = args.input.resolve()
    work_dir = args.work_dir.resolve()
    require(input_path.exists(), f"input workbook does not exist: {input_path}")
    work_dir.mkdir(parents=True, exist_ok=True)

    package_report = verify_fastxlsx_package(input_path)
    openpyxl_report = verify_with_openpyxl(input_path)

    with tempfile.TemporaryDirectory(dir=work_dir) as temp_dir:
        media_file = extract_reference_media(input_path, Path(temp_dir))
        xlsxwriter_report = create_xlsxwriter_reference(
            media_file, work_dir / "reference-xlsxwriter-image-metadata.xlsx")

    report = {
        "fastxlsx_input": str(input_path),
        "fastxlsx_package": package_report,
        "xlsx_libraries": {
            "openpyxl": openpyxl_report,
            "xlsxwriter": xlsxwriter_report,
        },
    }
    report_path = work_dir / "report.json"
    report_path.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # pragma: no cover - local QA helper
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
