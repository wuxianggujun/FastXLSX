#!/usr/bin/env python3
"""Run a balanced paired Patch benchmark for PackageEditor CRC32 profiles."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import statistics
import sys
import zipfile
from pathlib import Path
from typing import Any

from run_patch_benchmark_matrix import (
    BENCHMARK_SCHEMA_VERSION,
    PatchCase,
    inspect_archive,
    measured_run_summary,
    metric_summary,
    raw_entry_payload,
    require,
    run_single_case,
    verify_workbook_with_openpyxl,
    write_report,
)


PAIRED_MATRIX_SCHEMA_VERSION = "3"
TARGET_WORKSHEET_ENTRY = "xl/worksheets/sheet1.xml"
PAIRED_METRICS = [
    "total_editor_ms",
    "mutation_ms",
    "single_pass_transform_us",
    "single_pass_crc32_us",
    "single_pass_commit_ms",
    "save_ms",
    "total_editor_process_cpu_us",
    "mutation_process_cpu_us",
    "save_process_cpu_us",
    "peak_memory_mb",
]


def default_benchmark_exe(profile: str) -> Path:
    suffix = ".exe" if platform.system() == "Windows" else ""
    return Path(
        f"build/windows-nmake-crc-{profile}-profile/benchmarks/"
        f"fastxlsx_bench_workbook_editor{suffix}"
    )


def balanced_profile_order(
    profiles: list[dict[str, Any]], round_index: int
) -> list[dict[str, Any]]:
    require(bool(profiles), "paired profiles must not be empty")
    cycle = round_index // len(profiles)
    ordered = list(reversed(profiles)) if cycle % 2 else list(profiles)
    shift = round_index % len(profiles)
    return ordered[shift:] + ordered[:shift]


def target_entry_fingerprint(path: Path) -> dict[str, Any]:
    with zipfile.ZipFile(path) as archive:
        info = archive.getinfo(TARGET_WORKSHEET_ENTRY)
        logical_payload = archive.read(info)
    compressed_payload = raw_entry_payload(path, info)
    return {
        "entry": TARGET_WORKSHEET_ENTRY,
        "crc32": f"{info.CRC:08x}",
        "uncompressed_bytes": info.file_size,
        "compressed_bytes": info.compress_size,
        "logical_sha256": hashlib.sha256(logical_payload).hexdigest(),
        "compressed_payload_sha256": hashlib.sha256(compressed_payload).hexdigest(),
    }


def run_rounds(
    profiles: list[dict[str, Any]],
    output_dir: Path,
    source_path: Path,
    case: PatchCase,
    run_kind: str,
    round_count: int,
    rows: int,
    cols: int,
    source_compression_level: int,
    output_compression_level: int,
    source_pattern: str,
) -> dict[str, list[dict[str, Any]]]:
    runs = {profile["name"]: [] for profile in profiles}
    for round_index in range(round_count):
        order = balanced_profile_order(profiles, round_index)
        for position, profile in enumerate(order, start=1):
            name = profile["name"]
            run = run_single_case(
                profile["bench_exe"],
                output_dir,
                source_path,
                case,
                rows,
                cols,
                source_compression_level,
                output_compression_level,
                source_pattern,
                False,
                f"paired-{name}-{run_kind}-round-{round_index + 1:02d}"
                f"-position-{position:02d}",
                True,
                profile.get("schema_version", BENCHMARK_SCHEMA_VERSION),
            )
            require(
                run["result"]["package_editor_crc32_backend"]
                == profile["crc32_backend"],
                f"{name} executable CRC32 backend identity mismatch",
            )
            run["result"].setdefault("single_pass_fused_crc32", False)
            run["result"].setdefault("single_pass_crc32_us", 0)
            run["result"].setdefault("single_pass_crc32_segment_count", 0)
            run["round"] = round_index + 1
            run["position"] = position
            run["target_worksheet_fingerprint"] = target_entry_fingerprint(
                Path(run["output"])
            )
            runs[name].append(run)
    return runs


def verify_round_output_parity(
    baseline_runs: list[dict[str, Any]], candidate_runs: list[dict[str, Any]]
) -> None:
    require(len(baseline_runs) == len(candidate_runs), "paired run count mismatch")
    for round_index, (baseline, candidate) in enumerate(
        zip(baseline_runs, candidate_runs, strict=True), start=1
    ):
        require(baseline["round"] == round_index, "baseline round ordering mismatch")
        require(candidate["round"] == round_index, "candidate round ordering mismatch")
        require(
            baseline["target_worksheet_fingerprint"]
            == candidate["target_worksheet_fingerprint"],
            f"paired round {round_index} target worksheet bytes differ",
        )
        require(
            baseline["result"]["output_bytes"] == candidate["result"]["output_bytes"],
            f"paired round {round_index} package size differs",
        )


def verify_balanced_positions(runs: dict[str, list[dict[str, Any]]]) -> None:
    expected_count = None
    for name, profile_runs in runs.items():
        positions: dict[int, int] = {}
        for run in profile_runs:
            position = int(run["position"])
            positions[position] = positions.get(position, 0) + 1
        require(set(positions) == {1, 2}, f"{name} position coverage mismatch")
        require(len(set(positions.values())) == 1,
            f"{name} positions must have equal run counts")
        if expected_count is None:
            expected_count = positions
        else:
            require(positions == expected_count, "profile position counts differ")


def paired_values(
    baseline_runs: list[dict[str, Any]],
    candidate_runs: list[dict[str, Any]],
    metric: str,
    indexes: list[int],
) -> dict[str, Any]:
    deltas: list[int | float] = []
    percentages: list[float] = []
    rounds: list[dict[str, Any]] = []
    for index in indexes:
        baseline = baseline_runs[index]
        candidate = candidate_runs[index]
        baseline_value = baseline["result"][metric]
        candidate_value = candidate["result"][metric]
        delta = candidate_value - baseline_value
        percent = (
            None
            if baseline_value == 0
            else round((delta / baseline_value) * 100.0, 6)
        )
        deltas.append(delta)
        if percent is not None:
            percentages.append(percent)
        rounds.append(
            {
                "round": baseline["round"],
                "baseline_position": baseline["position"],
                "candidate_position": candidate["position"],
                "baseline": baseline_value,
                "candidate": candidate_value,
                "candidate_minus_baseline": delta,
                "candidate_minus_baseline_percent": percent,
            }
        )
    return {
        "delta": metric_summary(deltas),
        "percent": metric_summary(percentages) if percentages else None,
        "candidate_win_count": sum(value < 0 for value in deltas),
        "baseline_win_count": sum(value > 0 for value in deltas),
        "tie_count": sum(value == 0 for value in deltas),
        "rounds": rounds,
    }


def paired_metric_summary(
    baseline_runs: list[dict[str, Any]],
    candidate_runs: list[dict[str, Any]],
    baseline_name: str,
    candidate_name: str,
) -> dict[str, Any]:
    require(len(baseline_runs) == len(candidate_runs), "paired metric run count mismatch")
    summaries: dict[str, Any] = {}
    for metric in PAIRED_METRICS:
        all_indexes = list(range(len(baseline_runs)))
        overall = paired_values(baseline_runs, candidate_runs, metric, all_indexes)
        by_candidate_position = {}
        for position in (1, 2):
            indexes = [
                index
                for index, run in enumerate(candidate_runs)
                if int(run["position"]) == position
            ]
            by_candidate_position[str(position)] = paired_values(
                baseline_runs, candidate_runs, metric, indexes
            )
        summaries[metric] = {
            "direction": (
                f"{candidate_name} minus {baseline_name}; negative favors {candidate_name}"
            ),
            **overall,
            "by_candidate_position": by_candidate_position,
        }
    return summaries


def summarize_profile(
    profile: dict[str, Any],
    warmups: dict[str, list[dict[str, Any]]],
    measured: dict[str, list[dict[str, Any]]],
    verify_openpyxl: bool,
    case: PatchCase,
    rows: int,
    cols: int,
    source_pattern: str,
) -> dict[str, Any]:
    name = profile["name"]
    representative_index, statistics_report = measured_run_summary(measured[name])
    representative = measured[name][representative_index]
    openpyxl_report = (
        verify_workbook_with_openpyxl(
            Path(representative["output"]),
            case,
            rows,
            cols,
            source_pattern,
            False,
        )
        if verify_openpyxl
        else {"status": "not_requested"}
    )
    return {
        "name": name,
        "crc32_backend": profile["crc32_backend"],
        "benchmark_executable": str(profile["bench_exe"]),
        "representative_run": representative_index + 1,
        "statistics": statistics_report,
        "result": representative["result"],
        "byte_accounting": representative["byte_accounting"],
        "output_archive": representative["output_archive"],
        "target_worksheet_fingerprint": representative[
            "target_worksheet_fingerprint"
        ],
        "openpyxl": openpyxl_report,
        "warmups": warmups[name],
        "runs": measured[name],
    }


def run_self_test() -> None:
    profiles = [
        {"name": "portable", "crc32_backend": "portable"},
        {"name": "minizip-ng", "crc32_backend": "minizip-ng"},
    ]
    positions = {profile["name"]: [] for profile in profiles}
    for round_index in range(2 * len(profiles)):
        order = balanced_profile_order(profiles, round_index)
        require(len({profile["name"] for profile in order}) == 2, "profile duplication")
        for position, profile in enumerate(order, start=1):
            positions[profile["name"]].append(position)
    for values in positions.values():
        require(sorted(values) == [1, 1, 2, 2], "balanced position coverage mismatch")

    baseline = [
        {"round": 1, "position": 1,
            "result": {metric: 100 for metric in PAIRED_METRICS}},
        {"round": 2, "position": 2,
            "result": {metric: 120 for metric in PAIRED_METRICS}},
    ]
    candidate = [
        {"round": 1, "position": 2,
            "result": {metric: 90 for metric in PAIRED_METRICS}},
        {"round": 2, "position": 1,
            "result": {metric: 100 for metric in PAIRED_METRICS}},
    ]
    summary = paired_metric_summary(baseline, candidate, "baseline", "candidate")
    require(summary["total_editor_ms"]["delta"]["median"] == -15.0,
        "paired delta median mismatch")
    require(
        statistics.median(
            round_data["candidate_minus_baseline_percent"]
            for round_data in summary["total_editor_ms"]["rounds"]
        ) < 0,
        "paired percent direction mismatch",
    )
    require(
        summary["total_editor_ms"]["by_candidate_position"]["1"]
        ["delta"]["median"] == -20,
        "position-stratified paired delta mismatch",
    )
    print("OK: run_patch_crc_paired_benchmark.py self-test passed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--baseline-bench-exe", "--portable-bench-exe",
        dest="baseline_bench_exe",
        type=Path,
        default=default_benchmark_exe("portable"),
    )
    parser.add_argument(
        "--candidate-bench-exe", "--minizip-bench-exe",
        dest="candidate_bench_exe",
        type=Path,
        default=default_benchmark_exe("minizip"),
    )
    parser.add_argument("--baseline-name", default="portable")
    parser.add_argument("--candidate-name", default="minizip-ng")
    parser.add_argument(
        "--baseline-crc-backend",
        choices=["portable", "minizip-ng"],
        default="portable",
    )
    parser.add_argument(
        "--candidate-crc-backend",
        choices=["portable", "minizip-ng"],
        default="minizip-ng",
    )
    parser.add_argument("--baseline-schema-version", default=BENCHMARK_SCHEMA_VERSION)
    parser.add_argument("--candidate-schema-version", default=BENCHMARK_SCHEMA_VERSION)
    parser.add_argument("--comparison-label", default="crc-backend")
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("build/qa/patch-crc-paired-benchmark"),
    )
    parser.add_argument("--rows", type=int, default=200000)
    parser.add_argument("--cols", type=int, default=10)
    parser.add_argument("--edits", type=int, default=1000)
    parser.add_argument(
        "--source-pattern",
        choices=["numeric", "mixed-inline", "mixed-shared", "formula"],
        default="numeric",
    )
    parser.add_argument("--source-compression-level", type=int, default=6)
    parser.add_argument("--output-compression-level", type=int, default=1)
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
    require(args.edits > 0, "edits must be positive")
    require(bool(args.baseline_name), "baseline name must not be empty")
    require(bool(args.candidate_name), "candidate name must not be empty")
    require(args.baseline_name != args.candidate_name,
        "baseline and candidate names must differ")
    require(args.baseline_schema_version.isdigit(),
        "baseline schema version must be numeric")
    require(args.candidate_schema_version.isdigit(),
        "candidate schema version must be numeric")
    require(0 <= args.source_compression_level <= 9,
        "source compression level must be 0..9")
    require(-1 <= args.output_compression_level <= 9,
        "output compression level must be -1..9")
    require(args.output_compression_level != 0,
        "CRC paired profile requires a DEFLATE output")
    require(args.warmup_rounds >= 0, "warmup rounds must be non-negative")
    require(args.measured_rounds > 0, "measured rounds must be positive")
    profile_count = 2
    schedule_period = 2 * profile_count
    require(
        args.warmup_rounds == 0 or args.warmup_rounds % schedule_period == 0,
        "warmup rounds must be zero or a multiple of four",
    )
    require(
        args.measured_rounds % schedule_period == 0,
        "measured rounds must be a multiple of four",
    )

    profiles = [
        {
            "name": args.baseline_name,
            "crc32_backend": args.baseline_crc_backend,
            "schema_version": args.baseline_schema_version,
            "bench_exe": args.baseline_bench_exe.resolve(),
        },
        {
            "name": args.candidate_name,
            "crc32_backend": args.candidate_crc_backend,
            "schema_version": args.candidate_schema_version,
            "bench_exe": args.candidate_bench_exe.resolve(),
        },
    ]
    for profile in profiles:
        require(
            profile["bench_exe"].is_file(),
            f"benchmark executable not found: {profile['bench_exe']}",
        )

    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    baseline_name = profiles[0]["name"]
    candidate_name = profiles[1]["name"]
    source_path = output_dir / "patch-paired-source.xlsx"
    case = PatchCase("patch-upsert", args.edits)
    preparation = run_single_case(
        profiles[1]["bench_exe"],
        output_dir,
        source_path,
        PatchCase("noop-copy", 0),
        args.rows,
        args.cols,
        args.source_compression_level,
        args.output_compression_level,
        args.source_pattern,
        False,
        "source-preparation",
        False,
        profiles[1]["schema_version"],
    )
    require(
        preparation["result"]["package_editor_crc32_backend"]
        == profiles[1]["crc32_backend"],
        "source preparation executable CRC32 backend identity mismatch",
    )
    source_archive = inspect_archive(
        source_path, args.source_compression_level, "paired source"
    )

    warmups = run_rounds(
        profiles,
        output_dir,
        source_path,
        case,
        "warmup",
        args.warmup_rounds,
        args.rows,
        args.cols,
        args.source_compression_level,
        args.output_compression_level,
        args.source_pattern,
    )
    measured = run_rounds(
        profiles,
        output_dir,
        source_path,
        case,
        "measured",
        args.measured_rounds,
        args.rows,
        args.cols,
        args.source_compression_level,
        args.output_compression_level,
        args.source_pattern,
    )
    if args.warmup_rounds > 0:
        verify_balanced_positions(warmups)
    verify_balanced_positions(measured)
    verify_round_output_parity(measured[baseline_name], measured[candidate_name])
    profile_reports = [
        summarize_profile(
            profile,
            warmups,
            measured,
            args.verify_openpyxl,
            case,
            args.rows,
            args.cols,
            args.source_pattern,
        )
        for profile in profiles
    ]
    paired_summary = paired_metric_summary(
        measured[baseline_name], measured[candidate_name], baseline_name, candidate_name
    )

    for profile in profile_reports:
        stats = profile["statistics"]
        print(
            f"{profile['name']}: total={stats['total_editor_ms']['median']} ms, "
            f"commit={stats['single_pass_commit_ms']['median']} ms, "
            f"cpu={stats['total_editor_process_cpu_us']['median']} us, "
            f"peak={stats['peak_memory_mb']['median']} MB"
        )
    total_delta = paired_summary["total_editor_ms"]
    print(
        f"{candidate_name} minus {baseline_name} paired total: "
        f"median={total_delta['delta']['median']} ms, "
        f"median_percent={total_delta['percent']['median']}%"
    )

    report = {
        "patch_crc_paired_matrix_schema_version": PAIRED_MATRIX_SCHEMA_VERSION,
        "workbook_editor_benchmark_schema_versions": {
            baseline_name: profiles[0]["schema_version"],
            candidate_name: profiles[1]["schema_version"],
        },
        "comparison_label": args.comparison_label,
        "output_dir": str(output_dir),
        "rows": args.rows,
        "cols": args.cols,
        "source_cells": args.rows * args.cols,
        "requested_edits": args.edits,
        "source_pattern": args.source_pattern,
        "source_compression_level": args.source_compression_level,
        "output_compression_level": args.output_compression_level,
        "source": str(source_path),
        "source_bytes": source_path.stat().st_size,
        "source_archive": source_archive,
        "source_preparation": preparation,
        "warmup_rounds": args.warmup_rounds,
        "measured_rounds": args.measured_rounds,
        "schedule": (
            "Balanced two-profile rotation with reversed cycles; every profile appears "
            "in every position equally often."
        ),
        "paired_delta_direction": (
            f"{candidate_name} minus {baseline_name}; negative wall/CPU values favor "
            f"{candidate_name}"
        ),
        "process_priority": "inherited",
        "profiles": profile_reports,
        "paired_metrics": paired_summary,
        "scope": (
            "Same source package, Patch upsert workload, compression levels and "
            "compatibility checks for the named internal comparison profile. Every measured "
            "round requires identical worksheet logical and compressed payload hashes, CRC, "
            "sizes and package size. Source generation and representative openpyxl checks "
            "are outside timed processes; Office remains not_run."
        ),
    }
    report_path = output_dir / "patch-crc-paired-benchmark-report.json"
    write_report(report_path, report)
    print(f"Wrote {report_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
