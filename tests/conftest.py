from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys

import pytest

from bench.benchmark_matrix import BenchmarkScenario, write_scenario_sources
from models import BenchmarkResult

ROOT_DIR = Path(__file__).parent.parent
BENCH_DIR = ROOT_DIR / 'benchmarks'
OUT_DIR = ROOT_DIR / 'out' / 'benchmarks'


def resolve_command(command: str) -> str:
    if candidate := shutil.which(command):
        return candidate

    if candidate := shutil.which(f'{command}.cmd'):
        return candidate

    return command


def resolve_binary_path(path: Path) -> Path:
    if path.exists():
        return path

    if sys.platform == 'win32':
        exe_path = path.with_suffix('.exe')
        if exe_path.exists():
            return exe_path

    return path


def pytest_report_teststatus(report, config):
    del config
    if report.when == 'call' and report.outcome in {'passed', 'skipped'}:
        return report.outcome, '', ''
    return None


@pytest.fixture(scope='session', autouse=True)
def install_npm():
    if (BENCH_DIR / 'node_modules' / 'rxjs').exists():
        return

    subprocess.run(
        [resolve_command('npm'), 'install'],
        cwd=BENCH_DIR,
        check=True,
        capture_output=True,
    )


@pytest.fixture(scope='session')
def benchmark_workspace() -> Path:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    return OUT_DIR


@pytest.fixture(scope='session')
def pipeline_codegen_binary(benchmark_workspace: Path) -> Path:
    binary = resolve_binary_path(benchmark_workspace / 'pipeline_codegen')
    subprocess.run(
        [
            resolve_command('gcc'),
            '-O2',
            '-I./core/tools',
            '-I./core/src',
            'core/tools/pipeline_codegen.c',
            'core/tools/dsl_codegen.c',
            'core/tools/dsl_lexer.c',
            'core/tools/dsl_parser.c',
            'core/tools/lowering.c',
            'core/tools/operator_registry.c',
            'core/tools/planner.c',
            '-o',
            str(binary),
        ],
        cwd=ROOT_DIR,
        check=True,
        capture_output=True,
    )
    return binary


@pytest.fixture(scope='session')
def scenario_artifacts(benchmark_workspace: Path):
    cache: dict[str, dict[str, Path]] = {}

    def _artifacts(scenario: BenchmarkScenario) -> dict[str, Path]:
        if scenario.name not in cache:
            cache[scenario.name] = write_scenario_sources(
                benchmark_workspace, scenario
            )
        return cache[scenario.name]

    return _artifacts


@pytest.fixture(scope='session')
def run_raw_c(scenario_artifacts):
    cache: dict[str, Path] = {}

    def _run(scenario: BenchmarkScenario) -> BenchmarkResult:
        artifacts = scenario_artifacts(scenario)
        if scenario.name not in cache:
            subprocess.run(
                [
                    resolve_command('gcc'),
                    '-O3',
                    str(artifacts['raw_c']),
                    '-o',
                    str(artifacts['raw_binary']),
                ],
                cwd=ROOT_DIR,
                check=True,
                capture_output=True,
            )
            cache[scenario.name] = artifacts['raw_binary']

        stdout = subprocess.run(
            [str(cache[scenario.name]), str(scenario.n), str(scenario.runs)],
            cwd=ROOT_DIR,
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        return BenchmarkResult.parse_json(stdout)

    return _run


@pytest.fixture(scope='session')
def run_typescript(scenario_artifacts):
    node = resolve_command('node')
    env = dict(os.environ)
    env['NODE_PATH'] = str(BENCH_DIR / 'node_modules')

    def _run(scenario: BenchmarkScenario) -> BenchmarkResult:
        artifacts = scenario_artifacts(scenario)
        stdout = subprocess.run(
            [node, str(artifacts['ts']), str(scenario.n), str(scenario.runs)],
            cwd=ROOT_DIR,
            check=True,
            capture_output=True,
            text=True,
            env=env,
        ).stdout
        return BenchmarkResult.parse_json(stdout)

    return _run


@pytest.fixture(scope='session')
def run_dsl(pipeline_codegen_binary: Path, scenario_artifacts):
    cache: dict[str, Path] = {}

    def _run(scenario: BenchmarkScenario) -> BenchmarkResult:
        artifacts = scenario_artifacts(scenario)
        if scenario.name not in cache:
            subprocess.run(
                [
                    str(pipeline_codegen_binary),
                    '--dsl',
                    str(artifacts['dsl']),
                    '--output',
                    str(artifacts['dsl_c']),
                    '--binary',
                    str(artifacts['dsl_binary']),
                    '--define',
                    f'N={scenario.n}',
                    '--runs',
                    str(scenario.runs),
                    '--compile',
                ],
                cwd=ROOT_DIR,
                check=True,
                capture_output=True,
                text=True,
            )
            cache[scenario.name] = resolve_binary_path(artifacts['dsl_binary'])

        stdout = subprocess.run(
            [str(cache[scenario.name])],
            cwd=ROOT_DIR,
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        return BenchmarkResult.parse_codegen_output(stdout, scenario.n)

    return _run
