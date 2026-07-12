#!/usr/bin/env python3
"""Summarize opt-in FastXLSX benchmark JSON results.

The script is intentionally read-only for benchmark case JSON files. It accepts
individual schema-v4/v5 benchmark results, benchmark-matrix-report.json files, or a
directory containing those files, then emits a compact Markdown and/or JSON
summary for local QA notes.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
import tempfile
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable


BENCHMARK_SCHEMA_VERSION = "5"
SUPPORTED_BENCHMARK_SCHEMA_VERSIONS = {"4", "5"}
MATRIX_REPORT_SCHEMA_VERSION = "1"
SUMMARY_SCHEMA_VERSION = "1"
BYTES_PER_MIB = 1024.0 * 1024.0


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def number(value: Any, field_name: str) -> int | float:
    require(isinstance(value, (int, float)) and not isinstance(value, bool),
        f"{field_name} must be numeric")
    require(math.isfinite(value), f"{field_name} must be finite")
    return value


def text(value: Any, field_name: str) -> str:
    require(isinstance(value, str), f"{field_name} must be a string")
    return value


def optional_text(value: Any) -> str | None:
    return value if isinstance(value, str) else None


def optional_int(value: Any) -> int | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value
    return None


@dataclass(frozen=True)
class BenchmarkCase:
    name: str
    source: str
    benchmark_schema_version: str
    scenario: str
    rows: int
    cols: int
    sheets: int
    cells: int
    string_ratio: float
    string_pattern: str
    string_cells: int
    unique_string_values: int
    duplicate_string_cells: int
    string_dedup_ratio: float
    string_strategy: str
    zip_backend: str
    compression: str
    compression_level: int | None
    package_entry_source_mode: str
    temporary_worksheet_part_footprint: str
    temporary_worksheet_part_footprint_bytes: int
    elapsed_ms: int
    peak_memory_mb: float
    output_bytes: int
    office_open: str
    output: str | None = None
    openpyxl_status: str | None = None

    @property
    def cells_per_second(self) -> float:
        if self.elapsed_ms <= 0:
            return 0.0
        return self.cells / (self.elapsed_ms / 1000.0)

    @property
    def million_cells_per_second(self) -> float:
        return self.cells_per_second / 1_000_000.0

    @property
    def output_mib(self) -> float:
        return self.output_bytes / BYTES_PER_MIB

    @property
    def worksheet_body_mib(self) -> float:
        return self.temporary_worksheet_part_footprint_bytes / BYTES_PER_MIB


def case_name_from_result(path: Path, data: dict[str, Any]) -> str:
    scenario = data.get("scenario")
    pattern = data.get("string_pattern")
    strategy = data.get("string_strategy")
    if isinstance(scenario, str) and isinstance(pattern, str) and isinstance(strategy, str):
        if scenario == "numeric":
            return path.stem
        return path.stem
    return path.stem


def parse_benchmark_case(path: Path, data: dict[str, Any], name: str | None = None,
    output: str | None = None, openpyxl_status: str | None = None) -> BenchmarkCase:
    schema_version = data.get("benchmark_schema_version")
    require(schema_version in SUPPORTED_BENCHMARK_SCHEMA_VERSIONS,
        f"{path} is not a supported benchmark schema version")

    rows = int(number(data.get("rows"), "rows"))
    cols = int(number(data.get("cols"), "cols"))
    sheets = int(number(data.get("sheets"), "sheets"))
    cells = int(number(data.get("cells"), "cells"))
    require(rows > 0 and cols > 0 and sheets > 0 and cells > 0,
        "rows, cols, sheets, and cells must be positive")
    require(cells == rows * cols * sheets, "cells must equal rows * cols * sheets")

    return BenchmarkCase(
        name=name or case_name_from_result(path, data),
        source=str(path),
        benchmark_schema_version=str(schema_version),
        scenario=text(data.get("scenario"), "scenario"),
        rows=rows,
        cols=cols,
        sheets=sheets,
        cells=cells,
        string_ratio=float(number(data.get("string_ratio"), "string_ratio")),
        string_pattern=text(data.get("string_pattern"), "string_pattern"),
        string_cells=int(number(data.get("string_cells"), "string_cells")),
        unique_string_values=int(number(data.get("unique_string_values"), "unique_string_values")),
        duplicate_string_cells=int(number(data.get("duplicate_string_cells"), "duplicate_string_cells")),
        string_dedup_ratio=float(number(data.get("string_dedup_ratio"), "string_dedup_ratio")),
        string_strategy=text(data.get("string_strategy"), "string_strategy"),
        zip_backend=text(data.get("zip_backend"), "zip_backend"),
        compression=text(data.get("compression"), "compression"),
        compression_level=optional_int(data.get("compression_level")),
        package_entry_source_mode=text(data.get("package_entry_source_mode"), "package_entry_source_mode"),
        temporary_worksheet_part_footprint=text(
            data.get("temporary_worksheet_part_footprint"),
            "temporary_worksheet_part_footprint"),
        temporary_worksheet_part_footprint_bytes=int(number(
            data.get("temporary_worksheet_part_footprint_bytes"),
            "temporary_worksheet_part_footprint_bytes")),
        elapsed_ms=int(number(data.get("elapsed_ms"), "elapsed_ms")),
        peak_memory_mb=float(number(data.get("peak_memory_mb"), "peak_memory_mb")),
        output_bytes=int(number(data.get("output_bytes"), "output_bytes")),
        office_open=text(data.get("office_open"), "office_open"),
        output=output,
        openpyxl_status=openpyxl_status,
    )


def is_matrix_report(data: dict[str, Any]) -> bool:
    return data.get("benchmark_matrix_schema_version") == MATRIX_REPORT_SCHEMA_VERSION


def is_benchmark_result(data: dict[str, Any]) -> bool:
    return data.get("benchmark_schema_version") in SUPPORTED_BENCHMARK_SCHEMA_VERSIONS


def is_summary_report(data: dict[str, Any]) -> bool:
    return data.get("benchmark_summary_schema_version") == SUMMARY_SCHEMA_VERSION


def collect_from_matrix(path: Path, data: dict[str, Any]) -> list[BenchmarkCase]:
    raw_cases = data.get("cases")
    require(isinstance(raw_cases, list), f"{path} matrix report cases must be a list")
    cases: list[BenchmarkCase] = []
    for index, raw_case in enumerate(raw_cases):
        require(isinstance(raw_case, dict), f"{path} matrix case {index} must be an object")
        result = raw_case.get("result")
        require(isinstance(result, dict), f"{path} matrix case {index} missing result object")
        openpyxl = raw_case.get("openpyxl")
        openpyxl_status = None
        if isinstance(openpyxl, dict):
            openpyxl_status = optional_text(openpyxl.get("status"))
        cases.append(parse_benchmark_case(
            path,
            result,
            name=optional_text(raw_case.get("name")) or f"case-{index + 1}",
            output=optional_text(raw_case.get("output")),
            openpyxl_status=openpyxl_status,
        ))
    return cases


def collect_from_file(path: Path, allow_skip: bool) -> tuple[list[BenchmarkCase], list[str]]:
    data = load_json(path)
    if is_matrix_report(data):
        return collect_from_matrix(path, data), []
    if is_benchmark_result(data):
        return [parse_benchmark_case(path, data)], []
    if allow_skip and is_summary_report(data):
        return [], []
    if allow_skip:
        return [], [f"Skipped non-benchmark JSON: {path}"]
    raise AssertionError(f"{path} is neither a schema-v4/v5 benchmark result nor a matrix report")


def iter_input_files(path: Path) -> Iterable[Path]:
    if path.is_dir():
        yield from sorted(p for p in path.iterdir() if p.suffix.lower() == ".json")
    else:
        yield path


def collect_from_directory(path: Path) -> tuple[list[BenchmarkCase], list[str]]:
    # Prefer benchmark-matrix-report.json over sibling per-case JSON files to avoid
    # double-counting the same benchmark run when a directory contains both forms.
    json_paths = sorted(p for p in path.iterdir() if p.suffix.lower() == ".json")
    classified: list[tuple[Path, str, dict[str, Any]]] = []
    has_matrix_report = False
    for json_path in json_paths:
        data = load_json(json_path)
        if is_matrix_report(data):
            classified.append((json_path, "matrix", data))
            has_matrix_report = True
        elif is_summary_report(data):
            classified.append((json_path, "summary", data))
        elif is_benchmark_result(data):
            classified.append((json_path, "benchmark", data))
        else:
            classified.append((json_path, "other", data))

    cases: list[BenchmarkCase] = []
    warnings: list[str] = []
    for json_path, kind, data in classified:
        if kind == "matrix":
            cases.extend(collect_from_matrix(json_path, data))
        elif kind == "benchmark":
            if not has_matrix_report:
                cases.append(parse_benchmark_case(json_path, data))
        elif kind == "other":
            warnings.append(f"Skipped non-benchmark JSON: {json_path}")
    require(cases, "no benchmark cases were found")
    return cases, warnings


def collect_cases(paths: list[Path]) -> tuple[list[BenchmarkCase], list[str]]:
    cases: list[BenchmarkCase] = []
    warnings: list[str] = []
    for input_path in paths:
        require(input_path.exists(), f"input path does not exist: {input_path}")
        if input_path.is_dir():
            file_cases, file_warnings = collect_from_directory(input_path)
            cases.extend(file_cases)
            warnings.extend(file_warnings)
        else:
            file_cases, file_warnings = collect_from_file(input_path, allow_skip=False)
            cases.extend(file_cases)
            warnings.extend(file_warnings)
    require(cases, "no benchmark cases were found")
    return cases, warnings


def benchmark_case_to_dict(case: BenchmarkCase) -> dict[str, Any]:
    return {
        "name": case.name,
        "source": case.source,
        "scenario": case.scenario,
        "rows": case.rows,
        "cols": case.cols,
        "sheets": case.sheets,
        "cells": case.cells,
        "elapsed_ms": case.elapsed_ms,
        "cells_per_second": case.cells_per_second,
        "million_cells_per_second": case.million_cells_per_second,
        "peak_memory_mb": case.peak_memory_mb,
        "output_bytes": case.output_bytes,
        "output_mib": case.output_mib,
        "temporary_worksheet_part_footprint": case.temporary_worksheet_part_footprint,
        "temporary_worksheet_part_footprint_bytes": case.temporary_worksheet_part_footprint_bytes,
        "worksheet_body_mib": case.worksheet_body_mib,
        "string_pattern": case.string_pattern,
        "string_strategy": case.string_strategy,
        "string_cells": case.string_cells,
        "unique_string_values": case.unique_string_values,
        "duplicate_string_cells": case.duplicate_string_cells,
        "string_dedup_ratio": case.string_dedup_ratio,
        "zip_backend": case.zip_backend,
        "compression": case.compression,
        "compression_level": case.compression_level,
        "package_entry_source_mode": case.package_entry_source_mode,
        "office_open": case.office_open,
        "openpyxl_status": case.openpyxl_status,
        "output": case.output,
    }


def build_warnings(cases: list[BenchmarkCase], initial_warnings: list[str],
    memory_warning_mb: float, large_output_warning_mib: float) -> list[str]:
    warnings = list(initial_warnings)
    office_not_run_cases: list[str] = []
    worksheet_body_only_cases: list[str] = []
    for case in cases:
        if case.peak_memory_mb >= memory_warning_mb:
            warnings.append(
                f"{case.name}: peak_memory_mb={case.peak_memory_mb:.3f} exceeds "
                f"{memory_warning_mb:.3f} MB warning threshold.")
        if case.output_mib >= large_output_warning_mib:
            warnings.append(
                f"{case.name}: output={case.output_mib:.1f} MiB exceeds "
                f"{large_output_warning_mib:.1f} MiB; keep disk/Zip64/package limits explicit.")
        if case.string_strategy == "shared" and case.string_pattern == "unique":
            warnings.append(
                f"{case.name}: sharedStrings with unique strings is memory-sensitive; "
                "do not present sharedStrings as the default best strategy.")
        if case.office_open == "not_run":
            office_not_run_cases.append(case.name)
        if case.temporary_worksheet_part_footprint == "worksheet-body-file-bytes":
            worksheet_body_only_cases.append(case.name)
    if office_not_run_cases:
        if len(office_not_run_cases) == len(cases):
            warnings.append(
                f"All {len(cases)} cases have office_open=not_run; "
                "Office/WPS/LibreOffice validation is separate.")
        else:
            warnings.append(
                f"{len(office_not_run_cases)} cases have office_open=not_run: "
                f"{', '.join(office_not_run_cases)}.")
    if worksheet_body_only_cases:
        if len(worksheet_body_only_cases) == len(cases):
            warnings.append(
                f"All {len(cases)} cases report worksheet-body-file-bytes; "
                "that is not full RSS/package/temp footprint.")
        else:
            warnings.append(
                f"{len(worksheet_body_only_cases)} cases report worksheet-body-file-bytes only: "
                f"{', '.join(worksheet_body_only_cases)}.")
    return dedupe_preserve_order(warnings)


def dedupe_preserve_order(values: Iterable[str]) -> list[str]:
    seen: set[str] = set()
    result: list[str] = []
    for value in values:
        if value in seen:
            continue
        seen.add(value)
        result.append(value)
    return result


def build_summary(paths: list[Path], cases: list[BenchmarkCase], warnings: list[str]) -> dict[str, Any]:
    fastest = max(cases, key=lambda case: case.cells_per_second)
    lowest_memory = min(cases, key=lambda case: case.peak_memory_mb)
    largest = max(cases, key=lambda case: case.cells)
    return {
        "benchmark_summary_schema_version": SUMMARY_SCHEMA_VERSION,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "input_paths": [str(path) for path in paths],
        "case_count": len(cases),
        "largest_case": largest.name,
        "fastest_case_by_cells_per_second": fastest.name,
        "lowest_peak_memory_case": lowest_memory.name,
        "cases": [benchmark_case_to_dict(case) for case in sorted_cases(cases)],
        "warnings": warnings,
        "notes": [
            "Benchmark inputs are opt-in local QA evidence, not default CTest/CI results.",
            "temporary_worksheet_part_footprint_bytes is worksheet body row XML bytes only.",
            "office_open=not_run means office-suite validation has not been performed by the benchmark executable.",
        ],
    }


def sorted_cases(cases: list[BenchmarkCase]) -> list[BenchmarkCase]:
    return sorted(cases, key=lambda case: (case.scenario, case.cells, case.name))


def fmt_int(value: int) -> str:
    return f"{value:,}"


def fmt_float(value: float, digits: int = 2) -> str:
    return f"{value:.{digits}f}"


def render_markdown(summary: dict[str, Any]) -> str:
    cases = summary["cases"]
    lines = [
        "# FastXLSX Benchmark Summary",
        "",
        f"- Generated UTC: `{summary['generated_at_utc']}`",
        f"- Inputs: `{', '.join(summary['input_paths'])}`",
        f"- Case count: `{summary['case_count']}`",
        f"- Largest case: `{summary['largest_case']}`",
        f"- Fastest case by cells/sec: `{summary['fastest_case_by_cells_per_second']}`",
        "",
        "| case | cells | elapsed ms | M cells/s | peak MB | output MiB | worksheet body MiB | strings | backend |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |",
    ]
    for case in cases:
        strings = f"{case['string_pattern']}/{case['string_strategy']}"
        backend = f"{case['zip_backend']}/{case['compression']}"
        if case["compression_level"] is not None:
            backend = f"{backend} ({case['compression_level']})"
        lines.append(
            "| "
            f"{case['name']} | "
            f"{fmt_int(int(case['cells']))} | "
            f"{fmt_int(int(case['elapsed_ms']))} | "
            f"{fmt_float(float(case['million_cells_per_second']))} | "
            f"{fmt_float(float(case['peak_memory_mb']))} | "
            f"{fmt_float(float(case['output_mib']))} | "
            f"{fmt_float(float(case['worksheet_body_mib']))} | "
            f"{strings} | "
            f"{backend} |"
        )

    lines.extend(["", "## Warnings"])
    if summary["warnings"]:
        lines.extend(f"- {warning}" for warning in summary["warnings"])
    else:
        lines.append("- None.")

    lines.extend(["", "## Notes"])
    lines.extend(f"- {note}" for note in summary["notes"])
    lines.append("")
    return "\n".join(lines)


def write_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def run_self_test() -> None:
    sample = {
        "benchmark_schema_version": "4",
        "scenario": "numeric",
        "rows": 10,
        "cols": 2,
        "sheets": 1,
        "cells": 20,
        "string_ratio": 0,
        "string_pattern": "mixed",
        "string_cells": 0,
        "unique_string_values": 0,
        "duplicate_string_cells": 0,
        "string_dedup_ratio": 0,
        "string_strategy": "inline",
        "zip_backend": "stored-bootstrap",
        "compression": "store",
        "compression_level": -1,
        "package_entry_source_mode": "worksheet-file-backed-chunked",
        "temporary_worksheet_part_footprint": "worksheet-body-file-bytes",
        "temporary_worksheet_part_footprint_bytes": 1000,
        "elapsed_ms": 10,
        "peak_memory_mb": 5,
        "output_bytes": 1200,
        "office_open": "not_run",
    }
    unique_shared = dict(sample)
    unique_shared.update({
        "scenario": "strings",
        "string_ratio": 1,
        "string_pattern": "unique",
        "string_cells": 20,
        "unique_string_values": 20,
        "string_strategy": "shared",
        "temporary_worksheet_part_footprint_bytes": 800,
        "elapsed_ms": 20,
        "peak_memory_mb": 128,
        "output_bytes": 1600,
    })
    with tempfile.TemporaryDirectory() as temp:
        temp_dir = Path(temp)
        raw_dir = temp_dir / "raw"
        raw_dir.mkdir()
        (raw_dir / "numeric.json").write_text(json.dumps(sample), encoding="utf-8")
        (raw_dir / "notes.json").write_text(json.dumps({
            "not": "a benchmark result",
        }), encoding="utf-8")
        raw_cases, raw_warnings = collect_cases([raw_dir])
        require(len(raw_cases) == 1, "self-test raw directory case count mismatch")
        require(len(raw_warnings) == 1 and "Skipped non-benchmark JSON" in raw_warnings[0],
            "self-test raw directory warning mismatch")

        matrix_dir = temp_dir / "matrix"
        matrix_dir.mkdir()
        (matrix_dir / "numeric.json").write_text(json.dumps(sample), encoding="utf-8")
        (matrix_dir / "matrix.json").write_text(json.dumps({
            "benchmark_matrix_schema_version": "1",
            "cases": [{
                "name": "strings-unique-shared",
                "output": "out.xlsx",
                "result": unique_shared,
                "openpyxl": {"status": "opened"},
            }],
        }), encoding="utf-8")
        (matrix_dir / "prior-summary.json").write_text(json.dumps({
            "benchmark_summary_schema_version": "1",
            "cases": [],
        }), encoding="utf-8")
        (matrix_dir / "notes.json").write_text(json.dumps({
            "not": "a benchmark result",
        }), encoding="utf-8")
        cases, initial_warnings = collect_cases([matrix_dir])
        require(len(cases) == 1, "self-test matrix directory case count mismatch")
        require(cases[0].name == "strings-unique-shared", "self-test matrix directory case name mismatch")
        require(len(initial_warnings) == 1 and "Skipped non-benchmark JSON" in initial_warnings[0],
            "self-test matrix directory warning mismatch")
        warnings = build_warnings(cases, initial_warnings, 64.0, 1024.0)
        require(any("unique strings" in warning for warning in warnings),
            "self-test missing unique sharedStrings warning")
        summary = build_summary([matrix_dir], cases, warnings)
        markdown = render_markdown(summary)
        require("FastXLSX Benchmark Summary" in markdown, "self-test markdown title mismatch")
        require("strings-unique-shared" in markdown, "self-test markdown case mismatch")
        markdown_path = temp_dir / "out" / "summary.md"
        json_path = temp_dir / "out" / "summary.json"
        write_text(markdown_path, markdown)
        write_text(json_path, json.dumps(summary, indent=2, ensure_ascii=False) + "\n")
        require(markdown_path.read_text(encoding="utf-8") == markdown,
            "self-test markdown output mismatch")
        written_summary = json.loads(json_path.read_text(encoding="utf-8"))
        require(written_summary["benchmark_summary_schema_version"] == SUMMARY_SCHEMA_VERSION,
            "self-test JSON output schema mismatch")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="*", type=Path,
        help="Benchmark JSON file, benchmark-matrix-report.json, or directory containing JSON results.")
    parser.add_argument("--output-markdown", type=Path,
        help="Write a Markdown summary to this path.")
    parser.add_argument("--output-json", type=Path,
        help="Write a machine-readable summary JSON to this path.")
    parser.add_argument("--memory-warning-mb", type=float, default=64.0,
        help="Warn when a case peak_memory_mb is at least this value.")
    parser.add_argument("--large-output-warning-mib", type=float, default=1024.0,
        help="Warn when output size is at least this many MiB.")
    parser.add_argument("--self-test", action="store_true",
        help="Run lightweight internal checks without reading benchmark output.")
    args = parser.parse_args()

    if args.self_test:
        run_self_test()
        print("OK: summarize_benchmark_results.py self-test passed")
        return 0

    require(args.inputs, "at least one input path is required")
    require(args.memory_warning_mb > 0, "memory warning threshold must be positive")
    require(args.large_output_warning_mib > 0, "large output warning threshold must be positive")

    input_paths = [path.resolve() for path in args.inputs]
    cases, initial_warnings = collect_cases(input_paths)
    warnings = build_warnings(
        cases,
        initial_warnings,
        memory_warning_mb=args.memory_warning_mb,
        large_output_warning_mib=args.large_output_warning_mib,
    )
    summary = build_summary(input_paths, cases, warnings)
    markdown = render_markdown(summary)

    if args.output_markdown:
        write_text(args.output_markdown.resolve(), markdown)
    if args.output_json:
        write_text(args.output_json.resolve(), json.dumps(summary, indent=2, ensure_ascii=False) + "\n")
    if not args.output_markdown and not args.output_json:
        print(markdown)
    else:
        print(f"OK: summarized {summary['case_count']} benchmark cases")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001 - CLI should emit one concise failure line.
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
