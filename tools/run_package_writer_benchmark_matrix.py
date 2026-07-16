#!/usr/bin/env python3
"""Run an isolated repeated FastXLSX package-writer buffer matrix."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import statistics
import subprocess
import sys
import tempfile
import zipfile
import zlib
from pathlib import Path
from typing import Any


BENCHMARK_SCHEMA_VERSION = "1"
MATRIX_SCHEMA_VERSION = "1"
DEFAULT_BUFFER_KIB = [256, 512, 1024, 2048, 4096]
METRICS = [
    "write_ms",
    "output_bytes",
    "peak_memory_mb",
    "package_writer_total_us",
    "package_writer_total_process_cpu_us",
    "package_writer_open_us",
    "package_writer_close_us",
    "target_entry_total_us",
    "target_entry_total_process_cpu_us",
    "target_entry_open_us",
    "target_entry_close_us",
    "target_entry_close_process_cpu_us",
    "target_entry_input_read_calls",
    "target_entry_input_read_us",
    "target_entry_input_read_wait_us",
    "target_entry_writer_write_calls",
    "target_entry_writer_write_us",
    "target_entry_writer_write_process_cpu_us",
    "target_entry_writer_write_input_peak_bytes",
    "target_entry_writer_write_max_us",
    "target_entry_deflate_writer_process_cpu_us",
    "target_entry_staged_crc_validation_us",
    "target_entry_prefetched_staged_file_chunk_count",
    "target_entry_prefetched_staged_input_bytes",
    "target_entry_prefetch_peak_buffer_bytes",
]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def default_benchmark_exe() -> Path:
    suffix = ".exe" if platform.system() == "Windows" else ""
    return (
        Path("build/windows-nmake-release-benchmark/benchmarks")
        / f"fastxlsx_bench_package_writer{suffix}"
    )


def validate_buffer_kib(value: int) -> int:
    if value < 64 or value > 4096 or value & (value - 1):
        raise ValueError("file buffer must be a power of two from 64 KiB to 4096 KiB")
    return value


def validate_compression_level(value: int) -> int:
    if value < 1 or value > 9:
        raise ValueError("compression level must be between 1 and 9")
    return value


def indexed_run_name(base_name: str, run_kind: str, run_index: int, run_count: int) -> str:
    require(run_index >= 1, "run index must be positive")
    require(run_count >= 1, "run count must be positive")
    width = max(2, len(str(run_count)))
    return f"{base_name}-{run_kind}-{run_index:0{width}d}"


def metric_summary(values: list[int | float]) -> dict[str, int | float]:
    return {
        "min": min(values),
        "median": statistics.median(values),
        "max": max(values),
    }


def measured_run_summary(
    runs: list[dict[str, Any]],
) -> tuple[int, dict[str, dict[str, int | float]]]:
    require(bool(runs), "measured runs must not be empty")
    elapsed = [run["result"]["target_entry_total_us"] for run in runs]
    elapsed_median = statistics.median(elapsed)
    representative_index = min(
        range(len(runs)),
        key=lambda index: (abs(elapsed[index] - elapsed_median), index),
    )
    report: dict[str, dict[str, int | float]] = {}
    for metric in METRICS:
        values = [run["result"][metric] for run in runs]
        report[metric] = metric_summary(values)
    return representative_index, report


def run_process(command: list[str]) -> subprocess.CompletedProcess[str]:
    creationflags = 0x00000080 if platform.system() == "Windows" else 0
    completed = subprocess.run(
        command,
        check=False,
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
        creationflags=creationflags,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            "benchmark command failed\n"
            + " ".join(command)
            + f"\nstdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    return completed


def read_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    require(isinstance(value, dict), f"expected JSON object: {path}")
    return value


def write_report(path: Path, report: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2)
        stream.write("\n")


def hash_and_crc32(path: Path) -> tuple[str, int, int]:
    digest = hashlib.sha256()
    crc32 = 0
    size = 0
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
            crc32 = zlib.crc32(chunk, crc32)
            size += len(chunk)
    return digest.hexdigest(), crc32 & 0xFFFFFFFF, size


def prepare_payload(
    bench_exe: Path,
    output_dir: Path,
    rows: int,
    cols: int,
    pattern: str,
) -> dict[str, Any]:
    payload = output_dir / "package-writer-payload.xml"
    result_path = output_dir / "payload-preparation.json"
    command = [
        str(bench_exe),
        "--prepare-only",
        "--rows",
        str(rows),
        "--cols",
        str(cols),
        "--pattern",
        pattern,
        "--payload",
        str(payload),
        "--result",
        str(result_path),
    ]
    completed = run_process(command)
    result = read_json(result_path)
    require(
        result.get("package_writer_benchmark_schema_version")
        == BENCHMARK_SCHEMA_VERSION,
        "preparation benchmark schema mismatch",
    )
    require(result.get("mode") == "prepare", "preparation mode mismatch")
    require(result.get("rows") == rows and result.get("cols") == cols, "preparation shape mismatch")
    require(result.get("pattern") == pattern, "preparation pattern mismatch")
    sha256, crc32, size = hash_and_crc32(payload)
    require(result.get("payload_bytes") == size, "preparation payload size mismatch")
    require(result.get("payload_crc32") == crc32, "preparation payload CRC mismatch")
    return {
        "command": command,
        "stdout": completed.stdout,
        "stderr": completed.stderr,
        "payload": str(payload),
        "payload_bytes": size,
        "payload_crc32": crc32,
        "payload_sha256": sha256,
        "payload_generation_ms": result["payload_generation_ms"],
    }


def inspect_archive(
    path: Path,
    payload_size: int,
    payload_crc32: int,
) -> dict[str, Any]:
    expected_names = {
        "[Content_Types].xml",
        "_rels/.rels",
        "xl/workbook.xml",
        "xl/_rels/workbook.xml.rels",
        "xl/worksheets/sheet1.xml",
    }
    with zipfile.ZipFile(path, "r") as archive:
        require(set(archive.namelist()) == expected_names, "output package entry set mismatch")
        worksheet = archive.getinfo("xl/worksheets/sheet1.xml")
        require(worksheet.compress_type == zipfile.ZIP_DEFLATED, "worksheet is not DEFLATE")
        require(worksheet.file_size == payload_size, "worksheet logical size mismatch")
        require(worksheet.CRC == payload_crc32, "worksheet CRC mismatch")
        bad_entry = archive.testzip()
        require(bad_entry is None, f"output ZIP CRC failure: {bad_entry}")
        return {
            "entry_count": len(archive.infolist()),
            "worksheet_compression_method": "deflate",
            "worksheet_uncompressed_bytes": worksheet.file_size,
            "worksheet_compressed_bytes": worksheet.compress_size,
            "worksheet_crc32": worksheet.CRC,
        }


def validate_result(
    result: dict[str, Any],
    output: Path,
    rows: int,
    cols: int,
    pattern: str,
    compression_level: int,
    buffer_kib: int,
    payload_size: int,
    payload_crc32: int,
) -> dict[str, Any]:
    require(
        result.get("package_writer_benchmark_schema_version")
        == BENCHMARK_SCHEMA_VERSION,
        "benchmark schema mismatch",
    )
    require(result.get("mode") == "timed-write", "timed mode mismatch")
    require(result.get("rows") == rows and result.get("cols") == cols, "timed shape mismatch")
    require(result.get("pattern") == pattern, "timed pattern mismatch")
    require(result.get("compression_level") == compression_level, "compression level mismatch")
    buffer_bytes = buffer_kib * 1024
    require(result.get("file_io_buffer_bytes") == buffer_bytes, "file buffer mismatch")
    require(result.get("payload_bytes") == payload_size, "timed payload size mismatch")
    require(result.get("payload_crc32") == payload_crc32, "timed payload CRC mismatch")
    require(result.get("target_entry_input_bytes") == payload_size, "entry input byte mismatch")
    expected_calls = (payload_size + buffer_bytes - 1) // buffer_bytes
    require(result.get("target_entry_input_read_calls") == expected_calls, "input read count mismatch")
    require(result.get("target_entry_writer_write_calls") == expected_calls, "writer call count mismatch")
    require(
        result.get("target_entry_writer_write_input_peak_bytes")
        == min(payload_size, buffer_bytes),
        "writer input peak mismatch",
    )
    require(result.get("target_entry_reused_staged_crc32") is True, "staged CRC was not reused")
    if platform.system() == "Windows" and payload_size >= 4 * 1024 * 1024:
        require(result.get("target_entry_staged_file_read_prefetch") is True, "prefetch was not active")
        require(
            result.get("target_entry_prefetched_staged_file_chunk_count") == 1,
            "prefetched chunk count mismatch",
        )
        require(
            result.get("target_entry_prefetched_staged_input_bytes") == payload_size,
            "prefetched byte count mismatch",
        )
        require(
            result.get("target_entry_prefetch_peak_buffer_bytes") == 2 * buffer_bytes,
            "prefetch buffer peak mismatch",
        )
    require(output.exists() and output.stat().st_size == result.get("output_bytes"), "output size mismatch")
    return inspect_archive(output, payload_size, payload_crc32)


def run_once(
    bench_exe: Path,
    output_dir: Path,
    base_name: str,
    run_kind: str,
    run_index: int,
    run_count: int,
    rows: int,
    cols: int,
    pattern: str,
    compression_level: int,
    buffer_kib: int,
    payload: Path,
    payload_size: int,
    payload_crc32: int,
) -> dict[str, Any]:
    name = indexed_run_name(base_name, run_kind, run_index, run_count)
    output = output_dir / f"{name}.xlsx"
    result_path = output_dir / f"{name}.json"
    command = [
        str(bench_exe),
        "--rows",
        str(rows),
        "--cols",
        str(cols),
        "--pattern",
        pattern,
        "--compression-level",
        str(compression_level),
        "--file-buffer-kib",
        str(buffer_kib),
        "--payload-size",
        str(payload_size),
        "--payload-crc32",
        str(payload_crc32),
        "--payload",
        str(payload),
        "--output",
        str(output),
        "--result",
        str(result_path),
    ]
    completed = run_process(command)
    result = read_json(result_path)
    archive = validate_result(
        result,
        output,
        rows,
        cols,
        pattern,
        compression_level,
        buffer_kib,
        payload_size,
        payload_crc32,
    )
    return {
        "name": name,
        "command": command,
        "stdout": completed.stdout,
        "stderr": completed.stderr,
        "result": result,
        "archive": archive,
    }


def verify_workbook_with_openpyxl(path: Path, rows: int, cols: int) -> dict[str, Any]:
    try:
        import openpyxl
    except ImportError as exc:
        raise RuntimeError("openpyxl is required for --verify-openpyxl") from exc
    workbook = openpyxl.load_workbook(path, read_only=True, data_only=False)
    try:
        require(workbook.sheetnames == ["Data"], "openpyxl sheet catalog mismatch")
        worksheet = workbook["Data"]
        require(worksheet.max_row == rows, "openpyxl max row mismatch")
        require(worksheet.max_column == cols, "openpyxl max column mismatch")
        require(worksheet["A1"].value == 1, "openpyxl A1 value mismatch")
        return {
            "status": "opened",
            "openpyxl_version": openpyxl.__version__,
            "sheet_count": 1,
            "sheet1_max_row": worksheet.max_row,
            "sheet1_max_column": worksheet.max_column,
            "sheet1_first_cell": worksheet["A1"].value,
        }
    finally:
        workbook.close()


def run_case(
    bench_exe: Path,
    output_dir: Path,
    rows: int,
    cols: int,
    pattern: str,
    compression_level: int,
    buffer_kib: int,
    payload: Path,
    payload_size: int,
    payload_crc32: int,
    warmup_runs: int,
    measured_runs: int,
    verify_openpyxl: bool,
) -> dict[str, Any]:
    base_name = f"buffer-{buffer_kib}kib-level-{compression_level}"
    warmups = [
        run_once(
            bench_exe,
            output_dir,
            base_name,
            "warmup",
            index,
            warmup_runs,
            rows,
            cols,
            pattern,
            compression_level,
            buffer_kib,
            payload,
            payload_size,
            payload_crc32,
        )
        for index in range(1, warmup_runs + 1)
    ]
    measured = [
        run_once(
            bench_exe,
            output_dir,
            base_name,
            "measured",
            index,
            measured_runs,
            rows,
            cols,
            pattern,
            compression_level,
            buffer_kib,
            payload,
            payload_size,
            payload_crc32,
        )
        for index in range(1, measured_runs + 1)
    ]
    representative_index, statistics_report = measured_run_summary(measured)
    representative = measured[representative_index]
    openpyxl_report = (
        verify_workbook_with_openpyxl(
            Path(representative["result"]["output"]), rows, cols
        )
        if verify_openpyxl
        else {"status": "not_requested"}
    )
    return {
        "name": base_name,
        "compression_level": compression_level,
        "file_buffer_kib": buffer_kib,
        "warmup_runs": warmup_runs,
        "measured_runs": measured_runs,
        "representative_run": representative_index + 1,
        "statistics": statistics_report,
        "result": representative["result"],
        "archive": representative["archive"],
        "openpyxl": openpyxl_report,
        "warmups": warmups,
        "runs": measured,
    }


def run_self_test() -> None:
    for value in DEFAULT_BUFFER_KIB:
        require(validate_buffer_kib(value) == value, "valid buffer rejected")
    for invalid in [0, 63, 96, 8192]:
        try:
            validate_buffer_kib(invalid)
        except ValueError:
            pass
        else:
            raise AssertionError(f"invalid buffer accepted: {invalid}")
    reports = [
        {"result": {metric: index + 1 for metric in METRICS}}
        for index in [2, 0, 1]
    ]
    representative, summary = measured_run_summary(reports)
    require(representative == 2, "representative selection mismatch")
    require(summary["target_entry_total_us"] == {"min": 1, "median": 2, "max": 3},
        "statistics mismatch")
    require(indexed_run_name("case", "run", 1, 3) == "case-run-01", "run name mismatch")
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        payload = root / "payload.bin"
        payload.write_bytes(b"package-writer-self-test")
        sha256, crc32, size = hash_and_crc32(payload)
        require(len(sha256) == 64 and size == 24, "hash metadata mismatch")
        archive_path = root / "archive.xlsx"
        with zipfile.ZipFile(archive_path, "w", zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("[Content_Types].xml", "types")
            archive.writestr("_rels/.rels", "rels")
            archive.writestr("xl/workbook.xml", "workbook")
            archive.writestr("xl/_rels/workbook.xml.rels", "workbook-rels")
            archive.writestr("xl/worksheets/sheet1.xml", payload.read_bytes())
        inspected = inspect_archive(archive_path, size, crc32)
        require(inspected["worksheet_uncompressed_bytes"] == size, "archive inspection mismatch")
    print("OK: run_package_writer_benchmark_matrix.py self-test passed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bench-exe", type=Path, default=default_benchmark_exe())
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("build/qa/package-writer-benchmark-matrix"),
    )
    parser.add_argument("--rows", type=int, default=100000)
    parser.add_argument("--cols", type=int, default=10)
    parser.add_argument(
        "--pattern",
        choices=["numeric", "mixed-inline", "formula"],
        default="numeric",
    )
    parser.add_argument("--compression-level", type=int, action="append")
    parser.add_argument("--file-buffer-kib", type=int, action="append")
    parser.add_argument("--warmup-runs", type=int, default=2)
    parser.add_argument("--measured-runs", type=int, default=5)
    parser.add_argument("--verify-openpyxl", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        run_self_test()
        return 0

    require(args.rows > 0 and args.rows <= 1048576, "rows out of range")
    require(args.cols > 0 and args.cols <= 16384, "cols out of range")
    require(args.warmup_runs >= 0, "warmup runs must be non-negative")
    require(args.measured_runs > 0, "measured runs must be positive")
    compression_levels = args.compression_level or [1]
    buffer_values = args.file_buffer_kib or DEFAULT_BUFFER_KIB
    compression_levels = [validate_compression_level(value) for value in compression_levels]
    buffer_values = [validate_buffer_kib(value) for value in buffer_values]
    require(len(set(compression_levels)) == len(compression_levels), "duplicate compression level")
    require(len(set(buffer_values)) == len(buffer_values), "duplicate file buffer")

    bench_exe = args.bench_exe.resolve()
    require(bench_exe.is_file(), f"benchmark executable does not exist: {bench_exe}")
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    preparation = prepare_payload(
        bench_exe, output_dir, args.rows, args.cols, args.pattern
    )
    payload = Path(preparation["payload"])

    cases: list[dict[str, Any]] = []
    for compression_level in compression_levels:
        for buffer_kib in buffer_values:
            case = run_case(
                bench_exe,
                output_dir,
                args.rows,
                args.cols,
                args.pattern,
                compression_level,
                buffer_kib,
                payload,
                preparation["payload_bytes"],
                preparation["payload_crc32"],
                args.warmup_runs,
                args.measured_runs,
                args.verify_openpyxl,
            )
            cases.append(case)
            statistics_report = case["statistics"]
            print(
                f"{case['name']}: entry={statistics_report['target_entry_total_us']['median']} us, "
                f"writer={statistics_report['target_entry_writer_write_us']['median']} us, "
                f"cpu={statistics_report['target_entry_deflate_writer_process_cpu_us']['median']} us, "
                f"peak={statistics_report['peak_memory_mb']['median']} MB"
            )

    report = {
        "package_writer_matrix_schema_version": MATRIX_SCHEMA_VERSION,
        "benchmark_executable": str(bench_exe),
        "output_dir": str(output_dir),
        "rows": args.rows,
        "cols": args.cols,
        "source_cells": args.rows * args.cols,
        "pattern": args.pattern,
        "compression_levels": compression_levels,
        "file_buffer_kib_values": buffer_values,
        "warmup_runs_per_case": args.warmup_runs,
        "measured_runs_per_case": args.measured_runs,
        "representative_result_policy":
            "Measured run nearest target_entry_total_us median; earliest run breaks ties.",
        "process_priority": "Windows High priority" if platform.system() == "Windows" else "inherited",
        "preparation": preparation,
        "cases": cases,
        "scope":
            "Isolated staged worksheet payload -> production minizip-ng package writer. "
            "Payload generation and openpyxl validation are outside timed benchmark processes; "
            "Office remains not_run.",
    }
    report_path = output_dir / "package-writer-benchmark-matrix-report.json"
    write_report(report_path, report)
    print(f"Wrote {report_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, RuntimeError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
