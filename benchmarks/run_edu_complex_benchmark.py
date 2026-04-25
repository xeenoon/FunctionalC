from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

from edu_complex_data import (
    DEFAULT_MAX_RECORDS,
    HEADER_STRUCT,
    RECORD_STRUCT,
    REQUESTED_SIZES,
    expected_digest_for,
    generate_dataset,
)


ROOT = Path(__file__).resolve().parent.parent
BENCH_DIR = ROOT / "benchmarks"
DATA_DIR = BENCH_DIR / "data"


def per_run_binary(name: str) -> Path:
    return BENCH_DIR / f"{name}_{os.getpid()}.exe"


def expected_dataset_size(record_count: int) -> int:
    return HEADER_STRUCT.size + record_count * RECORD_STRUCT.size


def dataset_is_complete(path: Path, record_count: int) -> bool:
    return path.exists() and path.stat().st_size == expected_dataset_size(record_count)


def resolve_command(command: str) -> str:
    if candidate := shutil.which(command):
        return candidate
    if candidate := shutil.which(f"{command}.cmd"):
        return candidate
    return command


def compile_c(out_path: Path) -> None:
    subprocess.run(
        [
            resolve_command("gcc"),
            "-O3",
            "-I./core/src",
            "benchmarks/edu_complex_benchmark.c",
            "core/src/observable.c",
            "core/src/list.c",
            "core/src/task.c",
            "core/src/stopwatch.c",
            "core/src/profiler.c",
            "-o",
            str(out_path),
            "-lpthread",
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
    )


def compile_manual_c(out_path: Path) -> None:
    subprocess.run(
        [
            resolve_command("gcc"),
            "-O3",
            "-I./core/src",
            "benchmarks/edu_complex_benchmark_manual.c",
            "core/src/observable.c",
            "core/src/list.c",
            "core/src/task.c",
            "core/src/stopwatch.c",
            "core/src/profiler.c",
            "-o",
            str(out_path),
            "-lpthread",
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
    )


def compile_planner_codegen(out_path: Path) -> None:
    subprocess.run(
        [
            resolve_command("gcc"),
            "-O2",
            "-I./core/planner",
            "core/planner/main.c",
            "core/planner/diagnostics.c",
            "core/planner/string_builder.c",
            "core/planner/function_registry.c",
            "core/planner/simplify.c",
            "core/planner/lower.c",
            "core/planner/graph_opt.c",
            "core/planner/planner_ir.c",
            "core/planner/astgen.c",
            "core/planner/c_model.c",
            "core/planner/c_render.c",
            "core/planner/transpiler.c",
            "core/planner/compiled_segment.c",
            "-o",
            str(out_path),
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
    )


def compile_planner_c(planner_codegen: Path, out_path: Path) -> None:
    generated_c = BENCH_DIR / "edu_complex_planner_generated.c"
    subprocess.run(
        [
            str(planner_codegen),
            "--spec",
            str(BENCH_DIR / "edu_complex_planner.spec"),
            "--output",
            str(generated_c),
            "--header",
            "edu_complex_planner_helpers.h",
            "--helpers-source",
            str(BENCH_DIR / "edu_complex_benchmark.c"),
            "--no-main",
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
    )
    subprocess.run(
        [
            resolve_command("gcc"),
            "-O3",
            "-I./core/src",
            "-I./benchmarks",
            '-DEDU_COMPLEX_PLANNER_GENERATED="edu_complex_planner_generated.c"',
            "benchmarks/edu_complex_benchmark_planner.c",
            "core/src/observable.c",
            "core/src/list.c",
            "core/src/task.c",
            "core/src/stopwatch.c",
            "core/src/profiler.c",
            "-o",
            str(out_path),
            "-lpthread",
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
    )


def parse_result(output: str) -> dict[str, int | float]:
    return json.loads(output.strip())


def validate_result(label: str, result: dict[str, int | float], expected) -> None:
    if int(result["digest"]) != expected.digest:
        raise ValueError(f"{label} digest mismatch: {result['digest']} != {expected.digest}")
    if int(result["emitted_snapshots"]) != expected.emitted_snapshots:
        raise ValueError(
            f"{label} emitted mismatch: {result['emitted_snapshots']} != {expected.emitted_snapshots}"
        )
    if int(result["total_enrollment"]) != expected.total_enrollment:
        raise ValueError(
            f"{label} enrollment mismatch: {result['total_enrollment']} != {expected.total_enrollment}"
        )
    if int(result["total_risk"]) != expected.total_risk:
        raise ValueError(f"{label} risk mismatch: {result['total_risk']} != {expected.total_risk}")
    if int(result["max_risk_tier"]) != expected.max_risk_tier:
        raise ValueError(
            f"{label} tier mismatch: {result['max_risk_tier']} != {expected.max_risk_tier}"
        )
    if int(result["last_school_id"]) != expected.last_school_id:
        raise ValueError(
            f"{label} last school mismatch: {result['last_school_id']} != {expected.last_school_id}"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--sizes",
        type=int,
        nargs="*",
        default=REQUESTED_SIZES,
        help="Record counts to benchmark sequentially",
    )
    parser.add_argument(
        "--runs",
        type=int,
        default=3,
        help="Benchmark runs per implementation",
    )
    parser.add_argument(
        "--max-records",
        type=int,
        default=DEFAULT_MAX_RECORDS,
        help="Safety cap for materialized datasets; larger sizes are skipped",
    )
    parser.add_argument(
        "--regen",
        action="store_true",
        help="Regenerate datasets even if they already exist",
    )
    parser.add_argument(
        "--ts-max-records",
        type=int,
        default=100000,
        help="Skip the TypeScript benchmark above this materialized size",
    )
    parser.add_argument(
        "--skip-validation",
        action="store_true",
        help="Skip the Python expected-digest computation and result validation",
    )
    parser.add_argument(
        "--c-impl",
        choices=("library", "planner", "manual"),
        default="library",
        help="C implementation to benchmark against TypeScript",
    )
    args = parser.parse_args()

    c_binary = per_run_binary(
        "edu_complex_benchmark_planner"
        if args.c_impl == "planner"
        else "edu_complex_benchmark_manual"
        if args.c_impl == "manual"
        else "edu_complex_benchmark"
    )
    if args.c_impl == "planner":
        planner_codegen = per_run_binary("planner_codegen")
        compile_planner_codegen(planner_codegen)
        compile_planner_c(planner_codegen, c_binary)
    elif args.c_impl == "manual":
        compile_manual_c(c_binary)
    else:
        compile_c(c_binary)

    for size in args.sizes:
        print(f"\nsize={size}", flush=True)
        if size > args.max_records:
            if args.c_impl == "planner":
                print(
                    f"  planner external-buffer mode requires a materialized dataset; skipping size>{args.max_records}",
                    flush=True,
                )
                continue
            print(
                f"  using synthetic C stream: size exceeds --max-records={args.max_records}; skipping materialized dataset",
                flush=True,
            )
            print("  running C benchmark...", flush=True)
            c_output = subprocess.run(
                [str(c_binary), "--synthetic", str(size), str(args.runs)],
                cwd=ROOT,
                check=True,
                stdout=subprocess.PIPE,
                text=True,
            ).stdout
            c_result = parse_result(c_output)
            print(
                f"  C synthetic ok: avg_ms={c_result['average_ms']:.5f} digest={c_result['digest']} snapshots={c_result['emitted_snapshots']}",
                flush=True,
            )
            print("  skipped TypeScript and Python expected digest for oversized synthetic run", flush=True)
            continue

        data_path = DATA_DIR / f"edu_complex_{size}.bin"
        if args.regen or not dataset_is_complete(data_path, size):
            print("  generating dataset...", flush=True)
            if data_path.exists():
                print(
                    f"  existing dataset is incomplete or stale: size={data_path.stat().st_size} expected={expected_dataset_size(size)}",
                    flush=True,
                )
            generate_dataset(data_path, size)
        else:
            print("  reusing dataset...", flush=True)

        expected = None
        if args.skip_validation:
            print("  skipped expected digest validation", flush=True)
        else:
            print("  computing expected digest...", flush=True)
            expected = expected_digest_for(data_path)

        print("  running C benchmark...", flush=True)
        c_output = subprocess.run(
            [str(c_binary), str(data_path), str(args.runs)],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        c_result = parse_result(c_output)
        if expected is not None:
            validate_result("C", c_result, expected)
        print(
            f"  C ({args.c_impl}) ok: avg_ms={c_result['average_ms']:.5f} digest={c_result['digest']} snapshots={c_result['emitted_snapshots']}",
            flush=True,
        )

        if size > args.ts_max_records:
            print(
                f"  skipped TypeScript benchmark: size exceeds --ts-max-records={args.ts_max_records}",
                flush=True,
            )
        else:
            print("  running TypeScript benchmark...", flush=True)
            ts_output = subprocess.run(
                ["node", str(BENCH_DIR / "edu_complex_benchmark.ts"), str(data_path), str(args.runs)],
                cwd=ROOT,
                check=True,
                capture_output=True,
                text=True,
            ).stdout
            ts_result = parse_result(ts_output)
            if expected is not None:
                validate_result("TypeScript", ts_result, expected)
            speedup = float(ts_result["average_ms"]) / float(c_result["average_ms"]) if float(c_result["average_ms"]) > 0 else 0.0
            print(
                f"  TS ok: avg_ms={ts_result['average_ms']:.5f} digest={ts_result['digest']} speedup={speedup:.2f}x",
                flush=True,
            )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
