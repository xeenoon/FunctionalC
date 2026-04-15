import subprocess
import pytest
from pathlib import Path
from models import BenchmarkResult

BENCH_DIR = Path(__file__).parent.parent / 'benchmarks'


@pytest.fixture(scope='session', autouse=True)
def install_npm():
    subprocess.run(
        ['npm', 'install'], cwd=BENCH_DIR, check=True, capture_output=True
    )


@pytest.fixture(scope='session', autouse=True)
def compile_c():
    _cache = {}

    def _compile(scenario, opt='O2'):
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


@pytest.fixture(scope='session')
def run_c(compile_c):
    def _run(scenario, opt='O2', n=1_000_000, runs=5):
        binary = compile_c(scenario, opt)
        stdout = subprocess.run(
            [str(binary), str(n), str(runs)],
            capture_output=True,
            text=True,
            check=True,
        ).stdout
        return BenchmarkResult.parse(stdout)

    return _run


@pytest.fixture(scope='session')
def run_js():
    def _run(scenario, n=1_000_000, runs=5):
        stdout = subprocess.run(
            ['node', f'{scenario}.js', str(n), str(runs)],
            cwd=BENCH_DIR,
            capture_output=True,
            text=True,
            check=True,
        ).stdout
        return BenchmarkResult.parse(stdout)

    return _run
