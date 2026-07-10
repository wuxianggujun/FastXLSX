#!/usr/bin/env python3
"""Validate tracked FastXLSX benchmark evidence bundles using the standard library."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import tempfile
from datetime import datetime
from pathlib import Path, PurePosixPath

SCHEMA_VERSION = "1"
KINDS = {
    "streaming-writer",
    "workbook-editor",
    "package-editor-cell-replacement",
    "reference-writer",
}
ROLES = {"result", "matrix-report", "summary", "office-report", "workbook"}
HEX_REVISION = re.compile(r"^[0-9a-fA-F]{7,40}$")
SHA256 = re.compile(r"^[0-9a-f]{64}$")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_utc(value: object) -> None:
    require(isinstance(value, str) and value.endswith("Z"), "created_utc must end with Z")
    datetime.fromisoformat(value[:-1] + "+00:00")


def safe_relative_path(value: object) -> PurePosixPath:
    require(isinstance(value, str) and value, "artifact path must be a non-empty string")
    path = PurePosixPath(value)
    require(not path.is_absolute(), f"artifact path must be relative: {value}")
    require(".." not in path.parts, f"artifact path must not escape bundle: {value}")
    return path


def validate_manifest(manifest_path: Path) -> None:
    data = json.loads(manifest_path.read_text(encoding="utf-8"))
    required = {
        "evidence_schema_version",
        "evidence_id",
        "created_utc",
        "git_revision",
        "benchmark_kind",
        "environment",
        "artifacts",
        "claims",
    }
    require(set(data) == required, f"{manifest_path}: unexpected or missing top-level fields")
    require(data["evidence_schema_version"] == SCHEMA_VERSION, "unsupported evidence schema")
    require(isinstance(data["evidence_id"], str) and data["evidence_id"], "invalid evidence_id")
    parse_utc(data["created_utc"])
    require(isinstance(data["git_revision"], str) and HEX_REVISION.fullmatch(data["git_revision"]),
            "invalid git_revision")
    require(data["benchmark_kind"] in KINDS, "invalid benchmark_kind")

    environment = data["environment"]
    environment_fields = {"os", "compiler", "cmake_preset", "cpu", "memory_bytes"}
    require(isinstance(environment, dict) and set(environment) == environment_fields,
            "invalid environment fields")
    for field in ("os", "compiler", "cmake_preset", "cpu"):
        require(isinstance(environment[field], str) and environment[field], f"invalid {field}")
    require(isinstance(environment["memory_bytes"], int) and environment["memory_bytes"] > 0,
            "invalid memory_bytes")

    artifacts = data["artifacts"]
    require(isinstance(artifacts, list) and artifacts, "artifacts must be non-empty")
    artifact_paths: set[str] = set()
    for artifact in artifacts:
        require(isinstance(artifact, dict) and set(artifact) == {"path", "sha256", "role"},
                "invalid artifact fields")
        relative = safe_relative_path(artifact["path"])
        relative_text = relative.as_posix()
        require(relative_text not in artifact_paths, f"duplicate artifact path: {relative_text}")
        artifact_paths.add(relative_text)
        require(artifact["role"] in ROLES, f"invalid artifact role: {artifact['role']}")
        require(isinstance(artifact["sha256"], str) and SHA256.fullmatch(artifact["sha256"]),
                f"invalid sha256 for {relative_text}")
        artifact_path = manifest_path.parent.joinpath(*relative.parts)
        require(artifact_path.is_file(), f"missing artifact: {relative_text}")
        require(sha256(artifact_path) == artifact["sha256"], f"sha256 mismatch: {relative_text}")

    claims = data["claims"]
    require(isinstance(claims, list), "claims must be an array")
    for claim in claims:
        require(isinstance(claim, dict) and set(claim) == {"statement", "artifact_paths"},
                "invalid claim fields")
        require(isinstance(claim["statement"], str) and claim["statement"], "invalid claim statement")
        paths = claim["artifact_paths"]
        require(isinstance(paths, list) and paths, "claim artifact_paths must be non-empty")
        for path in paths:
            relative = safe_relative_path(path).as_posix()
            require(relative in artifact_paths, f"claim references undeclared artifact: {relative}")


def validate_root(root: Path) -> int:
    manifests = sorted(root.glob("*/manifest.json"))
    for manifest in manifests:
        validate_manifest(manifest)
    return len(manifests)


def self_test() -> None:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        bundle = root / "sample"
        bundle.mkdir()
        result = bundle / "result.json"
        result.write_text('{"benchmark_schema_version":"4"}\n', encoding="utf-8")
        manifest = {
            "evidence_schema_version": "1",
            "evidence_id": "sample",
            "created_utc": "2026-01-01T00:00:00Z",
            "git_revision": "0123456789abcdef",
            "benchmark_kind": "streaming-writer",
            "environment": {
                "os": "test",
                "compiler": "test",
                "cmake_preset": "test",
                "cpu": "test",
                "memory_bytes": 1,
            },
            "artifacts": [{"path": "result.json", "sha256": sha256(result), "role": "result"}],
            "claims": [{"statement": "self-test", "artifact_paths": ["result.json"]}],
        }
        manifest_path = bundle / "manifest.json"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        validate_manifest(manifest_path)
        manifest["artifacts"][0]["sha256"] = "0" * 64
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        try:
            validate_manifest(manifest_path)
        except ValueError:
            return
        raise ValueError("self-test expected a hash mismatch")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path("benchmarks/evidence"))
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        print("OK: benchmark evidence validator self-test passed")
        return 0
    count = validate_root(args.root)
    print(f"OK: validated {count} benchmark evidence bundle(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())