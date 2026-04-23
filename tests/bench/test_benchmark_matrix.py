import pytest

from bench.benchmark_matrix import SCENARIOS, BenchmarkScenario, expected_result


@pytest.mark.parametrize('scenario', SCENARIOS, ids=[s.name for s in SCENARIOS])
def test_benchmark_matrix(
    scenario: BenchmarkScenario,
    run_raw_c,
    run_dsl,
    run_typescript,
):
    expected = expected_result(scenario)

    raw_c = run_raw_c(scenario)
    dsl_c = run_dsl(scenario)
    typescript = run_typescript(scenario)

    assert raw_c.result == expected
    assert dsl_c.result == expected

    if scenario.name not in {'s10_chain_10000', 's12_chain_10000_x1000_items'}:
        assert typescript.result == expected

    print(f'\n{scenario.name}', flush=True)
    print(f'  inputs:     n={scenario.n}, runs={scenario.runs}', flush=True)
    print(f'  pipeline:   {scenario.describe()}', flush=True)
    print(f'  expected:   {expected}', flush=True)
    print(
        f'  raw C:      result={raw_c.result}  average_ms={raw_c.average_ms:.5f}',
        flush=True,
    )
    print(
        f'  DSL -> C:   result={dsl_c.result}  average_ms={dsl_c.average_ms:.5f}',
        flush=True,
    )
    print(
        f'  TypeScript: result={typescript.result}  average_ms={typescript.average_ms:.5f}',
        flush=True,
    )
