from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Callable


@dataclass(frozen=True)
class FunctionSpec:
    name: str
    kind: str
    dsl_expr: str
    c_expr: str
    ts_expr: str
    py_impl: Callable[..., int | bool]


@dataclass(frozen=True)
class Operation:
    kind: str
    arg: str | int | None = None
    extra: int | None = None


@dataclass(frozen=True)
class BenchmarkScenario:
    name: str
    n: int
    runs: int
    ops: tuple[Operation, ...]

    def describe(self) -> str:
        parts: list[str] = []
        for op in self.ops:
            if op.kind in {'map', 'filter', 'scan', 'takeWhile', 'skipWhile', 'distinctUntilChanged'}:
                parts.append(f'{op.kind}({op.arg})')
            elif op.kind == 'reduce':
                parts.append(f'reduce({op.arg}, {op.extra})')
            elif op.kind in {'take', 'skip'}:
                parts.append(f'{op.kind}({op.arg})')
            elif op.kind == 'last':
                parts.append('last()')
            else:
                parts.append(op.kind)
        if len(parts) > 16:
            preview = parts[:8] + [f'... ({len(parts) - 11} more ops) ...'] + parts[-3:]
            return ' -> '.join(preview)
        return ' -> '.join(parts)


def _value_func(
    name: str,
    dsl_expr: str,
    c_expr: str,
    ts_expr: str,
    py_impl: Callable[[int], int],
) -> FunctionSpec:
    return FunctionSpec(name, 'value', dsl_expr, c_expr, ts_expr, py_impl)


def _predicate_func(
    name: str,
    dsl_expr: str,
    c_expr: str,
    ts_expr: str,
    py_impl: Callable[[int], bool],
) -> FunctionSpec:
    return FunctionSpec(name, 'predicate', dsl_expr, c_expr, ts_expr, py_impl)


def _accumulator_func(
    name: str,
    dsl_expr: str,
    c_expr: str,
    ts_expr: str,
    py_impl: Callable[[int, int], int],
) -> FunctionSpec:
    return FunctionSpec(name, 'accumulator', dsl_expr, c_expr, ts_expr, py_impl)


FUNCTIONS = {
    'identity': _value_func(
        'identity', 'identity(x)', 'x', 'x', lambda x: x
    ),
    'add1': _value_func('add1', 'plus(x, 1)', 'x + 1', 'x + 1', lambda x: x + 1),
    'sub1': _value_func('sub1', 'minus(x, 1)', 'x - 1', 'x - 1', lambda x: x - 1),
    'times2': _value_func(
        'times2', 'mul(x, 2)', 'x * 2', 'x * 2', lambda x: x * 2
    ),
    'times3': _value_func(
        'times3', 'mul(x, 3)', 'x * 3', 'x * 3', lambda x: x * 3
    ),
    'square': _value_func(
        'square', 'mul(x, x)', 'x * x', 'x * x', lambda x: x * x
    ),
    'div10': _value_func(
        'div10', 'div(x, 10)', 'x / 10', 'Math.trunc(x / 10)', lambda x: x // 10
    ),
    'isEven': _predicate_func(
        'isEven',
        'eq(mod(x, 2), 0)',
        '(x % 2) == 0',
        '(x % 2) === 0',
        lambda x: (x % 2) == 0,
    ),
    'isDiv5': _predicate_func(
        'isDiv5',
        'eq(mod(x, 5), 0)',
        '(x % 5) == 0',
        '(x % 5) === 0',
        lambda x: (x % 5) == 0,
    ),
    'lt1500': _predicate_func(
        'lt1500', 'lt(x, 1500)', 'x < 1500', 'x < 1500', lambda x: x < 1500
    ),
    'lt5000': _predicate_func(
        'lt5000', 'lt(x, 5000)', 'x < 5000', 'x < 5000', lambda x: x < 5000
    ),
    'sum': _accumulator_func(
        'sum',
        'plus(accum, next)',
        'accum + next',
        'acc + x',
        lambda accum, next_value: accum + next_value,
    ),
}


def _long_map_chain(length: int) -> tuple[Operation, ...]:
    ops: list[Operation] = []
    for index in range(length):
        ops.append(Operation('map', 'add1' if index % 2 == 0 else 'sub1'))
    ops.append(Operation('last'))
    return tuple(ops)


SCENARIOS = (
    BenchmarkScenario(
        's01_map_reduce',
        n=100_000,
        runs=5,
        ops=(
            Operation('map', 'add1'),
            Operation('reduce', 'sum', 0),
        ),
    ),
    BenchmarkScenario(
        's02_map_filter_reduce',
        n=100_000,
        runs=5,
        ops=(
            Operation('map', 'square'),
            Operation('filter', 'isEven'),
            Operation('reduce', 'sum', 0),
        ),
    ),
    BenchmarkScenario(
        's03_map_map_filter_reduce',
        n=80_000,
        runs=5,
        ops=(
            Operation('map', 'add1'),
            Operation('map', 'times3'),
            Operation('filter', 'isDiv5'),
            Operation('reduce', 'sum', 0),
        ),
    ),
    BenchmarkScenario(
        's04_skip_take_map_reduce',
        n=50_000,
        runs=5,
        ops=(
            Operation('skip', 500),
            Operation('take', 20_000),
            Operation('map', 'times2'),
            Operation('reduce', 'sum', 0),
        ),
    ),
    BenchmarkScenario(
        's05_takewhile_reduce',
        n=20_000,
        runs=5,
        ops=(
            Operation('map', 'times2'),
            Operation('takeWhile', 'lt1500'),
            Operation('reduce', 'sum', 0),
        ),
    ),
    BenchmarkScenario(
        's06_scan_last',
        n=20_000,
        runs=5,
        ops=(
            Operation('map', 'add1'),
            Operation('scan', 'sum'),
            Operation('last'),
        ),
    ),
    BenchmarkScenario(
        's07_skipwhile_last',
        n=20_000,
        runs=5,
        ops=(
            Operation('skipWhile', 'lt5000'),
            Operation('map', 'add1'),
            Operation('last'),
        ),
    ),
    BenchmarkScenario(
        's08_distinct_until_changed_last',
        n=50_000,
        runs=5,
        ops=(
            Operation('map', 'div10'),
            Operation('distinctUntilChanged', 'identity'),
            Operation('last'),
        ),
    ),
    BenchmarkScenario(
        's09_chain_100',
        n=1_000,
        runs=5,
        ops=_long_map_chain(100),
    ),
    BenchmarkScenario(
        's10_chain_10000',
        n=10,
        runs=3,
        ops=_long_map_chain(10_000),
    ),
    BenchmarkScenario(
        's11_map_reduce_x1000_items',
        n=100_000_000,
        runs=1,
        ops=(
            Operation('map', 'add1'),
            Operation('reduce', 'sum', 0),
        ),
    ),
)


def expected_result(scenario: BenchmarkScenario) -> int:
    values = list(range(1, scenario.n + 1))

    for op in scenario.ops:
        if op.kind == 'map':
            fn = FUNCTIONS[str(op.arg)].py_impl
            values = [int(fn(value)) for value in values]
        elif op.kind == 'filter':
            fn = FUNCTIONS[str(op.arg)].py_impl
            values = [value for value in values if bool(fn(value))]
        elif op.kind == 'reduce':
            fn = FUNCTIONS[str(op.arg)].py_impl
            accum = int(op.extra or 0)
            for value in values:
                accum = int(fn(accum, value))
            values = [accum]
        elif op.kind == 'scan':
            fn = FUNCTIONS[str(op.arg)].py_impl
            accum = 0
            scanned: list[int] = []
            for value in values:
                accum = int(fn(accum, value))
                scanned.append(accum)
            values = scanned
        elif op.kind == 'take':
            values = values[: int(op.arg)]
        elif op.kind == 'skip':
            values = values[int(op.arg) :]
        elif op.kind == 'takeWhile':
            fn = FUNCTIONS[str(op.arg)].py_impl
            taken: list[int] = []
            for value in values:
                if not bool(fn(value)):
                    break
                taken.append(value)
            values = taken
        elif op.kind == 'skipWhile':
            fn = FUNCTIONS[str(op.arg)].py_impl
            start = 0
            for index, value in enumerate(values):
                if not bool(fn(value)):
                    start = index
                    break
            else:
                start = len(values)
            values = values[start:]
        elif op.kind == 'distinctUntilChanged':
            fn = FUNCTIONS[str(op.arg)].py_impl
            distinct: list[int] = []
            last_key: int | None = None
            has_key = False
            for value in values:
                key = int(fn(value))
                if has_key and key == last_key:
                    continue
                distinct.append(value)
                last_key = key
                has_key = True
            values = distinct
        elif op.kind == 'last':
            values = values[-1:] if values else []
        else:
            raise ValueError(f'Unsupported scenario op for evaluator: {op.kind}')

    return values[-1] if values else 0


def _required_functions(scenario: BenchmarkScenario) -> list[FunctionSpec]:
    required: dict[str, FunctionSpec] = {}
    for op in scenario.ops:
        if isinstance(op.arg, str) and op.arg in FUNCTIONS:
            required[op.arg] = FUNCTIONS[op.arg]
    return [required[name] for name in sorted(required)]


def _dsl_function(spec: FunctionSpec) -> str:
    if spec.kind == 'accumulator':
        return f'fn {spec.name}(accum, next) {{ return {spec.dsl_expr}; }}'
    return f'fn {spec.name}(x) {{ return {spec.dsl_expr}; }}'


def generate_dsl_source(scenario: BenchmarkScenario) -> str:
    lines = [_dsl_function(spec) for spec in _required_functions(scenario)]
    if lines:
        lines.append('')

    op_lines: list[str] = []
    for op in scenario.ops:
        if op.kind == 'map':
            op_lines.append(f'map({op.arg})')
        elif op.kind == 'filter':
            op_lines.append(f'filter({op.arg})')
        elif op.kind == 'reduce':
            op_lines.append(f'reduce({op.arg}, {op.extra})')
        elif op.kind == 'scan':
            op_lines.append(f'scan({op.arg})')
        elif op.kind == 'take':
            op_lines.append(f'take({op.arg})')
        elif op.kind == 'skip':
            op_lines.append(f'skip({op.arg})')
        elif op.kind == 'takeWhile':
            op_lines.append(f'takeWhile({op.arg})')
        elif op.kind == 'skipWhile':
            op_lines.append(f'skipWhile({op.arg})')
        elif op.kind == 'distinctUntilChanged':
            op_lines.append(f'distinctUntilChanged({op.arg})')
        elif op.kind == 'last':
            op_lines.append('last()')
        else:
            raise ValueError(f'Unsupported DSL op: {op.kind}')

    lines.append('range(1, N).pipe(')
    for index, op_line in enumerate(op_lines):
        suffix = ',' if index < len(op_lines) - 1 else ''
        lines.append(f'    {op_line}{suffix}')
    lines.append(').subscribe(assign(result_value));')
    lines.append('')
    return '\n'.join(lines)


def _c_function(spec: FunctionSpec) -> str:
    if spec.kind == 'predicate':
        return (
            f'static bool {spec.name}(intptr_t x) {{ return {spec.c_expr}; }}'
        )
    if spec.kind == 'accumulator':
        return (
            f'static intptr_t {spec.name}(intptr_t accum, intptr_t next) '
            f'{{ return {spec.c_expr}; }}'
        )
    return f'static intptr_t {spec.name}(intptr_t x) {{ return {spec.c_expr}; }}'


def generate_raw_c_source(scenario: BenchmarkScenario) -> str:
    functions = '\n'.join(_c_function(spec) for spec in _required_functions(scenario))
    uses_reduce = any(op.kind == 'reduce' for op in scenario.ops)
    uses_scan = any(op.kind == 'scan' for op in scenario.ops)
    uses_last = any(op.kind == 'last' for op in scenario.ops)
    uses_take = any(op.kind == 'take' for op in scenario.ops)
    uses_skip = any(op.kind == 'skip' for op in scenario.ops)
    uses_takewhile = any(op.kind == 'takeWhile' for op in scenario.ops)
    uses_skipwhile = any(op.kind == 'skipWhile' for op in scenario.ops)
    uses_distinct = any(op.kind == 'distinctUntilChanged' for op in scenario.ops)

    state_lines: list[str] = []
    if uses_take:
        state_lines.append('    intptr_t take_count = 0;')
    if uses_skip:
        state_lines.append('    intptr_t skip_count = 0;')
    if uses_scan:
        state_lines.append('    intptr_t scan_accum = 0;')
    if uses_reduce:
        initial = next(op.extra for op in scenario.ops if op.kind == 'reduce')
        state_lines.append(f'    intptr_t reduce_accum = (intptr_t)({initial});')
    if uses_last:
        state_lines.append('    intptr_t last_value = 0;')
        state_lines.append('    bool has_last_value = false;')
    if uses_skipwhile:
        state_lines.append('    bool skip_while_passed = false;')
    if uses_distinct:
        state_lines.append('    intptr_t last_key = 0;')
        state_lines.append('    bool has_last_key = false;')

    body_lines = [
        '    for (intptr_t src = 1; src <= (intptr_t)N; ++src) {',
        '        intptr_t value = src;',
        '        bool emit_value = true;',
        '        bool break_after_emit = false;',
    ]

    for op in scenario.ops:
        if op.kind == 'map':
            body_lines.append(f'        value = {op.arg}(value);')
        elif op.kind == 'filter':
            body_lines.append(f'        if (!{op.arg}(value)) {{ continue; }}')
        elif op.kind == 'reduce':
            body_lines.append(f'        reduce_accum = {op.arg}(reduce_accum, value);')
            body_lines.append('        emit_value = false;')
        elif op.kind == 'scan':
            body_lines.append(f'        scan_accum = {op.arg}(scan_accum, value);')
            body_lines.append('        value = scan_accum;')
        elif op.kind == 'take':
            body_lines.append(
                f'        if (take_count >= (intptr_t)({op.arg})) {{ break; }}'
            )
            body_lines.append('        take_count++;')
        elif op.kind == 'skip':
            body_lines.append(
                f'        if (skip_count < (intptr_t)({op.arg})) {{ skip_count++; continue; }}'
            )
        elif op.kind == 'takeWhile':
            body_lines.append(f'        if (!{op.arg}(value)) {{ break; }}')
        elif op.kind == 'skipWhile':
            body_lines.append(
                f'        if (!skip_while_passed && {op.arg}(value)) {{ continue; }}'
            )
            body_lines.append('        skip_while_passed = true;')
        elif op.kind == 'distinctUntilChanged':
            body_lines.append('        {')
            body_lines.append(f'            intptr_t key = {op.arg}(value);')
            body_lines.append(
                '            if (has_last_key && key == last_key) { continue; }'
            )
            body_lines.append('            last_key = key;')
            body_lines.append('            has_last_key = true;')
            body_lines.append('        }')
        elif op.kind == 'last':
            body_lines.append('        last_value = value;')
            body_lines.append('        has_last_value = true;')
            body_lines.append('        emit_value = false;')
        else:
            raise ValueError(f'Unsupported raw C op: {op.kind}')

    body_lines.append('        if (break_after_emit) { break; }')
    body_lines.append('    }')
    if uses_reduce:
        body_lines.append('    return (int64_t)reduce_accum;')
    elif uses_last:
        body_lines.append('    return has_last_value ? (int64_t)last_value : 0;')
    else:
        body_lines.append('    return 0;')

    return f"""#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

{functions}

static int64_t run_once(int N) {{
{chr(10).join(state_lines)}
{chr(10).join(body_lines)}
}}

int main(int argc, char **argv) {{
    int N = argc > 1 ? atoi(argv[1]) : {scenario.n};
    int RUNS = argc > 2 ? atoi(argv[2]) : {scenario.runs};
    int64_t result = 0;
    int64_t total_ns = 0;

    for (int run = 0; run < RUNS; ++run) {{
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        result = run_once(N);
        clock_gettime(CLOCK_MONOTONIC, &end);
        total_ns += (int64_t)(end.tv_sec - start.tv_sec) * 1000000000LL +
                    (int64_t)(end.tv_nsec - start.tv_nsec);
    }}

    printf("{{\\"result\\": %" PRId64 ", \\"average_ms\\": %.5f, \\"runs\\": %d, \\"n\\": %d}}\\n",
           result, (double)total_ns / RUNS / 1e6, RUNS, N);
    return 0;
}}
"""


def _ts_function(spec: FunctionSpec) -> str:
    if spec.kind == 'predicate':
        return f'const {spec.name} = (x: number): boolean => {spec.ts_expr};'
    if spec.kind == 'accumulator':
        return f'const {spec.name} = (acc: number, x: number): number => {spec.ts_expr};'
    return f'const {spec.name} = (x: number): number => {spec.ts_expr};'


def generate_ts_source(scenario: BenchmarkScenario) -> str:
    operator_names = {'range'}
    for op in scenario.ops:
        if op.kind == 'map':
            operator_names.add('map')
        elif op.kind == 'filter':
            operator_names.add('filter')
        elif op.kind == 'reduce':
            operator_names.add('reduce')
        elif op.kind == 'scan':
            operator_names.add('scan')
        elif op.kind == 'take':
            operator_names.add('take')
        elif op.kind == 'skip':
            operator_names.add('skip')
        elif op.kind == 'takeWhile':
            operator_names.add('takeWhile')
        elif op.kind == 'skipWhile':
            operator_names.add('skipWhile')
        elif op.kind == 'distinctUntilChanged':
            operator_names.add('distinctUntilChanged')
        elif op.kind == 'last':
            operator_names.add('last')

    helper_lines = '\n'.join(
        _ts_function(spec) for spec in _required_functions(scenario)
    )

    pipe_lines: list[str] = []
    for op in scenario.ops:
        if op.kind == 'map':
            pipe_lines.append(f'map({op.arg})')
        elif op.kind == 'filter':
            pipe_lines.append(f'filter({op.arg})')
        elif op.kind == 'reduce':
            pipe_lines.append(f'reduce({op.arg}, {op.extra})')
        elif op.kind == 'scan':
            pipe_lines.append(f'scan({op.arg}, 0)')
        elif op.kind == 'take':
            pipe_lines.append(f'take({op.arg})')
        elif op.kind == 'skip':
            pipe_lines.append(f'skip({op.arg})')
        elif op.kind == 'takeWhile':
            pipe_lines.append(f'takeWhile({op.arg})')
        elif op.kind == 'skipWhile':
            pipe_lines.append(f'skipWhile({op.arg})')
        elif op.kind == 'distinctUntilChanged':
            pipe_lines.append('distinctUntilChanged()')
        elif op.kind == 'last':
            pipe_lines.append('last()')
        else:
            raise ValueError(f'Unsupported TypeScript op: {op.kind}')

    return f"""const {{ performance }} = require('node:perf_hooks');
const {{ range }} = require('rxjs');
const {{ {', '.join(sorted(name for name in operator_names if name != 'range'))} }} = require('rxjs/operators');

{helper_lines}

const N: number = parseInt(process.argv[2] ?? '{scenario.n}', 10);
const RUNS: number = parseInt(process.argv[3] ?? '{scenario.runs}', 10);

let result: number = 0;
let totalMs: number = 0;

for (let run = 0; run < RUNS; run++) {{
    result = 0;
    const start = performance.now();

    let observable = range(1, N);
{chr(10).join(f'    observable = observable.pipe({line});' for line in pipe_lines)}
    observable.subscribe((value: number) => {{
        result = Number(value);
    }});

    totalMs += performance.now() - start;
}}

console.log(JSON.stringify({{ result, average_ms: totalMs / RUNS, runs: RUNS, n: N }}));
"""


def write_scenario_sources(base_dir: Path, scenario: BenchmarkScenario) -> dict[str, Path]:
    scenario_dir = base_dir / scenario.name
    scenario_dir.mkdir(parents=True, exist_ok=True)

    raw_c_path = scenario_dir / f'{scenario.name}_raw.c'
    dsl_path = scenario_dir / f'{scenario.name}.dsl'
    ts_path = scenario_dir / f'{scenario.name}.ts'

    raw_c_path.write_text(generate_raw_c_source(scenario), encoding='utf-8')
    dsl_path.write_text(generate_dsl_source(scenario), encoding='utf-8')
    ts_path.write_text(generate_ts_source(scenario), encoding='utf-8')

    return {
        'dir': scenario_dir,
        'raw_c': raw_c_path,
        'dsl': dsl_path,
        'ts': ts_path,
        'raw_binary': scenario_dir / f'{scenario.name}_raw.exe',
        'dsl_c': scenario_dir / f'{scenario.name}_dsl.c',
        'dsl_binary': scenario_dir / f'{scenario.name}_dsl.exe',
    }
