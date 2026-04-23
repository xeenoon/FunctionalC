import subprocess
import pytest
from pathlib import Path
from models import BenchmarkResult, Scenario

BENCH_DIR = Path(__file__).parent.parent / 'benchmarks'


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
    subprocess.run(
        ['npm', 'install'], cwd=BENCH_DIR, check=True, capture_output=True
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
                ['make', f'{scenario}_{opt}'],
                cwd=BENCH_DIR,
                check=True,
                capture_output=True,
            )
            _cache[key] = BENCH_DIR / f'{scenario}_{opt}'
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
