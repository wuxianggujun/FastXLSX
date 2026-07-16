#!/usr/bin/env python3
"""Run a balanced paired package-writer backend benchmark."""

from __future__ import annotations

import argparse
import json
import platform
import sys
from pathlib import Path
from typing import Any

from run_package_writer_benchmark_matrix import (
    BENCHMARK_SCHEMA_VERSION,
    default_benchmark_exe,
    measured_run_summary,
    prepare_payload,
    require,
    run_once,
    validate_buffer_kib,
    validate_compression_level,
    validate_deflate_output_kib,
    verify_workbook_with_openpyxl,
    write_report,
)


PAIRED_MATRIX_SCHEMA_VERSION = "1"
DEFAULT_DIRECT_OUTPUT_KIB = [32, 64, 256]


def case_name(case: dict[str, Any]) -> str:
    if case["backend"] == "minizip":
        return "minizip"
    return f"direct-zlib-one-pass-{case['deflate_output_kib']}kib"


def balanced_case_order(
    cases: list[dict[str, Any]], round_index: int,
) -> list[dict[str, Any]]:
    require(bool(cases), "paired cases must not be empty")
    cycle = round_index // len(cases)
    ordered = list(reversed(cases)) if cycle % 2 else list(cases)
    shift = round_index % len(cases)
    return ordered[shift:] + ordered[:shift]


def run_rounds(
    bench_exe: Path,
    output_dir: Path,
    cases: list[dict[str, Any]],
    run_kind: str,
    round_count: int,
    rows: int,
    cols: int,
    pattern: str,
    compression_level: int,
    file_buffer_kib: int,
    payload: Path,
    payload_size: int,
    payload_crc32: int,
) -> dict[str, list[dict[str, Any]]]:
    runs = {case_name(case): [] for case in cases}
    for round_index in range(round_count):
        for position, case in enumerate(balanced_case_order(cases, round_index), start=1):
            name = case_name(case)
            run = run_once(
                bench_exe,
                output_dir,
                f"paired-{name}-round-{round_index + 1:02d}-position-{position:02d}",
                run_kind,
                round_index + 1,
                round_count,
                rows,
                cols,
                pattern,
                case["backend"],
                compression_level,
                file_buffer_kib,
                case["deflate_output_kib"],
                payload,
                payload_size,
                payload_crc32,
            )
            run["round"] = round_index + 1
            run["position"] = position
            runs[name].append(run)
    return runs


def summarize_cases(
    cases: list[dict[str, Any]],
    warmups: dict[str, list[dict[str, Any]]],
    measured: dict[str, list[dict[str, Any]]],
    verify_openpyxl: bool,
    rows: int,
    cols: int,
) -> list[dict[str, Any]]:
    reports: list[dict[str, Any]] = []
    for case in cases:
        name = case_name(case)
        representative_index, statistics_report = measured_run_summary(measured[name])
        representative = measured[name][representative_index]
        openpyxl_report = (
            verify_workbook_with_openpyxl(
                Path(representative["result"]["output"]), rows, cols
            )
            if verify_openpyxl
            else {"status": "not_requested"}
        )
        reports.append(
            {
                "name": name,
                "backend": case["backend"],
                "direct_deflate_output_buffer_kib": (
                    case["deflate_output_kib"]
                    if case["backend"] == "direct-zlib-one-pass"
                    else 0
                ),
                "representative_run": representative_index + 1,
                "statistics": statistics_report,
                "result": representative["result"],
                "archive": representative["archive"],
                "openpyxl": openpyxl_report,
                "warmups": warmups[name],
                "runs": measured[name],
            }
        )
    return reports


def run_self_test() -> None:
    cases = [
        {"backend": "minizip", "deflate_output_kib": 0},
        {"backend": "direct-zlib-one-pass", "deflate_output_kib": 32},
        {"backend": "direct-zlib-one-pass", "deflate_output_kib": 64},
        {"backend": "direct-zlib-one-pass", "deflate_output_kib": 256},
    ]
    positions = {case_name(case): [] for case in cases}
    for round_index in range(2 * len(cases)):
        order = balanced_case_order(cases, round_index)
        require(len({case_name(case) for case in order}) == len(cases), "case duplication")
        for position, case in enumerate(order, start=1):
            positions[case_name(case)].append(position)
    expected = list(range(1, len(cases) + 1)) * 2
    for values in positions.values():
        require(sorted(values) == sorted(expected), "balanced position coverage mismatch")
    print("OK: run_package_writer_paired_benchmark.py self-test passed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bench-exe", type=Path, default=default_benchmark_exe())
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("build/qa/package-writer-paired-benchmark"),
    )
    parser.add_argument("--rows", type=int, default=100000)
    parser.add_argument("--cols", type=int, default=10)
    parser.add_argument(
        "--pattern",
        choices=["numeric", "mixed-inline", "formula"],
        default="numeric",
    )
    parser.add_argument("--compression-level", type=int, default=1)
    parser.add_argument("--file-buffer-kib", type=int, default=512)
    parser.add_argument("--direct-output-kib", type=int, action="append")
    parser.add_argument("--warmup-rounds", type=int, default=8)
    parser.add_argument("--measured-rounds", type=int, default=16)
    parser.add_argument("--verify-openpyxl", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        run_self_test()
        return 0

    require(0 < args.rows <= 1048576, "rows out of range")
    require(0 < args.cols <= 16384, "cols out of range")
    require(args.warmup_rounds >= 0, "warmup rounds must be non-negative")
    require(args.measured_rounds > 0, "measured rounds must be positive")
    compression_level = validate_compression_level(args.compression_level)
    file_buffer_kib = validate_buffer_kib(args.file_buffer_kib)
    output_values = args.direct_output_kib or DEFAULT_DIRECT_OUTPUT_KIB
    output_values = [validate_deflate_output_kib(value) for value in output_values]
    require(len(set(output_values)) == len(output_values), "duplicate direct output buffer")

    cases = [{"backend": "minizip", "deflate_output_kib": 0}]
    cases.extend(
        {"backend": "direct-zlib-one-pass", "deflate_output_kib": value}
        for value in output_values
    )
    require(
        args.measured_rounds % (2 * len(cases)) == 0,
        "measured rounds must be a multiple of twice the case count",
    )

    bench_exe = args.bench_exe.resolve()
    require(bench_exe.is_file(), f"benchmark executable does not exist: {bench_exe}")
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    preparation = prepare_payload(
        bench_exe, output_dir, args.rows, args.cols, args.pattern
    )
    payload = Path(preparation["payload"])

    warmups = run_rounds(
        bench_exe,
        output_dir,
        cases,
        "warmup",
        args.warmup_rounds,
        args.rows,
        args.cols,
        args.pattern,
        compression_level,
        file_buffer_kib,
        payload,
        preparation["payload_bytes"],
        preparation["payload_crc32"],
    )
    measured = run_rounds(
        bench_exe,
        output_dir,
        cases,
        "measured",
        args.measured_rounds,
        args.rows,
        args.cols,
        args.pattern,
        compression_level,
        file_buffer_kib,
        payload,
        preparation["payload_bytes"],
        preparation["payload_crc32"],
    )
    case_reports = summarize_cases(
        cases,
        warmups,
        measured,
        args.verify_openpyxl,
        args.rows,
        args.cols,
    )
    for case in case_reports:
        stats = case["statistics"]
        print(
            f"{case['name']}: pipeline={stats['pipeline_total_us']['median']} us, "
            f"entry={stats['target_entry_total_us']['median']} us, "
            f"cpu={stats['target_entry_deflate_writer_process_cpu_us']['median']} us, "
            f"peak={stats['peak_memory_mb']['median']} MB"
        )

    report = {
        "package_writer_paired_matrix_schema_version": PAIRED_MATRIX_SCHEMA_VERSION,
        "package_writer_benchmark_schema_version": BENCHMARK_SCHEMA_VERSION,
        "benchmark_executable": str(bench_exe),
        "output_dir": str(output_dir),
        "rows": args.rows,
        "cols": args.cols,
        "source_cells": args.rows * args.cols,
        "pattern": args.pattern,
        "compression_level": compression_level,
        "file_buffer_kib": file_buffer_kib,
        "direct_deflate_output_buffer_kib_values": output_values,
        "warmup_rounds": args.warmup_rounds,
        "measured_rounds": args.measured_rounds,
        "schedule": (
            "Balanced round-robin rotation with reversed cycles; measured rounds place "
            "every case in every position equally often."
        ),
        "representative_result_policy": (
            "Measured run nearest pipeline_total_us median; earliest run breaks ties."
        ),
        "process_priority": (
            "Windows High priority" if platform.system() == "Windows" else "inherited"
        ),
        "preparation": preparation,
        "cases": case_reports,
        "scope": (
            "Same staged worksheet payload, compression level and file buffer across "
            "production minizip DEFLATE and bounded one-pass direct zlib raw-entry output. "
            "Payload generation and openpyxl validation are outside timed processes; "
            "Office remains not_run."
        ),
    }
    report_path = output_dir / "package-writer-paired-benchmark-report.json"
    write_report(report_path, report)
    print(f"Wrote {report_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
