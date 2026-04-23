import pytest

from bench.benchmark_matrix import SCENARIOS, BenchmarkScenario, expected_result


@pytest.mark.parametrize('scenario', SCENARIOS, ids=[s.name for s in SCENARIOS])
def test_benchmark_matrix(
    scenario: BenchmarkScenario,
    selected_backends,
    run_raw_c,
    run_library_c,
    run_dsl,
    run_typescript,
):
    print(f'\n{scenario.name}', flush=True)
    print(f'  inputs:     n={scenario.n}, runs={scenario.runs}', flush=True)
    print(f'  source:     {scenario.source_description()}', flush=True)
    print(f'  pipeline:   {scenario.describe()}', flush=True)

    raw_c = (
        run_raw_c(scenario)
        if 'raw_c' in scenario.backends and 'raw_c' in selected_backends
        else None
    )
    if raw_c is not None:
        print(
            f'  raw C:      result={raw_c.result}  average_ms={raw_c.average_ms:.5f}',
            flush=True,
        )
    else:
        print('  raw C:      unsupported', flush=True)

    library_c = (
        run_library_c(scenario)
        if 'library_c' in scenario.backends and 'library_c' in selected_backends
        else None
    )
    if library_c is not None:
        print(
            f'  library C:  result={library_c.result}  average_ms={library_c.average_ms:.5f}',
            flush=True,
        )
    else:
        print('  library C:  unsupported', flush=True)

    dsl_c = (
        run_dsl(scenario)
        if 'dsl_c' in scenario.backends and 'dsl_c' in selected_backends
        else None
    )
    if dsl_c is not None:
        print(
            f'  DSL -> C:   result={dsl_c.result}  average_ms={dsl_c.average_ms:.5f}',
            flush=True,
        )
    else:
        print('  DSL -> C:   unsupported', flush=True)

    typescript = (
        run_typescript(scenario)
        if 'typescript' in scenario.backends and 'typescript' in selected_backends
        else None
    )
    if typescript is not None:
        print(
            f'  TypeScript: result={typescript.result}  average_ms={typescript.average_ms:.5f}',
            flush=True,
        )
    else:
        print('  TypeScript: unsupported', flush=True)

    expected = expected_result(scenario)
    print(f'  expected:   {expected}', flush=True)

    if raw_c is not None:
        assert raw_c.result == expected
    if library_c is not None:
        assert library_c.result == expected
    if dsl_c is not None:
        assert dsl_c.result == expected

    if (
        typescript is not None
        and scenario.name
        not in {'s10_chain_10000', 's12_chain_10000_x1000_items'}
    ):
        assert typescript.result == expected
