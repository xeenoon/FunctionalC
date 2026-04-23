from pathlib import Path
import shutil
import subprocess
import sys

import pytest
from models import BenchmarkResult, Scenario

BENCH_DIR = Path(__file__).parent.parent / 'benchmarks'


def resolve_command(command: str) -> str:
    if candidate := shutil.which(command):
        return candidate

    # Windows tools like npm are often exposed as .cmd files rather than .exe.
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


def make_target_name(scenario: Scenario, opt: str) -> str:
    target = f'{scenario}_{opt}'
    if sys.platform == 'win32':
        return f'{target}.exe'
    return target


# ------------ Cleaning up generated binaries after tests are done ----------- #
def cleanup_optimized_binaries():
    for pattern in ('*_O1', '*_O2', '*_O3'):
        for binary in BENCH_DIR.glob(pattern):
            if binary.is_file():
                binary.unlink()


@pytest.fixture(scope='session', autouse=True)
def cleanup_generated_binaries():
    yield
    cleanup_optimized_binaries()


# ------------------- Installing npm before testing session ------------------ #
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


# ----------------------- Function for running js files ---------------------- #
@pytest.fixture(scope='session')
def run_js():
    def _run(scenario: Scenario, n=1_000_000, runs=5):
        stdout = subprocess.run(
            ['node', f'{scenario}.js', str(n), str(runs)],
            cwd=BENCH_DIR,
            capture_output=True,
            text=True,
            check=True,
        ).stdout
        return BenchmarkResult.parse(stdout)

    return _run


# -------------------- Lazy compiler function for C files -------------------- #
@pytest.fixture(scope='session')
def compile_c():
    _cache = {}

    def _compile(scenario: Scenario, opt='O2'):
        key = (scenario, opt)
        if key not in _cache:
            subprocess.run(
                [resolve_command('make'), make_target_name(scenario, opt)],
                cwd=BENCH_DIR,
                check=True,
                capture_output=True,
            )
            _cache[key] = resolve_binary_path(BENCH_DIR / f'{scenario}_{opt}')
        return _cache[key]

    return _compile


# ------------------------------ Running C files ----------------------------- #
@pytest.fixture(scope='session')
def run_c(compile_c):
    def _run(scenario: Scenario, opt='O3', n=1_000_000, runs=5):
        binary = compile_c(scenario, opt)
        stdout = subprocess.run(
            [str(binary), str(n), str(runs)],
            capture_output=True,
            text=True,
            check=True,
        ).stdout
        return BenchmarkResult.parse(stdout)

    return _run
