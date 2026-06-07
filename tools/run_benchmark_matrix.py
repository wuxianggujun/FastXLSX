#!/usr/bin/env python3
"""Run an opt-in FastXLSX streaming writer benchmark matrix.

This helper is intentionally outside CTest and CI. It wraps the existing
fastxlsx_bench_streaming_writer executable, collects its schema-v3 JSON results,
and optionally verifies generated workbooks with openpyxl as local QA only.
"""

from __future__ import annotations

import argparse
import json
import math
import platform
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


DEFAULT_CASES = [
    "numeric:inline:mixed",
    "mixed:inline:mixed",
    "mixed:shared:mixed",
    "strings:inline:repeated",
    "strings:shared:repeated",
    "strings:inline:unique",
    "strings:shared:unique",
]


def default_benchmark_exe() -> Path:
    suffix = ".exe" if platform.system() == "Windows" else ""
    return Path("build/windows-nmake-release-benchmark/benchmarks") / f"fastxlsx_bench_streaming_writer{suffix}"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


@dataclass(frozen=True)
class MatrixCase:
    scenario: str
    string_strategy: str
    string_pattern: str

    @property
    def name(self) -> str:
        if self.scenario == "numeric":
            return "numeric-inline"
        return f"{self.scenario}-{self.string_pattern}-{self.string_strategy}"


def parse_case(text: str) -> MatrixCase:
    parts = text.split(":")
    if len(parts) != 3:
        raise ValueError(f"case must use scenario:string_strategy:string_pattern format: {text}")
    scenario, string_strategy, string_pattern = parts
    if scenario not in {"numeric", "mixed", "strings"}:
        raise ValueError(f"unsupported scenario in case {text!r}")
    if string_strategy not in {"inline", "shared"}:
        raise ValueError(f"unsupported string strategy in case {text!r}")
    if string_pattern not in {"mixed", "repeated", "unique"}:
        raise ValueError(f"unsupported string pattern in case {text!r}")
    if scenario == "numeric" and string_strategy != "inline":
        raise ValueError("numeric cases should use inline strategy to avoid duplicate no-string runs")
    return MatrixCase(scenario=scenario, string_strategy=string_strategy, string_pattern=string_pattern)


def expected_string_ratio(case: MatrixCase, mixed_string_ratio: float) -> float:
    if case.scenario == "numeric":
        return 0.0
    if case.scenario == "strings":
        return 1.0
    return mixed_string_ratio


def should_write_string(case: MatrixCase, mixed_string_ratio: float, row: int, col: int) -> bool:
    ratio = expected_string_ratio(case, mixed_string_ratio)
    if case.scenario == "numeric" or ratio <= 0.0:
        return False
    if case.scenario == "strings" or ratio >= 1.0:
        return True
    raw_bucket = 1.0 / ratio
    if not math.isfinite(raw_bucket) or raw_bucket >= 2**32 - 1:
        return False
    bucket = max(int(raw_bucket), 1)
    return ((row + col) % bucket) == 0


def make_string_value(case: MatrixCase, sheet: int, row: int, col: int) -> str:
    if case.string_pattern == "repeated":
        return "repeat"
    if case.string_pattern == "unique":
        return f"s{sheet}-r{row}-c{col}"
    if (row + col) % 5 == 0:
        return "repeat"
    return f"s{sheet}-r{row}-c{col}"


def make_number_value(sheet: int, row: int, col: int) -> float:
    return float(sheet) * 1_000_000.0 + float(row) * 1_000.0 + float(col)


def expected_cell_value(case: MatrixCase, mixed_string_ratio: float, row: int, col: int) -> int | str:
    if should_write_string(case, mixed_string_ratio, row, col):
        return make_string_value(case, 1, row, col)
    value = make_number_value(1, row, col)
    return int(value) if value.is_integer() else value


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def verify_result_json(path: Path, case: MatrixCase, rows: int, cols: int, sheets: int,
    mixed_string_ratio: float, output_path: Path) -> dict[str, Any]:
    data = load_json(path)
    require(data.get("benchmark_schema_version") == "3", "benchmark schema version mismatch")
    require(data.get("scenario") == case.scenario, f"{case.name} scenario mismatch")
    require(data.get("rows") == rows, f"{case.name} rows mismatch")
    require(data.get("cols") == cols, f"{case.name} cols mismatch")
    require(data.get("sheets") == sheets, f"{case.name} sheets mismatch")
    require(data.get("cells") == rows * cols * sheets, f"{case.name} cell count mismatch")
    require(data.get("string_pattern") == case.string_pattern, f"{case.name} string pattern mismatch")
    require(data.get("string_strategy") == case.string_strategy, f"{case.name} string strategy mismatch")
    require(
        abs(float(data.get("string_ratio")) - expected_string_ratio(case, mixed_string_ratio)) < 1e-9,
        f"{case.name} string ratio mismatch",
    )
    require(data.get("package_entry_source_mode") == "worksheet-file-backed-chunked",
        f"{case.name} package entry source mode mismatch")
    require(data.get("temporary_worksheet_part_footprint") == "worksheet-body-file-bytes",
        f"{case.name} temporary footprint label mismatch")
    require(int(data.get("temporary_worksheet_part_footprint_bytes")) > 0,
        f"{case.name} temporary footprint bytes should be positive")
    require(int(data.get("elapsed_ms")) >= 0, f"{case.name} elapsed_ms mismatch")
    require(int(data.get("output_bytes")) == output_path.stat().st_size,
        f"{case.name} output size mismatch")
    require(data.get("office_open") == "not_run", f"{case.name} office_open should remain not_run")
    return data


def verify_workbook_with_openpyxl(path: Path, case: MatrixCase, rows: int, cols: int, sheets: int,
    mixed_string_ratio: float) -> dict[str, Any]:
    try:
        import openpyxl  # type: ignore
    except ModuleNotFoundError:
        return {"status": "skipped", "reason": "Python module openpyxl is not installed"}

    workbook = openpyxl.load_workbook(path, read_only=True, data_only=False)
    try:
        require(len(workbook.sheetnames) == sheets, f"{case.name} sheet count mismatch")
        require("Sheet1" in workbook.sheetnames, f"{case.name} missing Sheet1")
        worksheet = workbook["Sheet1"]
        require(worksheet.max_row == rows, f"{case.name} row count mismatch: {worksheet.max_row}")
        require(worksheet.max_column == cols, f"{case.name} column count mismatch: {worksheet.max_column}")
        first_expected = expected_cell_value(case, mixed_string_ratio, 1, 1)
        last_expected = expected_cell_value(case, mixed_string_ratio, rows, cols)
        first_value = worksheet.cell(row=1, column=1).value
        last_value = worksheet.cell(row=rows, column=cols).value
        require(first_value == first_expected,
            f"{case.name} A1 mismatch: expected {first_expected!r}, got {first_value!r}")
        require(last_value == last_expected,
            f"{case.name} last cell mismatch: expected {last_expected!r}, got {last_value!r}")
        return {
            "status": "opened",
            "sheet_count": len(workbook.sheetnames),
            "sheet1_max_row": worksheet.max_row,
            "sheet1_max_column": worksheet.max_column,
            "sheet1_first_cell": first_value,
            "sheet1_last_cell": last_value,
        }
    finally:
        workbook.close()


def run_case(bench_exe: Path, output_dir: Path, case: MatrixCase, rows: int, cols: int, sheets: int,
    mixed_string_ratio: float, verify_openpyxl: bool) -> dict[str, Any]:
    output_path = output_dir / f"fastxlsx-bench-{case.name}.xlsx"
    result_path = output_dir / f"fastxlsx-bench-{case.name}.json"
    command = [
        str(bench_exe),
        "--scenario",
        case.scenario,
        "--rows",
        str(rows),
        "--cols",
        str(cols),
        "--sheets",
        str(sheets),
        "--string-pattern",
        case.string_pattern,
        "--string-strategy",
        case.string_strategy,
        "--string-ratio",
        str(mixed_string_ratio),
        "--output",
        str(output_path),
        "--result",
        str(result_path),
    ]
    completed = subprocess.run(command, check=False, text=True, capture_output=True)
    require(completed.returncode == 0,
        f"{case.name} benchmark failed with {completed.returncode}: {completed.stderr.strip()}")
    require(output_path.exists(), f"{case.name} output workbook was not created")
    require(result_path.exists(), f"{case.name} result JSON was not created")

    result_json = verify_result_json(
        result_path, case, rows, cols, sheets, mixed_string_ratio, output_path)
    openpyxl_report = verify_workbook_with_openpyxl(
        output_path, case, rows, cols, sheets, mixed_string_ratio) if verify_openpyxl else {
            "status": "not_requested"
        }
    return {
        "name": case.name,
        "scenario": case.scenario,
        "string_strategy": case.string_strategy,
        "string_pattern": case.string_pattern,
        "command": command,
        "stdout": completed.stdout.strip(),
        "stderr": completed.stderr.strip(),
        "output": str(output_path),
        "result_json": str(result_path),
        "expected": {
            "sheet1_first_cell": expected_cell_value(case, mixed_string_ratio, 1, 1),
            "sheet1_last_cell": expected_cell_value(case, mixed_string_ratio, rows, cols),
        },
        "result": result_json,
        "openpyxl": openpyxl_report,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bench-exe", type=Path, default=default_benchmark_exe(),
        help="Path to fastxlsx_bench_streaming_writer.")
    parser.add_argument("--output-dir", type=Path, default=Path("build/qa/benchmark-matrix"),
        help="Directory for generated workbooks, per-case JSON, and the matrix report.")
    parser.add_argument("--rows", type=int, default=1000, help="Rows per worksheet.")
    parser.add_argument("--cols", type=int, default=10, help="Columns per worksheet.")
    parser.add_argument("--sheets", type=int, default=1, help="Worksheet count.")
    parser.add_argument("--mixed-string-ratio", type=float, default=0.25,
        help="String ratio passed to mixed scenario cases.")
    parser.add_argument("--case", action="append", dest="cases",
        help="Case in scenario:string_strategy:string_pattern format. May be passed multiple times.")
    parser.add_argument("--verify-openpyxl", action="store_true",
        help="Open generated workbooks with openpyxl and verify Sheet1 first/last cells.")
    args = parser.parse_args()

    bench_exe = args.bench_exe.resolve()
    output_dir = args.output_dir.resolve()
    require(bench_exe.exists(), f"benchmark executable does not exist: {bench_exe}")
    require(args.rows > 0 and args.cols > 0 and args.sheets > 0, "rows, cols, and sheets must be positive")
    require(0.0 <= args.mixed_string_ratio <= 1.0, "mixed string ratio must be between 0 and 1")
    output_dir.mkdir(parents=True, exist_ok=True)

    raw_cases = args.cases if args.cases else DEFAULT_CASES
    cases = [parse_case(raw_case) for raw_case in raw_cases]
    names = [case.name for case in cases]
    require(len(names) == len(set(names)), "benchmark case names must be unique")

    reports = [
        run_case(bench_exe, output_dir, case, args.rows, args.cols, args.sheets,
            args.mixed_string_ratio, args.verify_openpyxl)
        for case in cases
    ]
    report = {
        "benchmark_matrix_schema_version": "1",
        "benchmark_executable": str(bench_exe),
        "output_dir": str(output_dir),
        "rows": args.rows,
        "cols": args.cols,
        "sheets": args.sheets,
        "cells_per_case": args.rows * args.cols * args.sheets,
        "mixed_string_ratio": args.mixed_string_ratio,
        "cases": reports,
        "comparison_scope": (
            "Manual opt-in benchmark matrix. Results compare generated schema-v3 JSON "
            "fields and optional openpyxl workbook readability; Office validation is a "
            "separate local step and benchmark office_open fields remain not_run."
        ),
    }
    report_path = output_dir / "benchmark-matrix-report.json"
    report_path.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001 - CLI should emit one concise failure line.
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
