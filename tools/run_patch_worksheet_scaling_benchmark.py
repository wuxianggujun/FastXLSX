#!/usr/bin/env python3
"""Run balanced 1/2/4-worksheet FastXLSX Patch scaling profiles."""

from __future__ import annotations

import argparse
import json
import platform
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from run_patch_benchmark_matrix import (
    BENCHMARK_SCHEMA_VERSION,
    PatchCase,
    default_benchmark_exe,
    measured_run_summary,
    metric_summary,
    pre_warmup_observation,
    require,
    run_single_case,
    verify_workbook_with_openpyxl,
    write_report,
)


SCALING_MATRIX_SCHEMA_VERSION = "2"
DEFAULT_WORKSHEET_COUNTS = [1, 2, 4]
SCALING_METRICS = [
    "total_editor_ms",
    "total_editor_process_cpu_us",
    "mutation_ms",
    "mutation_process_cpu_us",
    "save_ms",
    "save_process_cpu_us",
    "single_pass_transform_us",
    "single_pass_crc32_us",
    "package_writer_total_us",
    "package_writer_total_process_cpu_us",
    "rewritten_worksheet_entry_total_us",
    "rewritten_worksheet_entry_total_process_cpu_us",
    "rewritten_worksheet_entry_input_read_us",
    "rewritten_worksheet_entry_input_read_wait_us",
    "rewritten_worksheet_entry_writer_write_us",
    "rewritten_worksheet_entry_writer_write_process_cpu_us",
    "rewritten_worksheet_entry_writer_write_max_us",
    "rewritten_worksheet_entry_close_us",
    "rewritten_worksheet_entry_close_process_cpu_us",
    "peak_memory_mb",
    "output_bytes",
]
ENTRY_METRICS = [
    "uncompressed_bytes",
    "input_bytes",
    "input_read_calls",
    "writer_write_calls",
    "writer_write_input_peak_bytes",
    "writer_write_max_us",
    "reused_staged_file_chunk_count",
    "prefetched_staged_file_chunk_count",
    "prefetched_staged_input_bytes",
    "prefetch_peak_buffer_bytes",
    "total_us",
    "total_process_cpu_us",
    "open_us",
    "input_read_us",
    "input_read_wait_us",
    "writer_write_us",
    "writer_write_process_cpu_us",
    "staged_crc_validation_us",
    "close_us",
    "close_process_cpu_us",
    "deflate_writer_process_cpu_us",
]


@dataclass(frozen=True)
class ScalingVariant:
    worksheets: int
    rows_per_worksheet: int
    cols: int
    edits_per_worksheet: int

    @property
    def name(self) -> str:
        return f"worksheets-{self.worksheets}"

    @property
    def source_cells(self) -> int:
        return self.worksheets * self.rows_per_worksheet * self.cols

    @property
    def requested_edits(self) -> int:
        return self.worksheets * self.edits_per_worksheet


def balanced_variant_order(
    variants: list[ScalingVariant], round_index: int
) -> list[ScalingVariant]:
    require(bool(variants), "scaling variants must not be empty")
    cycle = round_index // len(variants)
    ordered = list(reversed(variants)) if cycle % 2 else list(variants)
    shift = round_index % len(variants)
    return ordered[shift:] + ordered[:shift]


def build_variants(
    mode: str,
    worksheet_counts: list[int],
    cols: int,
    rows_per_worksheet: int,
    edits_per_worksheet: int,
    total_rows: int,
    total_edits: int,
) -> list[ScalingVariant]:
    require(mode in {"fixed-shape", "fixed-total"}, "unsupported scaling mode")
    require(bool(worksheet_counts), "worksheet counts must not be empty")
    require(all(count > 0 for count in worksheet_counts), "worksheet counts must be positive")
    require(
        len(worksheet_counts) == len(set(worksheet_counts)),
        "worksheet counts must be unique",
    )
    require(cols > 0, "--cols must be positive")

    variants: list[ScalingVariant] = []
    for worksheets in worksheet_counts:
        if mode == "fixed-shape":
            rows = rows_per_worksheet
            edits = edits_per_worksheet
        else:
            require(
                total_rows % worksheets == 0,
                f"--total-rows must be divisible by worksheet count {worksheets}",
            )
            require(
                total_edits % worksheets == 0,
                f"--total-edits must be divisible by worksheet count {worksheets}",
            )
            rows = total_rows // worksheets
            edits = total_edits // worksheets

        require(0 < rows <= 1048576, "rows per worksheet out of Excel range")
        require(edits > 0, "edits per worksheet must be positive")
        variants.append(ScalingVariant(worksheets, rows, cols, edits))
    return variants


def validate_variant(variant: ScalingVariant, scenario: str) -> None:
    cells_per_worksheet = variant.rows_per_worksheet * variant.cols
    if scenario == "patch-replace":
        require(
            variant.edits_per_worksheet <= cells_per_worksheet,
            "replace edits per worksheet cannot exceed source cells per worksheet",
        )
        return

    require(scenario == "patch-upsert", "scaling scenario must be Patch replace/upsert")
    existing_edits = variant.edits_per_worksheet // 2
    inserted_edits = variant.edits_per_worksheet - existing_edits
    require(
        existing_edits <= cells_per_worksheet,
        "upsert existing edits per worksheet cannot exceed source cells per worksheet",
    )
    require(
        inserted_edits <= 1048576 - variant.rows_per_worksheet,
        "upsert inserted rows exceed the Excel row limit",
    )


def position_statistics(reports: list[dict[str, Any]]) -> dict[str, Any]:
    positions = sorted({int(report["position"]) for report in reports})
    summary: dict[str, Any] = {}
    for position in positions:
        positioned = [report for report in reports if int(report["position"]) == position]
        metrics: dict[str, Any] = {}
        for metric in SCALING_METRICS:
            if not all(metric in report["result"] for report in positioned):
                continue
            metrics[metric] = metric_summary(
                [report["result"][metric] for report in positioned]
            )
        summary[str(position)] = {
            "sample_count": len(positioned),
            "metrics": metrics,
        }
    return summary


def per_worksheet_entry_statistics(reports: list[dict[str, Any]]) -> list[dict[str, Any]]:
    require(bool(reports), "per-worksheet entry statistics require measured reports")
    expected_names = [
        entry["entry_name"]
        for entry in reports[0]["result"]["rewritten_worksheet_entries"]
    ]
    require(bool(expected_names), "measured reports must contain worksheet entry details")
    entries_by_name: dict[str, list[dict[str, Any]]] = {
        name: [] for name in expected_names
    }
    for report in reports:
        entries = report["result"]["rewritten_worksheet_entries"]
        require(
            [entry["entry_name"] for entry in entries] == expected_names,
            "worksheet entry detail order changed between measured runs",
        )
        for entry in entries:
            entries_by_name[entry["entry_name"]].append(entry)

    summaries: list[dict[str, Any]] = []
    for name in expected_names:
        entries = entries_by_name[name]
        metrics = {
            metric: metric_summary([entry[metric] for entry in entries])
            for metric in ENTRY_METRICS
        }
        summaries.append(
            {
                "entry_name": name,
                "sample_count": len(entries),
                "statistics": metrics,
            }
        )
    return summaries


def run_rounds(
    bench_exe: Path,
    output_dir: Path,
    variants: list[ScalingVariant],
    source_paths: dict[str, Path],
    scenario: str,
    source_compression_level: int,
    output_compression_level: int,
    source_pattern: str,
    run_kind: str,
    round_count: int,
) -> dict[str, list[dict[str, Any]]]:
    runs = {variant.name: [] for variant in variants}
    for round_index in range(round_count):
        order = balanced_variant_order(variants, round_index)
        for position, variant in enumerate(order, start=1):
            name = (
                f"{run_kind}-{variant.name}-round-{round_index + 1:02d}"
                f"-position-{position:02d}"
            )
            report = run_single_case(
                bench_exe,
                output_dir,
                source_paths[variant.name],
                PatchCase(scenario, variant.edits_per_worksheet),
                variant.rows_per_worksheet,
                variant.cols,
                source_compression_level,
                output_compression_level,
                source_pattern,
                False,
                name,
                True,
                worksheets=variant.worksheets,
            )
            report["round"] = round_index + 1
            report["position"] = position
            runs[variant.name].append(report)
    return runs


def relative_metrics(
    statistics_report: dict[str, Any], baseline_statistics: dict[str, Any]
) -> dict[str, float | None]:
    ratios: dict[str, float | None] = {}
    for metric in SCALING_METRICS:
        if metric not in statistics_report or metric not in baseline_statistics:
            continue
        value = statistics_report[metric]["median"]
        baseline = baseline_statistics[metric]["median"]
        ratios[metric] = None if baseline == 0 else round(value / baseline, 6)
    return ratios


def paired_round_ratios(
    reports: list[dict[str, Any]], baseline_reports: list[dict[str, Any]]
) -> dict[str, Any]:
    baseline_by_round = {int(report["round"]): report for report in baseline_reports}
    require(
        len(baseline_by_round) == len(baseline_reports),
        "baseline measured rounds must be unique",
    )
    paired: dict[str, Any] = {}
    for metric in SCALING_METRICS:
        observations: list[dict[str, int | float | None]] = []
        numeric_ratios: list[float] = []
        for report in reports:
            round_index = int(report["round"])
            baseline = baseline_by_round.get(round_index)
            require(baseline is not None, "measured round missing from baseline")
            if metric not in report["result"] or metric not in baseline["result"]:
                continue
            value = report["result"][metric]
            baseline_value = baseline["result"][metric]
            ratio = None if baseline_value == 0 else round(value / baseline_value, 6)
            observations.append(
                {
                    "round": round_index,
                    "position": int(report["position"]),
                    "baseline_position": int(baseline["position"]),
                    "ratio": ratio,
                }
            )
            if ratio is not None:
                numeric_ratios.append(ratio)
        if observations:
            paired[metric] = {
                "statistics": metric_summary(numeric_ratios) if numeric_ratios else None,
                "observations": observations,
            }
    return paired


def normalized_metrics(
    variant: ScalingVariant, statistics_report: dict[str, Any]
) -> dict[str, float]:
    million_cells = variant.source_cells / 1_000_000.0
    return {
        "total_editor_ms_per_million_source_cells": round(
            statistics_report["total_editor_ms"]["median"] / million_cells, 6
        ),
        "total_editor_process_cpu_ms_per_million_source_cells": round(
            statistics_report["total_editor_process_cpu_us"]["median"]
            / 1000.0
            / million_cells,
            6,
        ),
        "mutation_ms_per_million_source_cells": round(
            statistics_report["mutation_ms"]["median"] / million_cells, 6
        ),
        "save_ms_per_million_source_cells": round(
            statistics_report["save_ms"]["median"] / million_cells, 6
        ),
        "peak_memory_mb_per_million_source_cells": round(
            statistics_report["peak_memory_mb"]["median"] / million_cells, 6
        ),
    }


def prepare_sources(
    bench_exe: Path,
    output_dir: Path,
    variants: list[ScalingVariant],
    source_compression_level: int,
    output_compression_level: int,
    source_pattern: str,
) -> tuple[dict[str, Path], dict[str, dict[str, Any]]]:
    source_paths: dict[str, Path] = {}
    preparations: dict[str, dict[str, Any]] = {}
    for variant in variants:
        source_path = output_dir / f"scaling-source-{variant.name}.xlsx"
        preparation = run_single_case(
            bench_exe,
            output_dir,
            source_path,
            PatchCase("noop-copy", 0),
            variant.rows_per_worksheet,
            variant.cols,
            source_compression_level,
            output_compression_level,
            source_pattern,
            False,
            f"source-preparation-{variant.name}",
            False,
            worksheets=variant.worksheets,
        )
        source_paths[variant.name] = source_path
        preparations[variant.name] = preparation
    return source_paths, preparations


def summarize_variants(
    variants: list[ScalingVariant],
    scenario: str,
    source_pattern: str,
    warmups: dict[str, list[dict[str, Any]]],
    measured: dict[str, list[dict[str, Any]]],
    verify_openpyxl: bool,
) -> list[dict[str, Any]]:
    summaries: list[dict[str, Any]] = []
    statistics_by_name: dict[str, dict[str, Any]] = {}
    representatives: dict[str, tuple[int, dict[str, Any]]] = {}

    for variant in variants:
        representative_index, statistics_report = measured_run_summary(
            measured[variant.name]
        )
        statistics_by_name[variant.name] = statistics_report
        representatives[variant.name] = (
            representative_index,
            measured[variant.name][representative_index],
        )

    baseline = variants[0]
    baseline_statistics = statistics_by_name[baseline.name]
    for variant in variants:
        representative_index, representative = representatives[variant.name]
        openpyxl_report = (
            verify_workbook_with_openpyxl(
                Path(representative["output"]),
                PatchCase(scenario, variant.edits_per_worksheet),
                variant.rows_per_worksheet,
                variant.cols,
                source_pattern,
                False,
                variant.worksheets,
            )
            if verify_openpyxl
            else {"status": "not_requested"}
        )
        statistics_report = statistics_by_name[variant.name]
        summaries.append(
            {
                "name": variant.name,
                "worksheets": variant.worksheets,
                "rows_per_worksheet": variant.rows_per_worksheet,
                "cols": variant.cols,
                "source_cells": variant.source_cells,
                "edits_per_worksheet": variant.edits_per_worksheet,
                "requested_edits": variant.requested_edits,
                "representative_run": representative_index + 1,
                "statistics": statistics_report,
                "relative_to_baseline_median": relative_metrics(
                    statistics_report, baseline_statistics
                ),
                "paired_round_ratios_to_baseline": paired_round_ratios(
                    measured[variant.name], measured[baseline.name]
                ),
                "normalized_medians": normalized_metrics(variant, statistics_report),
                "position_statistics": position_statistics(measured[variant.name]),
                "per_worksheet_entry_statistics": per_worksheet_entry_statistics(
                    measured[variant.name]
                ),
                "pre_warmup_observation": pre_warmup_observation(
                    warmups[variant.name], statistics_report, "total_editor_ms"
                ),
                "result": representative["result"],
                "byte_accounting": representative["byte_accounting"],
                "output_archive": representative["output_archive"],
                "openpyxl": openpyxl_report,
                "warmups": warmups[variant.name],
                "runs": measured[variant.name],
            }
        )
    return summaries


def validate_round_count(round_count: int, variants: int, label: str) -> None:
    require(round_count >= 0, f"{label} rounds must be non-negative")
    if round_count == 0:
        return
    require(
        round_count % (2 * variants) == 0,
        f"{label} rounds must be zero or a multiple of twice the variant count",
    )


def run_self_test() -> None:
    variants = build_variants("fixed-shape", [1, 2, 4], 10, 1000, 100, 0, 0)
    require([variant.source_cells for variant in variants] == [10000, 20000, 40000],
            "fixed-shape source cell scaling mismatch")
    require([variant.requested_edits for variant in variants] == [100, 200, 400],
            "fixed-shape edit scaling mismatch")

    fixed_total = build_variants("fixed-total", [1, 2, 4], 10, 0, 0, 4000, 400)
    require([variant.rows_per_worksheet for variant in fixed_total] == [4000, 2000, 1000],
            "fixed-total row partition mismatch")
    require([variant.source_cells for variant in fixed_total] == [40000] * 3,
            "fixed-total source cells mismatch")
    require([variant.requested_edits for variant in fixed_total] == [400] * 3,
            "fixed-total edits mismatch")

    positions = {variant.name: [] for variant in variants}
    for round_index in range(2 * len(variants)):
        order = balanced_variant_order(variants, round_index)
        require(len({variant.name for variant in order}) == len(variants),
                "variant duplication in balanced schedule")
        for position, variant in enumerate(order, start=1):
            positions[variant.name].append(position)
    expected_positions = list(range(1, len(variants) + 1)) * 2
    for values in positions.values():
        require(sorted(values) == sorted(expected_positions),
                "balanced variant position coverage mismatch")

    mock_reports = [
        {
            "position": position,
            "result": {
                "total_editor_ms": value,
                "total_editor_process_cpu_us": value * 1000,
            },
        }
        for position, value in [(1, 10), (2, 20), (1, 30), (2, 40)]
    ]
    positions_report = position_statistics(mock_reports)
    require(positions_report["1"]["sample_count"] == 2,
            "position sample count mismatch")
    require(positions_report["1"]["metrics"]["total_editor_ms"]["median"] == 20,
            "position median mismatch")
    ratio_reports = [
        {"round": 1, "position": 2, "result": {"total_editor_ms": 20}},
        {"round": 2, "position": 1, "result": {"total_editor_ms": 45}},
    ]
    ratio_baseline = [
        {"round": 1, "position": 1, "result": {"total_editor_ms": 10}},
        {"round": 2, "position": 2, "result": {"total_editor_ms": 30}},
    ]
    require(
        paired_round_ratios(ratio_reports, ratio_baseline)["total_editor_ms"]
        ["statistics"]["median"]
        == 1.75,
        "paired round ratio mismatch",
    )
    entry_reports = [
        {
            "result": {
                "rewritten_worksheet_entries": [
                    {"entry_name": "xl/worksheets/sheet1.xml", **{
                        metric: index + 1 for metric in ENTRY_METRICS
                    }}
                ]
            }
        }
        for index in range(2)
    ]
    entry_statistics = per_worksheet_entry_statistics(entry_reports)
    require(entry_statistics[0]["sample_count"] == 2,
            "per-worksheet entry sample count mismatch")
    require(entry_statistics[0]["statistics"]["total_us"]["median"] == 1.5,
            "per-worksheet entry median mismatch")
    validate_round_count(6, 3, "measured")
    print("OK: run_patch_worksheet_scaling_benchmark.py self-test passed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bench-exe", type=Path, default=default_benchmark_exe())
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("build/qa/patch-worksheet-scaling-benchmark"),
    )
    parser.add_argument(
        "--mode", choices=["fixed-shape", "fixed-total"], default="fixed-shape"
    )
    parser.add_argument("--worksheet-count", action="append", type=int)
    parser.add_argument("--cols", type=int, default=10)
    parser.add_argument("--rows-per-worksheet", type=int, default=100000)
    parser.add_argument("--edits-per-worksheet", type=int, default=1000)
    parser.add_argument("--total-rows", type=int, default=400000)
    parser.add_argument("--total-edits", type=int, default=4000)
    parser.add_argument(
        "--scenario", choices=["patch-replace", "patch-upsert"], default="patch-upsert"
    )
    parser.add_argument(
        "--source-pattern",
        choices=["numeric", "mixed-inline", "mixed-shared", "formula"],
        default="numeric",
    )
    parser.add_argument("--source-compression-level", type=int, default=6)
    parser.add_argument("--output-compression-level", type=int, default=1)
    parser.add_argument("--warmup-rounds", type=int, default=6)
    parser.add_argument("--measured-rounds", type=int, default=12)
    parser.add_argument("--verify-openpyxl", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        run_self_test()
        return 0

    worksheet_counts = args.worksheet_count or DEFAULT_WORKSHEET_COUNTS
    variants = build_variants(
        args.mode,
        worksheet_counts,
        args.cols,
        args.rows_per_worksheet,
        args.edits_per_worksheet,
        args.total_rows,
        args.total_edits,
    )
    require(len(variants) >= 2, "scaling profile requires at least two variants")
    for variant in variants:
        validate_variant(variant, args.scenario)
    require(0 <= args.source_compression_level <= 9,
            "source compression level must be 0..9")
    require(-1 <= args.output_compression_level <= 9,
            "output compression level must be -1..9")
    validate_round_count(args.warmup_rounds, len(variants), "warmup")
    require(args.measured_rounds > 0, "measured rounds must be positive")
    validate_round_count(args.measured_rounds, len(variants), "measured")

    bench_exe = args.bench_exe.resolve()
    require(bench_exe.is_file(), f"benchmark executable not found: {bench_exe}")
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    source_paths, preparations = prepare_sources(
        bench_exe,
        output_dir,
        variants,
        args.source_compression_level,
        args.output_compression_level,
        args.source_pattern,
    )
    warmups = run_rounds(
        bench_exe,
        output_dir,
        variants,
        source_paths,
        args.scenario,
        args.source_compression_level,
        args.output_compression_level,
        args.source_pattern,
        "warmup",
        args.warmup_rounds,
    )
    measured = run_rounds(
        bench_exe,
        output_dir,
        variants,
        source_paths,
        args.scenario,
        args.source_compression_level,
        args.output_compression_level,
        args.source_pattern,
        "measured",
        args.measured_rounds,
    )
    variant_reports = summarize_variants(
        variants,
        args.scenario,
        args.source_pattern,
        warmups,
        measured,
        args.verify_openpyxl,
    )

    baseline = variants[0]
    for variant_report in variant_reports:
        statistics_report = variant_report["statistics"]
        print(
            f"{variant_report['name']}: cells={variant_report['source_cells']}, "
            f"total={statistics_report['total_editor_ms']['median']} ms, "
            f"cpu={statistics_report['total_editor_process_cpu_us']['median']} us, "
            f"mutation={statistics_report['mutation_ms']['median']} ms, "
            f"save={statistics_report['save_ms']['median']} ms, "
            f"peak={statistics_report['peak_memory_mb']['median']} MB"
        )

    report = {
        "patch_worksheet_scaling_matrix_schema_version": SCALING_MATRIX_SCHEMA_VERSION,
        "workbook_editor_benchmark_schema_version": BENCHMARK_SCHEMA_VERSION,
        "benchmark_executable": str(bench_exe),
        "output_dir": str(output_dir),
        "mode": args.mode,
        "scenario": args.scenario,
        "source_pattern": args.source_pattern,
        "source_compression_level": args.source_compression_level,
        "output_compression_level": args.output_compression_level,
        "worksheet_counts": [variant.worksheets for variant in variants],
        "baseline_variant": baseline.name,
        "warmup_rounds": args.warmup_rounds,
        "measured_rounds": args.measured_rounds,
        "schedule": (
            "Balanced round-robin rotation with reversed cycles; every non-zero warm-up "
            "and measured sequence places every variant in every position equally often."
        ),
        "representative_result_policy": (
            "Measured run nearest total_editor_ms median; earliest run breaks ties."
        ),
        "process_priority": "inherited",
        "host_platform": platform.platform(),
        "shape_interpretation": (
            "Each worksheet keeps the same rows, columns and edits; total cells and edits "
            "scale with worksheet count."
            if args.mode == "fixed-shape"
            else "Total rows, columns-per-row and edits are fixed, then divided evenly "
            "across worksheets. Different row-number widths and per-sheet XML/package-part "
            "sizes mean this isolates partition/fixed-entry cost, not byte-identical input."
        ),
        "source_preparations": preparations,
        "variants": variant_reports,
        "scope": (
            "Sequential public WorkbookEditor Patch processing with independently prepared "
            "source workbooks. Source preparation, report serialization and representative "
            "OpenPyXL validation are outside timed benchmark processes. Results are local "
            "profiling observations and are not tracked release evidence."
        ),
    }
    report_path = output_dir / "patch-worksheet-scaling-benchmark-report.json"
    write_report(report_path, report)
    print(f"Wrote {report_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
