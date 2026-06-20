#!/usr/bin/env python3
"""Local workbook-editor QA runner for FastXLSX.

This helper is intentionally outside CTest. It drives the opt-in
`fastxlsx_workbook_editor_qa_tool`, then validates the produced workbooks with
ZIP/XML checks, openpyxl readback, and optional XlsxWriter-generated reference
workbooks for generated scenarios.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from xml.etree import ElementTree


NAMESPACES = {
    "main": "http://schemas.openxmlformats.org/spreadsheetml/2006/main",
}

GENERATED_SCENARIOS = [
    "generated_rename_materialized",
    "generated_style_passthrough",
    "generated_image_replace",
    "generated_public_e2e",
    "generated_non_default_style_rejection",
]

DEFAULT_XLNT_RENAME_FIXTURES = [
    "2_minimal.xlsx",
    "3_default.xlsx",
    "20_active_sheet.xlsx",
]

DEFAULT_XLNT_STRING_FIXTURES = [
    "Issue445_inline_str.xlsx",
    "Issue494_shared_string.xlsx",
]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def zip_names(path: Path) -> set[str]:
    with zipfile.ZipFile(path) as archive:
        return set(archive.namelist())


def read_zip_text(path: Path, name: str) -> str:
    with zipfile.ZipFile(path) as archive:
        with archive.open(name) as entry:
            return entry.read().decode("utf-8")


def read_zip_bytes(path: Path, name: str) -> bytes:
    with zipfile.ZipFile(path) as archive:
        with archive.open(name) as entry:
            return entry.read()


def resolve_default_qa_exe() -> Path | None:
    candidates = [
        Path("build/windows-nmake-release-minizip/tools/fastxlsx_workbook_editor_qa_tool.exe"),
        Path("build/windows-nmake-release/tools/fastxlsx_workbook_editor_qa_tool.exe"),
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate

    for candidate in Path("build").rglob("fastxlsx_workbook_editor_qa_tool.exe"):
        return candidate
    return None


def resolve_default_excel_script() -> Path:
    return Path(__file__).resolve().with_name("verify_workbook_editor_qa_excel.ps1")


def choose_unique_sheet_name(existing: list[str], preferred: str = "QA_Renamed") -> str:
    existing_lower = {name.lower() for name in existing}
    candidate = preferred[:31]
    if candidate.lower() not in existing_lower:
        return candidate

    for index in range(1, 1000):
        suffix = f"_{index}"
        candidate = preferred[: 31 - len(suffix)] + suffix
        if candidate.lower() not in existing_lower:
            return candidate

    raise RuntimeError("failed to allocate a unique worksheet name")


@dataclass
class ScenarioResult:
    name: str
    report: dict[str, Any]
    zip_xml: dict[str, Any]
    openpyxl: dict[str, Any]
    xlsxwriter_reference: dict[str, Any]
    error: str = ""


def load_openpyxl():
    try:
        import openpyxl  # type: ignore
    except ModuleNotFoundError as exc:  # pragma: no cover - environment dependent
        raise RuntimeError("openpyxl is required for workbook-editor QA") from exc
    return openpyxl


def load_xlsxwriter():
    try:
        import xlsxwriter  # type: ignore
    except ModuleNotFoundError:
        return None
    return xlsxwriter


def openpyxl_image_count(worksheet: Any) -> int:
    return len(getattr(worksheet, "_images", []))


def run_tool(
    qa_exe: Path,
    scenario: str,
    case_dir: Path,
    *,
    source: Path | None = None,
    output: Path | None = None,
    sheet_name: str | None = None,
    rename_to: str | None = None,
    replacement_image: Path | None = None,
) -> dict[str, Any]:
    case_dir.mkdir(parents=True, exist_ok=True)
    report_path = case_dir / "tool-report.json"
    output_path = output if output is not None else case_dir / "output.xlsx"
    args = [
        str(qa_exe),
        "--scenario",
        scenario,
        "--output",
        str(output_path),
        "--report",
        str(report_path),
    ]
    if source is not None:
        args.extend(["--source", str(source)])
    if sheet_name:
        args.extend(["--sheet", sheet_name])
    if rename_to:
        args.extend(["--rename-to", rename_to])
    if replacement_image is not None:
        args.extend(["--replacement-image", str(replacement_image)])

    completed = subprocess.run(
        args,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"{scenario}: tool failed with code {completed.returncode}\n"
            f"stdout:\n{completed.stdout}\n"
            f"stderr:\n{completed.stderr}"
        )

    require(report_path.exists(), f"{scenario}: tool did not write report: {report_path}")
    with report_path.open("r", encoding="utf-8") as stream:
        report = json.load(stream)
    return report


def verify_generated_rename_materialized(path: Path) -> tuple[dict[str, Any], dict[str, Any]]:
    zip_report: dict[str, Any] = {}
    names = zip_names(path)
    require("xl/workbook.xml" in names, "generated rename/materialized: missing workbook.xml")
    workbook_xml = read_zip_text(path, "xl/workbook.xml")
    sheet_xml = read_zip_text(path, "xl/worksheets/sheet1.xml")
    require('name="EditedData"' in workbook_xml, "generated rename/materialized: missing renamed sheet")
    require('name="Data"' not in workbook_xml, "generated rename/materialized: old sheet name still present")
    require("materialized-edit" in sheet_xml, "generated rename/materialized: missing edited A1 text")
    require('<c r="B2"><v>42</v></c>' in sheet_xml, "generated rename/materialized: missing edited B2 number")
    zip_report["sheet_xml"] = "checked"

    openpyxl = load_openpyxl()
    workbook = openpyxl.load_workbook(path, read_only=False, data_only=False)
    try:
        require(workbook.sheetnames == ["EditedData", "Untouched"],
                f"generated rename/materialized: unexpected sheetnames {workbook.sheetnames!r}")
        edited = workbook["EditedData"]
        untouched = workbook["Untouched"]
        require(edited["A1"].value == "materialized-edit", "generated rename/materialized: A1 mismatch")
        require(edited["B2"].value == 42, "generated rename/materialized: B2 mismatch")
        require(untouched["A1"].value == "keep-me", "generated rename/materialized: untouched A1 mismatch")
        openpyxl_report = {
            "sheetnames": workbook.sheetnames,
            "EditedData!A1": edited["A1"].value,
            "EditedData!B2": edited["B2"].value,
            "Untouched!A1": untouched["A1"].value,
        }
    finally:
        workbook.close()

    return zip_report, openpyxl_report


def verify_generated_style_passthrough(path: Path) -> tuple[dict[str, Any], dict[str, Any]]:
    zip_report: dict[str, Any] = {}
    names = zip_names(path)
    require("xl/styles.xml" in names, "generated style passthrough: missing styles.xml")
    worksheet_xml = read_zip_text(path, "xl/worksheets/sheet1.xml")
    require('<c r="A1" s="1"><v>9.5</v></c>' in worksheet_xml,
            "generated style passthrough: missing styled numeric cell")
    require('<c r="B1" t="inlineStr"><is><t>explicit default</t></is></c>' in worksheet_xml,
            "generated style passthrough: missing explicit default text cell")
    require('s="0"' not in worksheet_xml, "generated style passthrough: explicit default serialized as s=0")
    zip_report["style_cell"] = "A1"

    openpyxl = load_openpyxl()
    workbook = openpyxl.load_workbook(path, read_only=False, data_only=False)
    try:
        worksheet = workbook["Data"]
        require(worksheet["A1"].value == 9.5, "generated style passthrough: A1 mismatch")
        require(worksheet["B1"].value == "explicit default", "generated style passthrough: B1 mismatch")
        require(worksheet["A1"].number_format == "0.00",
                f"generated style passthrough: unexpected A1 number format {worksheet['A1'].number_format!r}")
        openpyxl_report = {
            "sheetnames": workbook.sheetnames,
            "Data!A1": worksheet["A1"].value,
            "Data!A1.number_format": worksheet["A1"].number_format,
            "Data!B1": worksheet["B1"].value,
        }
    finally:
        workbook.close()

    return zip_report, openpyxl_report


def verify_generated_image_replace(path: Path, replacement_image: Path) -> tuple[dict[str, Any], dict[str, Any]]:
    zip_report: dict[str, Any] = {}
    names = zip_names(path)
    require("xl/media/image1.png" in names, "generated image replace: missing image1.png")
    require("xl/drawings/drawing1.xml" in names, "generated image replace: missing drawing1.xml")
    output_bytes = read_zip_bytes(path, "xl/media/image1.png")
    expected_bytes = replacement_image.read_bytes()
    require(output_bytes == expected_bytes, "generated image replace: media bytes mismatch")
    zip_report["image_part"] = "xl/media/image1.png"

    openpyxl = load_openpyxl()
    workbook = openpyxl.load_workbook(path, read_only=False, data_only=False)
    try:
        require(workbook.sheetnames == ["Data", "Pictures"],
                f"generated image replace: unexpected sheetnames {workbook.sheetnames!r}")
        pictures = workbook["Pictures"]
        openpyxl_report = {
            "sheetnames": workbook.sheetnames,
            "Pictures!A1": pictures["A1"].value,
            "Pictures.image_count": openpyxl_image_count(pictures),
        }
        require(pictures["A1"].value == "image-sheet", "generated image replace: Pictures!A1 mismatch")
    finally:
        workbook.close()

    return zip_report, openpyxl_report


def verify_generated_public_e2e(path: Path, replacement_image: Path) -> tuple[dict[str, Any], dict[str, Any]]:
    zip_report: dict[str, Any] = {}
    workbook_xml = read_zip_text(path, "xl/workbook.xml")
    sheet1_xml = read_zip_text(path, "xl/worksheets/sheet1.xml")
    sheet2_xml = read_zip_text(path, "xl/worksheets/sheet2.xml")
    require('name="EditedData"' in workbook_xml, "generated public e2e: missing renamed EditedData")
    require('name="Data"' not in workbook_xml, "generated public e2e: old Data name still present")
    require("materialized-edit" in sheet1_xml, "generated public e2e: missing materialized edit")
    require('<c r="B2"><v>42</v></c>' in sheet1_xml, "generated public e2e: missing B2 edit")
    require("sheetdata-final" in sheet2_xml, "generated public e2e: missing replaced sheet text")
    require('<c r="B1"><v>7</v></c>' in sheet2_xml, "generated public e2e: missing replaced sheet number")
    require(read_zip_bytes(path, "xl/media/image1.png") == replacement_image.read_bytes(),
            "generated public e2e: image bytes mismatch")
    zip_report["sheets"] = ["EditedData", "ReplaceMe", "Pictures"]

    openpyxl = load_openpyxl()
    workbook = openpyxl.load_workbook(path, read_only=False, data_only=False)
    try:
        require(workbook.sheetnames == ["EditedData", "ReplaceMe", "Pictures"],
                f"generated public e2e: unexpected sheetnames {workbook.sheetnames!r}")
        edited = workbook["EditedData"]
        replaced = workbook["ReplaceMe"]
        pictures = workbook["Pictures"]
        require(edited["A1"].value == "materialized-edit", "generated public e2e: A1 mismatch")
        require(edited["B2"].value == 42, "generated public e2e: B2 mismatch")
        require(replaced["A1"].value == "sheetdata-final", "generated public e2e: ReplaceMe!A1 mismatch")
        require(replaced["B1"].value == 7, "generated public e2e: ReplaceMe!B1 mismatch")
        openpyxl_report = {
            "sheetnames": workbook.sheetnames,
            "EditedData!A1": edited["A1"].value,
            "EditedData!B2": edited["B2"].value,
            "ReplaceMe!A1": replaced["A1"].value,
            "ReplaceMe!B1": replaced["B1"].value,
            "Pictures.image_count": openpyxl_image_count(pictures),
        }
    finally:
        workbook.close()

    return zip_report, openpyxl_report


def verify_generated_non_default_style_rejection(
    source_path: Path,
    output_path: Path,
    tool_report: dict[str, Any],
) -> tuple[dict[str, Any], dict[str, Any]]:
    zip_report: dict[str, Any] = {}
    require(tool_report.get("status") == "expected_rejection_observed",
            f"generated style rejection: unexpected tool status {tool_report.get('status')!r}")
    error_message = tool_report.get("error_message", "")
    require("does not support non-default StyleId" in error_message,
            f"generated style rejection: unexpected error message {error_message!r}")

    source_names = zip_names(source_path)
    output_names = zip_names(output_path)
    require(source_names == output_names, "generated style rejection: ZIP entries changed after rejection")
    for name in source_names:
        require(read_zip_bytes(source_path, name) == read_zip_bytes(output_path, name),
                f"generated style rejection: entry changed after rejection: {name}")
    zip_report["entries_compared"] = len(source_names)

    openpyxl = load_openpyxl()
    workbook = openpyxl.load_workbook(output_path, read_only=False, data_only=False)
    try:
        worksheet = workbook["Data"]
        require(worksheet["A1"].value == "placeholder-a1", "generated style rejection: A1 mismatch")
        require(worksheet["B1"].value == 1, "generated style rejection: B1 mismatch")
        openpyxl_report = {
            "sheetnames": workbook.sheetnames,
            "Data!A1": worksheet["A1"].value,
            "Data!B1": worksheet["B1"].value,
        }
    finally:
        workbook.close()

    return zip_report, openpyxl_report


def workbook_sheetnames(path: Path) -> list[str]:
    openpyxl = load_openpyxl()
    workbook = openpyxl.load_workbook(path, read_only=False, data_only=False)
    try:
        return list(workbook.sheetnames)
    finally:
        workbook.close()


def verify_fixture_rename_materialized(
    source_path: Path,
    output_path: Path,
    original_sheet_name: str,
    renamed_sheet_name: str,
) -> tuple[dict[str, Any], dict[str, Any]]:
    zip_report: dict[str, Any] = {}
    source_names = zip_names(source_path)
    output_names = zip_names(output_path)
    require("xl/workbook.xml" in output_names, "fixture rename/materialized: missing workbook.xml")
    if "xl/sharedStrings.xml" in source_names:
        require("xl/sharedStrings.xml" in output_names,
                "fixture rename/materialized: sharedStrings part should be preserved when source has one")
        require(read_zip_bytes(source_path, "xl/sharedStrings.xml")
                == read_zip_bytes(output_path, "xl/sharedStrings.xml"),
                "fixture rename/materialized: sharedStrings bytes changed unexpectedly")
        zip_report["shared_strings"] = "preserved"
    else:
        require("xl/sharedStrings.xml" not in output_names,
                "fixture rename/materialized: output should not invent sharedStrings.xml")
        zip_report["shared_strings"] = "absent"

    if "xl/styles.xml" in source_names:
        require("xl/styles.xml" in output_names, "fixture rename/materialized: styles.xml should remain present")
        zip_report["styles"] = "present"
    else:
        zip_report["styles"] = "absent"

    openpyxl = load_openpyxl()
    workbook = openpyxl.load_workbook(output_path, read_only=False, data_only=False)
    try:
        require(renamed_sheet_name in workbook.sheetnames,
                f"fixture rename/materialized: renamed sheet missing from {workbook.sheetnames!r}")
        require(original_sheet_name not in workbook.sheetnames,
                f"fixture rename/materialized: old sheet still present in {workbook.sheetnames!r}")
        worksheet = workbook[renamed_sheet_name]
        require(worksheet["A1"].value == "fixture-materialized-edit",
                "fixture rename/materialized: A1 mismatch")
        require(worksheet["B2"].value == 42,
                f"fixture rename/materialized: B2 mismatch {worksheet['B2'].value!r}")
        openpyxl_report = {
            "sheetnames": workbook.sheetnames,
            f"{renamed_sheet_name}!A1": worksheet["A1"].value,
            f"{renamed_sheet_name}!B2": worksheet["B2"].value,
        }
        zip_report["sheetnames"] = workbook.sheetnames
    finally:
        workbook.close()

    return zip_report, openpyxl_report


def verify_fixture_materialized_only(
    source_path: Path,
    output_path: Path,
    sheet_name: str,
) -> tuple[dict[str, Any], dict[str, Any]]:
    zip_report: dict[str, Any] = {}
    source_names = zip_names(source_path)
    output_names = zip_names(output_path)
    require("xl/workbook.xml" in output_names or "workbook.xml" in output_names,
            "fixture materialized-only: missing workbook part")

    if "xl/sharedStrings.xml" in source_names:
        require("xl/sharedStrings.xml" in output_names,
                "fixture materialized-only: sharedStrings part should be preserved when source has one")
        require(read_zip_bytes(source_path, "xl/sharedStrings.xml")
                == read_zip_bytes(output_path, "xl/sharedStrings.xml"),
                "fixture materialized-only: sharedStrings bytes changed unexpectedly")
        zip_report["shared_strings"] = "preserved"
    else:
        require("xl/sharedStrings.xml" not in output_names,
                "fixture materialized-only: output should not invent sharedStrings.xml")
        zip_report["shared_strings"] = "absent"

    if "xl/styles.xml" in source_names:
        require("xl/styles.xml" in output_names,
                "fixture materialized-only: styles.xml should remain present when source has one")
        zip_report["styles"] = "present"
    else:
        zip_report["styles"] = "absent"

    openpyxl = load_openpyxl()
    workbook = openpyxl.load_workbook(output_path, read_only=False, data_only=False)
    try:
        require(sheet_name in workbook.sheetnames,
                f"fixture materialized-only: sheet {sheet_name!r} missing from {workbook.sheetnames!r}")
        worksheet = workbook[sheet_name]
        require(worksheet["A1"].value == "fixture-materialized-edit",
                f"fixture materialized-only: A1 mismatch {worksheet['A1'].value!r}")
        require(worksheet["B2"].value == 42,
                f"fixture materialized-only: B2 mismatch {worksheet['B2'].value!r}")
        openpyxl_report = {
            "sheetnames": workbook.sheetnames,
            f"{sheet_name}!A1": worksheet["A1"].value,
            f"{sheet_name}!B2": worksheet["B2"].value,
        }
    finally:
        workbook.close()

    return zip_report, openpyxl_report


def create_xlsxwriter_reference(
    scenario: str,
    reference_path: Path,
    replacement_image: Path | None,
) -> dict[str, Any]:
    xlsxwriter = load_xlsxwriter()
    if xlsxwriter is None:
        return {"status": "skipped", "reason": "xlsxwriter not installed"}

    reference_path.parent.mkdir(parents=True, exist_ok=True)
    workbook = xlsxwriter.Workbook(reference_path)
    try:
        if scenario == "generated_rename_materialized":
            edited = workbook.add_worksheet("EditedData")
            untouched = workbook.add_worksheet("Untouched")
            edited.write("A1", "materialized-edit")
            edited.write_number("B2", 42)
            untouched.write("A1", "keep-me")
        elif scenario == "generated_style_passthrough":
            data = workbook.add_worksheet("Data")
            style = workbook.add_format({"num_format": "0.00"})
            data.write_number("A1", 9.5, style)
            data.write("B1", "explicit default")
        elif scenario == "generated_image_replace":
            require(replacement_image is not None, "xlsxwriter reference: replacement image is required")
            data = workbook.add_worksheet("Data")
            pictures = workbook.add_worksheet("Pictures")
            data.write("A1", "placeholder-a1")
            pictures.write("A1", "image-sheet")
            pictures.insert_image("A2", str(replacement_image))
        elif scenario == "generated_public_e2e":
            require(replacement_image is not None, "xlsxwriter reference: replacement image is required")
            edited = workbook.add_worksheet("EditedData")
            replace_me = workbook.add_worksheet("ReplaceMe")
            pictures = workbook.add_worksheet("Pictures")
            edited.write("A1", "materialized-edit")
            edited.write_number("B2", 42)
            replace_me.write("A1", "sheetdata-final")
            replace_me.write_number("B1", 7)
            pictures.insert_image("A1", str(replacement_image))
        else:
            return {"status": "skipped", "reason": f"no xlsxwriter reference for {scenario}"}
    finally:
        workbook.close()

    if not reference_path.exists():
        return {"status": "skipped", "reason": f"no xlsxwriter reference for {scenario}"}

    openpyxl = load_openpyxl()
    reference = openpyxl.load_workbook(reference_path, read_only=False, data_only=False)
    try:
        info = {"status": "created", "sheetnames": reference.sheetnames}
        if scenario == "generated_style_passthrough":
            info["Data!A1.number_format"] = reference["Data"]["A1"].number_format
        return info
    finally:
        reference.close()


def run_generated_case(
    qa_exe: Path,
    work_dir: Path,
    scenario: str,
) -> ScenarioResult:
    case_dir = work_dir / scenario
    tool_report = run_tool(qa_exe, scenario, case_dir)
    output_path = Path(tool_report["output"])
    replacement_image = Path(tool_report["replacement_image"]) if tool_report.get("replacement_image") else None

    if scenario == "generated_rename_materialized":
        zip_xml, openpyxl_report = verify_generated_rename_materialized(output_path)
    elif scenario == "generated_style_passthrough":
        zip_xml, openpyxl_report = verify_generated_style_passthrough(output_path)
    elif scenario == "generated_image_replace":
        require(replacement_image is not None, "generated image replace: missing replacement image in tool report")
        zip_xml, openpyxl_report = verify_generated_image_replace(output_path, replacement_image)
    elif scenario == "generated_public_e2e":
        require(replacement_image is not None, "generated public e2e: missing replacement image in tool report")
        zip_xml, openpyxl_report = verify_generated_public_e2e(output_path, replacement_image)
    elif scenario == "generated_non_default_style_rejection":
        source_path = Path(tool_report["source"])
        zip_xml, openpyxl_report = verify_generated_non_default_style_rejection(
            source_path, output_path, tool_report
        )
    else:  # pragma: no cover - guarded by argparse
        raise RuntimeError(f"unsupported generated scenario: {scenario}")

    reference_path = case_dir / "xlsxwriter-reference.xlsx"
    xlsxwriter_reference = create_xlsxwriter_reference(scenario, reference_path, replacement_image)
    return ScenarioResult(
        name=scenario,
        report=tool_report,
        zip_xml=zip_xml,
        openpyxl=openpyxl_report,
        xlsxwriter_reference=xlsxwriter_reference,
    )


def run_fixture_case(
    qa_exe: Path,
    work_dir: Path,
    fixture_path: Path,
    group_name: str,
) -> ScenarioResult:
    case_dir = work_dir / group_name / fixture_path.stem
    openpyxl = load_openpyxl()
    workbook = openpyxl.load_workbook(fixture_path, read_only=False, data_only=False)
    try:
        source_sheet_name = workbook.sheetnames[0]
        rename_to = choose_unique_sheet_name(list(workbook.sheetnames))
    finally:
        workbook.close()

    source_names = zip_names(fixture_path)
    tool_scenario = (
        "fixture_rename_materialized"
        if "xl/workbook.xml" in source_names
        else "fixture_materialized_only"
    )

    tool_kwargs: dict[str, Any] = {
        "source": fixture_path,
        "rename_to": rename_to,
    }
    if tool_scenario == "fixture_materialized_only":
        tool_kwargs["sheet_name"] = source_sheet_name

    tool_report = run_tool(
        qa_exe,
        tool_scenario,
        case_dir,
        **tool_kwargs,
    )
    output_path = Path(tool_report["output"])
    actual_source_sheet_name = tool_report.get("source_sheet_name") or source_sheet_name
    actual_renamed_sheet_name = tool_report.get("renamed_sheet_name") or rename_to
    if tool_scenario == "fixture_rename_materialized":
        zip_xml, openpyxl_report = verify_fixture_rename_materialized(
            fixture_path,
            output_path,
            actual_source_sheet_name,
            actual_renamed_sheet_name,
        )
    else:
        zip_xml, openpyxl_report = verify_fixture_materialized_only(
            fixture_path,
            output_path,
            actual_source_sheet_name,
        )
    return ScenarioResult(
        name=f"{group_name}:{fixture_path.name}",
        report=tool_report,
        zip_xml=zip_xml,
        openpyxl=openpyxl_report,
        xlsxwriter_reference={"status": "skipped", "reason": "fixture scenario"},
    )


def run_self_test() -> int:
    openpyxl = load_openpyxl()
    temp_dir = Path(tempfile.mkdtemp(prefix="fastxlsx-workbook-editor-qa-selftest-"))
    try:
        workbook_path = temp_dir / "selftest.xlsx"
        workbook = openpyxl.Workbook()
        try:
            worksheet = workbook.active
            worksheet.title = "SelfTest"
            worksheet["A1"] = "ok"
            workbook.save(workbook_path)
        finally:
            workbook.close()

        names = zip_names(workbook_path)
        require("xl/workbook.xml" in names, "self-test: workbook.xml missing")
        require("SelfTest" in read_zip_text(workbook_path, "xl/workbook.xml"),
                "self-test: worksheet title missing from workbook.xml")

        reopened = openpyxl.load_workbook(workbook_path, read_only=False, data_only=False)
        try:
            require(reopened["SelfTest"]["A1"].value == "ok", "self-test: openpyxl readback mismatch")
        finally:
            reopened.close()

        xlsxwriter = load_xlsxwriter()
        xlsxwriter_status = "not_installed"
        if xlsxwriter is not None:
            reference_path = temp_dir / "xlsxwriter-selftest.xlsx"
            reference = xlsxwriter.Workbook(reference_path)
            try:
                sheet = reference.add_worksheet("Ref")
                sheet.write("A1", "ok")
            finally:
                reference.close()
            require(reference_path.exists(), "self-test: xlsxwriter did not produce workbook")
            xlsxwriter_status = "created"

        print(
            json.dumps(
                {
                    "status": "ok",
                    "workbook": str(workbook_path),
                    "xlsxwriter": xlsxwriter_status,
                },
                indent=2,
            )
        )
        return 0
    finally:
        shutil.rmtree(temp_dir, ignore_errors=True)


def run_excel_verification(report_path: Path, work_dir: Path, excel_script: Path) -> dict[str, Any]:
    office_report_path = work_dir / "office-report.json"
    completed = subprocess.run(
        [
            "powershell",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(excel_script),
            "-ReportPath",
            str(report_path),
            "-OfficeReportPath",
            str(office_report_path),
        ],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )

    if completed.returncode != 0:
        return {
            "status": "failed",
            "report": str(office_report_path),
            "stdout": completed.stdout,
            "stderr": completed.stderr,
        }

    if office_report_path.exists():
        with office_report_path.open("r", encoding="utf-8-sig") as stream:
            office_report = json.load(stream)
        office_report["stdout"] = completed.stdout
        return office_report

    return {
        "status": "failed",
        "report": str(office_report_path),
        "stdout": completed.stdout,
        "stderr": "Excel verification completed but did not write an office report.",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--qa-exe",
        type=Path,
        default=resolve_default_qa_exe(),
        help="Path to fastxlsx_workbook_editor_qa_tool.exe.",
    )
    parser.add_argument(
        "--fixture-root",
        type=Path,
        help="Root directory containing external .xlsx fixtures, for example xlnt tests/data.",
    )
    parser.add_argument(
        "--work-dir",
        type=Path,
        default=Path("build/qa/workbook-editor"),
        help="Directory for QA artifacts and reports.",
    )
    parser.add_argument(
        "--report",
        type=Path,
        help="Optional aggregate JSON report path. Defaults to <work-dir>/report.json.",
    )
    parser.add_argument(
        "--scenario",
        action="append",
        choices=GENERATED_SCENARIOS + ["xlnt_fixture_rename_smoke", "xlnt_fixture_string_smoke"],
        help="Scenario group to run. Can be passed multiple times.",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="Run a lightweight helper self-test and exit.",
    )
    parser.add_argument(
        "--excel-verify",
        action="store_true",
        help="Open successful output workbooks with local Excel COM and write an office report.",
    )
    parser.add_argument(
        "--excel-script",
        type=Path,
        default=resolve_default_excel_script(),
        help="Path to verify_workbook_editor_qa_excel.ps1.",
    )
    args = parser.parse_args()

    if args.self_test:
        return run_self_test()

    require(args.qa_exe is not None, "qa tool path is required; build with FASTXLSX_BUILD_QA_TOOLS=ON")
    qa_exe = args.qa_exe.resolve()
    require(qa_exe.exists(), f"qa tool not found: {qa_exe}")

    work_dir: Path = args.work_dir
    work_dir.mkdir(parents=True, exist_ok=True)

    selected = list(args.scenario) if args.scenario else list(GENERATED_SCENARIOS)
    if args.fixture_root is not None and not args.scenario:
        selected.extend(["xlnt_fixture_rename_smoke", "xlnt_fixture_string_smoke"])

    results: list[ScenarioResult] = []
    for scenario in selected:
        if scenario in GENERATED_SCENARIOS:
            try:
                results.append(run_generated_case(qa_exe, work_dir, scenario))
            except Exception as exc:
                results.append(
                    ScenarioResult(
                        name=scenario,
                        report={"scenario": scenario, "status": "failed"},
                        zip_xml={},
                        openpyxl={},
                        xlsxwriter_reference={"status": "skipped", "reason": "case failed"},
                        error=str(exc),
                    )
                )
            continue

        require(args.fixture_root is not None,
                f"{scenario}: --fixture-root is required for fixture scenarios")
        fixture_root: Path = args.fixture_root
        require(fixture_root.exists(), f"fixture root does not exist: {fixture_root}")
        fixture_names = (
            DEFAULT_XLNT_RENAME_FIXTURES
            if scenario == "xlnt_fixture_rename_smoke"
            else DEFAULT_XLNT_STRING_FIXTURES
        )
        for fixture_name in fixture_names:
            fixture_path = fixture_root / fixture_name
            require(fixture_path.exists(), f"fixture not found: {fixture_path}")
            try:
                results.append(run_fixture_case(qa_exe, work_dir, fixture_path, scenario))
            except Exception as exc:
                results.append(
                    ScenarioResult(
                        name=f"{scenario}:{fixture_path.name}",
                        report={"scenario": scenario, "status": "failed", "fixture": str(fixture_path)},
                        zip_xml={},
                        openpyxl={},
                        xlsxwriter_reference={"status": "skipped", "reason": "case failed"},
                        error=str(exc),
                    )
                )

    failed_cases = [result.name for result in results if result.error]
    aggregate = {
        "qa_exe": str(qa_exe),
        "work_dir": str(work_dir.resolve()),
        "status": "ok" if not failed_cases else "partial_failure",
        "failed_cases": failed_cases,
        "cases": [
            {
                "name": result.name,
                "tool_report": result.report,
                "zip_xml": result.zip_xml,
                "openpyxl": result.openpyxl,
                "xlsxwriter_reference": result.xlsxwriter_reference,
                "error": result.error,
            }
            for result in results
        ],
    }
    report_path = args.report if args.report is not None else work_dir / "report.json"
    with report_path.open("w", encoding="utf-8") as stream:
        json.dump(aggregate, stream, indent=2, ensure_ascii=False)
        stream.write("\n")

    excel_failed = False
    if args.excel_verify:
        excel_script: Path = args.excel_script.resolve()
        require(excel_script.exists(), f"Excel verification script not found: {excel_script}")
        excel_report = run_excel_verification(report_path, work_dir, excel_script)
        aggregate["excel"] = excel_report
        excel_failed = excel_report.get("status") != "ok"
        with report_path.open("w", encoding="utf-8") as stream:
            json.dump(aggregate, stream, indent=2, ensure_ascii=False)
            stream.write("\n")

    final_status = "ok" if not failed_cases and not excel_failed else "partial_failure"
    print(
        json.dumps(
            {
                "status": final_status,
                "report": str(report_path),
                "case_count": len(results),
                "failed_cases": failed_cases,
                "excel": aggregate.get("excel", {"status": "not_run"}),
            },
            indent=2,
        )
    )
    return 0 if not failed_cases and not excel_failed else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"run_workbook_editor_qa.py failed: {exc}", file=sys.stderr)
        raise
