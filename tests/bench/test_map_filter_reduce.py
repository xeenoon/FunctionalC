from models import BenchmarkResult, Scenario

SCENARIO = Scenario.map_filter_reduce
EXPECTED = 166_667_166_667_000_000


def test_c_correctness(run_c):
    c: BenchmarkResult = run_c(SCENARIO)

    assert c.result == EXPECTED

    print(f'\nC -O2: {c.average_ms:.2f} ms')


def test_js_correctness(run_js):
    js: BenchmarkResult = run_js(SCENARIO)

    # float64 loses precision at this magnitude
    assert abs(js.result - EXPECTED) < 1_000_000

    print(f'\nRxJS:  {js.average_ms:.2f} ms')


def test_comparison(run_c, run_js):
    c: BenchmarkResult = run_c(SCENARIO)
    js: BenchmarkResult = run_js(SCENARIO)

    assert c.average_ms < js.average_ms

    print(f'\n{"C -O2":<10} {c.average_ms:>7.2f} ms')
    print(f'{"RxJS":<10} {js.average_ms:>7.2f} ms')
    print(f'{"Ratio":<10} {js.average_ms / c.average_ms:>7.2f}x')
