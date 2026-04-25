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
ALL_BENCHMARK_BACKENDS = (
    'raw_c',
    'library_c',
    'planner_c',
    'planner_graph_c',
    'dsl_c',
    'typescript',
)
C_BENCHMARK_BACKENDS = ('raw_c', 'library_c', 'planner_c', 'planner_graph_c', 'dsl_c')


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


def pytest_addoption(parser):
    parser.addoption(
        '--benchmark-backends',
        action='store',
        default=','.join(ALL_BENCHMARK_BACKENDS),
        help=(
            'Comma-separated benchmark backends to run. '
            f'Available: {", ".join(ALL_BENCHMARK_BACKENDS)}'
        ),
    )
    parser.addoption(
        '--c-only',
        action='store_true',
        help='Run only C backends (raw_c, library_c, planner_c, planner_graph_c, dsl_c) and skip TypeScript.',
    )


@pytest.fixture(scope='session')
def selected_backends(pytestconfig) -> set[str]:
    if pytestconfig.getoption('--c-only'):
        return set(C_BENCHMARK_BACKENDS)

    raw_value = str(pytestconfig.getoption('--benchmark-backends'))
    selected = {value.strip() for value in raw_value.split(',') if value.strip()}
    unknown = selected - set(ALL_BENCHMARK_BACKENDS)
    if unknown:
        raise pytest.UsageError(
            f'Unknown benchmark backends: {", ".join(sorted(unknown))}'
        )
    return selected


@pytest.fixture(scope='session', autouse=True)
def install_npm(selected_backends: set[str]):
    if 'typescript' not in selected_backends:
        return

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
def planner_codegen_binary(benchmark_workspace: Path) -> Path:
    binary = resolve_binary_path(benchmark_workspace / 'planner_codegen')
    subprocess.run(
        [
            resolve_command('gcc'),
            '-O2',
            '-I./core/planner',
            'core/planner/main.c',
            'core/planner/diagnostics.c',
            'core/planner/string_builder.c',
            'core/planner/function_registry.c',
            'core/planner/simplify.c',
            'core/planner/lower.c',
            'core/planner/graph_opt.c',
            'core/planner/planner_ir.c',
            'core/planner/astgen.c',
            'core/planner/c_model.c',
            'core/planner/c_render.c',
            'core/planner/transpiler.c',
            'core/planner/compiled_segment.c',
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
def run_library_c(scenario_artifacts):
    cache: dict[str, Path] = {}

    def _run(scenario: BenchmarkScenario) -> BenchmarkResult:
        artifacts = scenario_artifacts(scenario)
        if scenario.name not in cache:
            subprocess.run(
                [
                    resolve_command('gcc'),
                    '-O3',
                    '-I./core/src',
                    str(artifacts['library_c']),
                    'src/observable.c',
                    'core/src/list.c',
                    'core/src/task.c',
                    'core/src/stopwatch.c',
                    'core/src/profiler.c',
                    '-o',
                    str(artifacts['library_binary']),
                    '-lpthread',
                ],
                cwd=ROOT_DIR,
                check=True,
                capture_output=True,
            )
            cache[scenario.name] = artifacts['library_binary']

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
def run_planner_c(planner_codegen_binary: Path, scenario_artifacts):
    cache: dict[str, Path] = {}

    def _run(scenario: BenchmarkScenario) -> BenchmarkResult | None:
        artifacts = scenario_artifacts(scenario)
        if not artifacts['planner_spec'].exists():
            return None

        if scenario.name not in cache:
            subprocess.run(
                [
                    str(planner_codegen_binary),
                    '--spec',
                    str(artifacts['planner_spec']),
                    '--output',
                    str(artifacts['planner_c']),
                    '--header',
                    artifacts['planner_helper_h'].name,
                    '--helpers-source',
                    artifacts['planner_helper_c'].name,
                ],
                cwd=artifacts['dir'],
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run(
                [
                    resolve_command('gcc'),
                    '-O3',
                    '-I.',
                    str(artifacts['planner_c']),
                    str(artifacts['planner_helper_c']),
                    '-o',
                    str(artifacts['planner_binary']),
                ],
                cwd=artifacts['dir'],
                check=True,
                capture_output=True,
            )
            cache[scenario.name] = artifacts['planner_binary']

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
def run_planner_graph_c(planner_codegen_binary: Path, scenario_artifacts):
    cache: dict[str, Path] = {}

    def _run(scenario: BenchmarkScenario) -> BenchmarkResult | None:
        artifacts = scenario_artifacts(scenario)
        if not artifacts['planner_spec'].exists():
            return None

        if scenario.name not in cache:
            planner_graph_c = artifacts['dir'] / f'{scenario.name}_planner_graph_generated.c'
            planner_graph_binary = artifacts['dir'] / f'{scenario.name}_planner_graph'
            subprocess.run(
                [
                    str(planner_codegen_binary),
                    '--spec',
                    str(artifacts['planner_spec']),
                    '--output',
                    str(planner_graph_c),
                    '--header',
                    artifacts['planner_helper_h'].name,
                    '--helpers-source',
                    artifacts['planner_helper_c'].name,
                    '--graph-opt',
                ],
                cwd=artifacts['dir'],
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run(
                [
                    resolve_command('gcc'),
                    '-O3',
                    '-I.',
                    str(planner_graph_c),
                    str(artifacts['planner_helper_c']),
                    '-o',
                    str(planner_graph_binary),
                ],
                cwd=artifacts['dir'],
                check=True,
                capture_output=True,
            )
            cache[scenario.name] = resolve_binary_path(planner_graph_binary)

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
