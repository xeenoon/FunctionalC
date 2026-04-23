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
class SourceSpec:
    kind: str = 'range'
    inner_n: int | None = None


@dataclass(frozen=True)
class BenchmarkScenario:
    name: str
    n: int
    runs: int
    ops: tuple[Operation, ...]
    source: SourceSpec = SourceSpec()
    backends: tuple[str, ...] = ('raw_c', 'library_c', 'dsl_c', 'typescript')

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
            elif op.kind == 'pairMap':
                parts.append(f'map({op.arg})')
            elif op.kind == 'tripleMap':
                parts.append(f'map({op.arg})')
            else:
                parts.append(op.kind)
        if len(parts) > 16:
            preview = parts[:8] + [f'... ({len(parts) - 11} more ops) ...'] + parts[-3:]
            return ' -> '.join(preview)
        return ' -> '.join(parts)

    def source_description(self) -> str:
        if self.source.kind == 'range':
            return 'range(1, N)'
        if self.source.kind == 'zip_range':
            return 'zip(range(1, N), range(1, N))'
        if self.source.kind == 'merge_map_range':
            return f'mergeMap(range(1, N), range(1, {self.source.inner_n}))'
        if self.source.kind == 'zip_merge_map_range':
            return (
                f'zip(range(1, N), range(1, N))'
                f' -> mergeMap(range(1, {self.source.inner_n}))'
            )
        return self.source.kind


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
    'pairSum': _value_func(
        'pairSum',
        'pairSum(x)',
        'x',
        'pair[0] + pair[1]',
        lambda pair: pair[0] + pair[1],
    ),
    'tripleSum': _value_func(
        'tripleSum',
        'tripleSum(x)',
        'x',
        'triple[0][0] + triple[0][1] + triple[1]',
        lambda triple: triple[0][0] + triple[0][1] + triple[1],
    ),
}


def _long_map_chain(length: int) -> tuple[Operation, ...]:
    ops: list[Operation] = []
    for index in range(length):
        ops.append(Operation('map', 'add1' if index % 2 == 0 else 'sub1'))
    ops.append(Operation('last'))
    return tuple(ops)


def _zip_pair_map_chain(length: int) -> tuple[Operation, ...]:
    ops: list[Operation] = [Operation('pairMap', 'pairSum')]
    for index in range(length):
        ops.append(Operation('map', 'add1' if index % 2 == 0 else 'sub1'))
    ops.append(Operation('last'))
    return tuple(ops)


def _zip_stateful_chain(length: int) -> tuple[Operation, ...]:
    ops: list[Operation] = [
        Operation('pairMap', 'pairSum'),
        Operation('scan', 'sum'),
        Operation('map', 'div10'),
        Operation('distinctUntilChanged', 'identity'),
        Operation('skipWhile', 'lt5000'),
    ]
    for index in range(length):
        ops.append(Operation('map', 'add1' if index % 2 == 0 else 'sub1'))
    ops.append(Operation('last'))
    return tuple(ops)


def _mergemap_stateful_chain(length: int) -> tuple[Operation, ...]:
    return _zip_stateful_chain(length)


def _zip_mergemap_stateful_chain(length: int) -> tuple[Operation, ...]:
    ops: list[Operation] = [
        Operation('tripleMap', 'tripleSum'),
        Operation('scan', 'sum'),
        Operation('map', 'div10'),
        Operation('distinctUntilChanged', 'identity'),
        Operation('skipWhile', 'lt5000'),
    ]
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
    BenchmarkScenario(
        's12_chain_10000_x1000_items',
        n=10_000,
        runs=1,
        ops=_long_map_chain(10_000),
    ),
    BenchmarkScenario(
        's13_zip_complex_small',
        n=10_000,
        runs=3,
        source=SourceSpec('zip_range'),
        backends=('raw_c', 'library_c', 'typescript'),
        ops=(
            Operation('pairMap', 'pairSum'),
            Operation('scan', 'sum'),
            Operation('map', 'div10'),
            Operation('distinctUntilChanged', 'identity'),
            Operation('skipWhile', 'lt5000'),
            Operation('last'),
        ),
    ),
    BenchmarkScenario(
        's14_zip_complex_large',
        n=500_000,
        runs=1,
        source=SourceSpec('zip_range'),
        backends=('raw_c', 'library_c', 'typescript'),
        ops=(
            Operation('pairMap', 'pairSum'),
            Operation('scan', 'sum'),
            Operation('map', 'div10'),
            Operation('distinctUntilChanged', 'identity'),
            Operation('skipWhile', 'lt5000'),
            Operation('last'),
        ),
    ),
    BenchmarkScenario(
        's15_mergemap_complex_small',
        n=1_000,
        runs=3,
        source=SourceSpec('merge_map_range', inner_n=1_000),
        backends=('raw_c', 'library_c', 'typescript'),
        ops=(
            Operation('pairMap', 'pairSum'),
            Operation('scan', 'sum'),
            Operation('map', 'div10'),
            Operation('distinctUntilChanged', 'identity'),
            Operation('skipWhile', 'lt5000'),
            Operation('last'),
        ),
    ),
    BenchmarkScenario(
        's16_mergemap_complex_large',
        n=10_000,
        runs=1,
        source=SourceSpec('merge_map_range', inner_n=10_000),
        backends=('raw_c', 'library_c', 'typescript'),
        ops=(
            Operation('pairMap', 'pairSum'),
            Operation('scan', 'sum'),
            Operation('map', 'div10'),
            Operation('distinctUntilChanged', 'identity'),
            Operation('skipWhile', 'lt5000'),
            Operation('last'),
        ),
    ),
    BenchmarkScenario(
        's17_zip_chain_100_probe',
        n=100_000,
        runs=1,
        source=SourceSpec('zip_range'),
        backends=('raw_c', 'library_c', 'typescript'),
        ops=_zip_pair_map_chain(100),
    ),
    BenchmarkScenario(
        's18_zip_stateful_chain_50_probe',
        n=100_000,
        runs=1,
        source=SourceSpec('zip_range'),
        backends=('raw_c', 'library_c', 'typescript'),
        ops=_zip_stateful_chain(50),
    ),
    BenchmarkScenario(
        's19_zip_stateful_chain_50_large',
        n=500_000,
        runs=1,
        source=SourceSpec('zip_range'),
        backends=('raw_c', 'library_c', 'typescript'),
        ops=_zip_stateful_chain(50),
    ),
    BenchmarkScenario(
        's20_zip_stateful_chain_50_x2_large',
        n=1_000_000,
        runs=1,
        source=SourceSpec('zip_range'),
        backends=('raw_c', 'library_c', 'typescript'),
        ops=_zip_stateful_chain(50),
    ),
    BenchmarkScenario(
        's21_mergemap_stateful_chain_150_probe',
        n=1_000,
        runs=1,
        source=SourceSpec('merge_map_range', inner_n=1_000),
        backends=('raw_c', 'library_c', 'typescript'),
        ops=_mergemap_stateful_chain(150),
    ),
    BenchmarkScenario(
        's22_mergemap_stateful_chain_150_large',
        n=3_000,
        runs=1,
        source=SourceSpec('merge_map_range', inner_n=3_000),
        backends=('raw_c', 'library_c', 'typescript'),
        ops=_mergemap_stateful_chain(150),
    ),
    BenchmarkScenario(
        's23_zip_mergemap_complex_probe',
        n=1_000,
        runs=1,
        source=SourceSpec('zip_merge_map_range', inner_n=100),
        backends=('raw_c', 'library_c', 'typescript'),
        ops=(
            Operation('tripleMap', 'tripleSum'),
            Operation('scan', 'sum'),
            Operation('map', 'div10'),
            Operation('distinctUntilChanged', 'identity'),
            Operation('skipWhile', 'lt5000'),
            Operation('last'),
        ),
    ),
    BenchmarkScenario(
        's24_zip_mergemap_stateful_chain_150_large',
        n=3_000,
        runs=1,
        source=SourceSpec('zip_merge_map_range', inner_n=1_000),
        backends=('raw_c', 'library_c', 'typescript'),
        ops=_zip_mergemap_stateful_chain(150),
    ),
    BenchmarkScenario(
        's25_zip_mergemap_zip_bottleneck_1000_chain',
        n=1_000_000,
        runs=1,
        source=SourceSpec('zip_merge_map_range', inner_n=1),
        backends=('raw_c', 'library_c', 'typescript'),
        ops=_zip_mergemap_stateful_chain(1000),
    ),
    BenchmarkScenario(
        's26_zip_stateful_chain_50_x3_huge',
        n=3_000_000,
        runs=1,
        source=SourceSpec('zip_range'),
        backends=('raw_c', 'library_c', 'typescript'),
        ops=_zip_stateful_chain(50),
    ),
    BenchmarkScenario(
        's27_zip_mergemap_zip_bottleneck_5000_chain',
        n=1_000_000,
        runs=1,
        source=SourceSpec('zip_merge_map_range', inner_n=1),
        backends=('raw_c', 'library_c', 'typescript'),
        ops=_zip_mergemap_stateful_chain(5_000),
    ),
)


def expected_result(scenario: BenchmarkScenario) -> int:
    if scenario.source.kind == 'range':
        values = iter(range(1, scenario.n + 1))
    elif scenario.source.kind == 'zip_range':
        values = ((value, value) for value in range(1, scenario.n + 1))
    elif scenario.source.kind == 'merge_map_range':
        inner_n = scenario.source.inner_n
        if inner_n is None:
            raise ValueError('merge_map_range scenarios require inner_n')
        values = (
            (left, right)
            for right in range(1, inner_n + 1)
            for left in range(1, scenario.n + 1)
        )
    elif scenario.source.kind == 'zip_merge_map_range':
        inner_n = scenario.source.inner_n
        if inner_n is None:
            raise ValueError('zip_merge_map_range scenarios require inner_n')
        values = (
            ((left, left), right)
            for right in range(1, inner_n + 1)
            for left in range(1, scenario.n + 1)
        )
    else:
        raise ValueError(f'Unsupported scenario source for evaluator: {scenario.source.kind}')

    uses_reduce = any(op.kind == 'reduce' for op in scenario.ops)
    uses_last = any(op.kind == 'last' for op in scenario.ops)
    reduce_accum = next((int(op.extra or 0) for op in scenario.ops if op.kind == 'reduce'), 0)
    scan_accum = 0
    take_count = 0
    skip_count = 0
    skip_while_passed = False
    has_last_key = False
    last_key = 0
    has_last_value = False
    last_value = 0

    for initial_value in values:
        value = initial_value
        emit_value = True

        for op in scenario.ops:
            if op.kind == 'map':
                fn = FUNCTIONS[str(op.arg)].py_impl
                value = int(fn(value))
            elif op.kind == 'pairMap':
                fn = FUNCTIONS[str(op.arg)].py_impl
                value = int(fn(value))
            elif op.kind == 'tripleMap':
                fn = FUNCTIONS[str(op.arg)].py_impl
                value = int(fn(value))
            elif op.kind == 'filter':
                fn = FUNCTIONS[str(op.arg)].py_impl
                if not bool(fn(value)):
                    emit_value = False
                    break
            elif op.kind == 'reduce':
                fn = FUNCTIONS[str(op.arg)].py_impl
                reduce_accum = int(fn(reduce_accum, value))
                emit_value = False
                break
            elif op.kind == 'scan':
                fn = FUNCTIONS[str(op.arg)].py_impl
                scan_accum = int(fn(scan_accum, value))
                value = scan_accum
            elif op.kind == 'take':
                if take_count >= int(op.arg):
                    emit_value = False
                    break
                take_count += 1
            elif op.kind == 'skip':
                if skip_count < int(op.arg):
                    skip_count += 1
                    emit_value = False
                    break
            elif op.kind == 'takeWhile':
                fn = FUNCTIONS[str(op.arg)].py_impl
                if not bool(fn(value)):
                    emit_value = False
                    break
            elif op.kind == 'skipWhile':
                fn = FUNCTIONS[str(op.arg)].py_impl
                if not skip_while_passed and bool(fn(value)):
                    emit_value = False
                    break
                skip_while_passed = True
            elif op.kind == 'distinctUntilChanged':
                fn = FUNCTIONS[str(op.arg)].py_impl
                key = int(fn(value))
                if has_last_key and key == last_key:
                    emit_value = False
                    break
                last_key = key
                has_last_key = True
            elif op.kind == 'last':
                last_value = int(value)
                has_last_value = True
                emit_value = False
                break
            else:
                raise ValueError(f'Unsupported scenario op for evaluator: {op.kind}')

        if not emit_value:
            continue

    if uses_reduce:
        return reduce_accum
    if uses_last:
        return last_value if has_last_value else 0
    return 0


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
    if scenario.source.kind != 'range':
        raise ValueError(f'DSL generation does not support source {scenario.source.kind}')

    lines = [_dsl_function(spec) for spec in _required_functions(scenario)]
    if lines:
        lines.append('')

    op_lines: list[str] = []
    for op in scenario.ops:
        if op.kind == 'map':
            op_lines.append(f'map({op.arg})')
        elif op.kind == 'pairMap':
            raise ValueError('DSL generation does not support pairMap operations')
        elif op.kind == 'tripleMap':
            raise ValueError('DSL generation does not support tripleMap operations')
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


def _library_c_function(spec: FunctionSpec) -> str:
    if spec.name == 'pairSum':
        return (
            'static void *pairSum(void *x) {\n'
            '    intptr_t left = (intptr_t)pair_left(x);\n'
            '    intptr_t right = (intptr_t)pair_right(x);\n'
            '    return (void *)(intptr_t)(left + right);\n'
            '}'
        )
    if spec.name == 'tripleSum':
        return (
            'static void *tripleSum(void *x) {\n'
            '    void *pair = pair_left(x);\n'
            '    intptr_t left = (intptr_t)pair_left(pair);\n'
            '    intptr_t right = (intptr_t)pair_right(pair);\n'
            '    intptr_t extra = (intptr_t)pair_right(x);\n'
            '    return (void *)(intptr_t)(left + right + extra);\n'
            '}'
        )
    if spec.kind == 'predicate':
        return (
            f'static bool {spec.name}(void *raw) {{ '
            f'intptr_t x = (intptr_t)raw; return {spec.c_expr}; }}'
        )
    if spec.kind == 'accumulator':
        return (
            f'static void *{spec.name}(void *raw_accum, void *raw_next) {{ '
            f'intptr_t accum = (intptr_t)raw_accum; '
            f'intptr_t next = (intptr_t)raw_next; '
            f'return (void *)(intptr_t)({spec.c_expr}); }}'
        )
    return (
        f'static void *{spec.name}(void *raw) {{ '
        f'intptr_t x = (intptr_t)raw; '
        f'return (void *)(intptr_t)({spec.c_expr}); }}'
    )


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

    if scenario.source.kind == 'range':
        body_lines = [
            '    for (intptr_t src = 1; src <= (intptr_t)N; ++src) {',
            '        intptr_t value = src;',
            '        bool emit_value = true;',
            '        bool break_after_emit = false;',
        ]
    elif scenario.source.kind == 'zip_range':
        body_lines = [
            '    for (intptr_t src = 1; src <= (intptr_t)N; ++src) {',
            '        intptr_t left = src;',
            '        intptr_t right = src;',
            '        intptr_t value = 0;',
            '        bool emit_value = true;',
            '        bool break_after_emit = false;',
        ]
    elif scenario.source.kind == 'merge_map_range':
        inner_n = scenario.source.inner_n
        if inner_n is None:
            raise ValueError('merge_map_range scenarios require inner_n')
        body_lines = [
            f'    for (intptr_t right = 1; right <= (intptr_t){inner_n}; ++right) {{',
            '        for (intptr_t src = 1; src <= (intptr_t)N; ++src) {',
            '            intptr_t left = src;',
            '            intptr_t value = 0;',
            '            bool emit_value = true;',
            '            bool break_after_emit = false;',
        ]
    elif scenario.source.kind == 'zip_merge_map_range':
        inner_n = scenario.source.inner_n
        if inner_n is None:
            raise ValueError('zip_merge_map_range scenarios require inner_n')
        body_lines = [
            f'    for (intptr_t right = 1; right <= (intptr_t){inner_n}; ++right) {{',
            '        for (intptr_t src = 1; src <= (intptr_t)N; ++src) {',
            '            intptr_t left = src;',
            '            intptr_t zipped_left = src;',
            '            intptr_t zipped_right = src;',
            '            intptr_t value = 0;',
            '            bool emit_value = true;',
            '            bool break_after_emit = false;',
        ]
    else:
        raise ValueError(f'Unsupported raw C source: {scenario.source.kind}')

    for op in scenario.ops:
        if op.kind == 'map':
            body_lines.append(f'        value = {op.arg}(value);')
        elif op.kind == 'pairMap':
            if op.arg != 'pairSum':
                raise ValueError(f'Unsupported pair map op: {op.arg}')
            body_lines.append('        value = left + right;')
        elif op.kind == 'tripleMap':
            if op.arg != 'tripleSum':
                raise ValueError(f'Unsupported triple map op: {op.arg}')
            body_lines.append('        value = zipped_left + zipped_right + right;')
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

    if scenario.source.kind in {'merge_map_range', 'zip_merge_map_range'}:
        body_lines.append('            if (break_after_emit) { break; }')
        body_lines.append('        }')
        body_lines.append('    }')
    else:
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


def generate_library_c_source(scenario: BenchmarkScenario) -> str:
    functions = '\n'.join(
        _library_c_function(spec) for spec in _required_functions(scenario)
    )

    source_lines: list[str]
    if scenario.source.kind == 'range':
        source_lines = ['    Observable *observable = range(1, N);']
    elif scenario.source.kind == 'zip_range':
        source_lines = [
            '    Observable *observable = zip(2, range(1, N), range(1, N));'
        ]
    elif scenario.source.kind == 'merge_map_range':
        inner_n = scenario.source.inner_n
        if inner_n is None:
            raise ValueError('merge_map_range scenarios require inner_n')
        source_lines = [
            '    Observable *observable = range(1, N);',
            f'    Observable *merge_source = range(1, {inner_n});',
            '    append_query(&observable, mergeMap(merge_source));',
        ]
    elif scenario.source.kind == 'zip_merge_map_range':
        inner_n = scenario.source.inner_n
        if inner_n is None:
            raise ValueError('zip_merge_map_range scenarios require inner_n')
        source_lines = [
            '    Observable *observable = zip(2, range(1, N), range(1, N));',
            f'    Observable *merge_source = range(1, {inner_n});',
            '    append_query(&observable, mergeMap(merge_source));',
        ]
    else:
        raise ValueError(f'Unsupported library C source: {scenario.source.kind}')

    op_lines: list[str] = []
    for op in scenario.ops:
        if op.kind == 'map':
            op_lines.append(f'    append_query(&observable, map({op.arg}));')
        elif op.kind == 'pairMap':
            op_lines.append(f'    append_query(&observable, map({op.arg}));')
        elif op.kind == 'tripleMap':
            op_lines.append(f'    append_query(&observable, map({op.arg}));')
        elif op.kind == 'filter':
            op_lines.append(f'    append_query(&observable, filter({op.arg}));')
        elif op.kind == 'reduce':
            op_lines.append(f'    append_query(&observable, reduce({op.arg}));')
        elif op.kind == 'scan':
            op_lines.append(f'    append_query(&observable, scan({op.arg}));')
        elif op.kind == 'take':
            op_lines.append(f'    append_query(&observable, take({op.arg}));')
        elif op.kind == 'skip':
            op_lines.append(f'    append_query(&observable, skip({op.arg}));')
        elif op.kind == 'takeWhile':
            op_lines.append(f'    append_query(&observable, takeWhile({op.arg}));')
        elif op.kind == 'skipWhile':
            op_lines.append(f'    append_query(&observable, skipWhile({op.arg}));')
        elif op.kind == 'distinctUntilChanged':
            op_lines.append(
                f'    append_query(&observable, distinctUntilChanged({op.arg}));'
            )
        elif op.kind == 'last':
            op_lines.append('    append_query(&observable, last());')
        else:
            raise ValueError(f'Unsupported library C op: {op.kind}')

    return f"""#define _POSIX_C_SOURCE 200809L
#include "observable.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

{functions}

static intptr_t result_value = 0;

static void assign_result(void *value) {{
    result_value = (intptr_t)value;
}}

static void append_query(Observable **root, Query *query) {{
    Observable *current = *root;
    while (current->pipe != NULL) {{
        current = current->pipe;
    }}
    Observable *next = create_observable();
    current->emit_handler = query;
    current->pipe = next;
}}

static intptr_t run_once(int N) {{
{chr(10).join(source_lines)}
{chr(10).join(op_lines)}
    result_value = 0;
    subscribe(observable, assign_result);
    return result_value;
}}

int main(int argc, char **argv) {{
    int N = argc > 1 ? atoi(argv[1]) : {scenario.n};
    int RUNS = argc > 2 ? atoi(argv[2]) : {scenario.runs};
    intptr_t result = 0;
    int64_t total_ns = 0;

    for (int run = 0; run < RUNS; ++run) {{
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        result = run_once(N);
        clock_gettime(CLOCK_MONOTONIC, &end);
        total_ns += (int64_t)(end.tv_sec - start.tv_sec) * 1000000000LL +
                    (int64_t)(end.tv_nsec - start.tv_nsec);
    }}

    printf("{{\\"result\\": %" PRIdPTR ", \\"average_ms\\": %.5f, \\"runs\\": %d, \\"n\\": %d}}\\n",
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
    source_names = {'range'}
    for op in scenario.ops:
        if op.kind == 'map':
            source_names.add('map')
        elif op.kind == 'pairMap':
            source_names.add('map')
        elif op.kind == 'tripleMap':
            source_names.add('map')
        elif op.kind == 'filter':
            source_names.add('filter')
        elif op.kind == 'reduce':
            source_names.add('reduce')
        elif op.kind == 'scan':
            source_names.add('scan')
        elif op.kind == 'take':
            source_names.add('take')
        elif op.kind == 'skip':
            source_names.add('skip')
        elif op.kind == 'takeWhile':
            source_names.add('takeWhile')
        elif op.kind == 'skipWhile':
            source_names.add('skipWhile')
        elif op.kind == 'distinctUntilChanged':
            source_names.add('distinctUntilChanged')
        elif op.kind == 'last':
            source_names.add('last')

    if scenario.source.kind == 'zip_range':
        source_names.add('zip')
    if scenario.source.kind == 'merge_map_range':
        source_names.add('mergeMap')
    if scenario.source.kind == 'zip_merge_map_range':
        source_names.add('zip')
        source_names.add('mergeMap')

    helper_lines = '\n'.join(
        _ts_function(spec) for spec in _required_functions(scenario)
    )

    pipe_lines: list[str] = []
    for op in scenario.ops:
        if op.kind == 'map':
            pipe_lines.append(f'map({op.arg})')
        elif op.kind == 'pairMap':
            if op.arg != 'pairSum':
                raise ValueError(f'Unsupported TypeScript pairMap op: {op.arg}')
            pipe_lines.append('map((pair) => pair[0] + pair[1])')
        elif op.kind == 'tripleMap':
            if op.arg != 'tripleSum':
                raise ValueError(f'Unsupported TypeScript tripleMap op: {op.arg}')
            pipe_lines.append('map((triple) => triple[0][0] + triple[0][1] + triple[1])')
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

    if scenario.source.kind == 'range':
        source_expr = 'range(1, N)'
        root_imports = 'range'
    elif scenario.source.kind == 'zip_range':
        source_expr = 'zip(range(1, N), range(1, N))'
        root_imports = 'range, zip'
    elif scenario.source.kind == 'merge_map_range':
        inner_n = scenario.source.inner_n
        if inner_n is None:
            raise ValueError('merge_map_range scenarios require inner_n')
        source_expr = (
            f'range(1, {inner_n}).pipe('
            'mergeMap((right) => range(1, N).pipe(map((left) => [left, right]))))'
        )
        root_imports = 'range'
    elif scenario.source.kind == 'zip_merge_map_range':
        inner_n = scenario.source.inner_n
        if inner_n is None:
            raise ValueError('zip_merge_map_range scenarios require inner_n')
        source_expr = (
            f'range(1, {inner_n}).pipe('
            'mergeMap((right) => zip(range(1, N), range(1, N)).pipe(map((pair) => [pair, right]))))'
        )
        root_imports = 'range, zip'
    else:
        raise ValueError(f'Unsupported TypeScript source: {scenario.source.kind}')

    operator_imports = ', '.join(sorted(name for name in source_names if name not in {'range', 'zip'}))

    return f"""const {{ performance }} = require('node:perf_hooks');
const {{ {root_imports} }} = require('rxjs');
const {{ {operator_imports} }} = require('rxjs/operators');

{helper_lines}

const N: number = parseInt(process.argv[2] ?? '{scenario.n}', 10);
const RUNS: number = parseInt(process.argv[3] ?? '{scenario.runs}', 10);

let result: number = 0;
let totalMs: number = 0;

for (let run = 0; run < RUNS; run++) {{
    result = 0;
    const start = performance.now();

    let observable = {source_expr};
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
    library_c_path = scenario_dir / f'{scenario.name}_library.c'
    dsl_path = scenario_dir / f'{scenario.name}.dsl'
    ts_path = scenario_dir / f'{scenario.name}.ts'

    raw_c_path.write_text(generate_raw_c_source(scenario), encoding='utf-8')
    library_c_path.write_text(generate_library_c_source(scenario), encoding='utf-8')
    if 'dsl_c' in scenario.backends:
        dsl_path.write_text(generate_dsl_source(scenario), encoding='utf-8')
    ts_path.write_text(generate_ts_source(scenario), encoding='utf-8')

    return {
        'dir': scenario_dir,
        'raw_c': raw_c_path,
        'library_c': library_c_path,
        'dsl': dsl_path,
        'ts': ts_path,
        'raw_binary': scenario_dir / f'{scenario.name}_raw.exe',
        'library_binary': scenario_dir / f'{scenario.name}_library.exe',
        'dsl_c': scenario_dir / f'{scenario.name}_dsl.c',
        'dsl_binary': scenario_dir / f'{scenario.name}_dsl.exe',
    }
