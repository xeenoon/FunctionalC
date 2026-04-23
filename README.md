# FunctionalC

FunctionalC is a C-based optimization of the existing RxJS library for large-scale backend workloads. It is designed to make high-throughput, RxJS-style data processing practical for backend operations where JavaScript alone becomes a bottleneck, while preserving the observable pipeline model and adding a transpilation path that can lower selected chains into optimized C.

In practical terms, the project is aimed at workloads where large volumes of data need to move through long operator chains with predictable latency and much lower overhead than a standard TypeScript/RxJS implementation.

## Performance Summary

The current benchmark suite shows the same overall pattern across the tested scenarios:

- for smaller synchronous pipelines, the raw C path is typically around 100x faster than the TypeScript/RxJS version
- the DSL-transpiled C path is also consistently faster than TypeScript/RxJS, while preserving the observable-style pipeline shape
- for heavier backend-style stress cases, especially very deep operator chains, zipped streams, or very large data volumes, the gap becomes much larger

This is the main purpose of the library: to make backend scaling with RxJS-style pipelines viable when the workload is too large or too latency-sensitive for a standard TypeScript implementation.

Examples from the current benchmark output:

- `s01_map_reduce`: raw C `0.02622 ms` vs TypeScript `3.47722 ms`, an exact speedup of `132.62x`
- `s02_map_filter_reduce`: raw C `0.05702 ms` vs TypeScript `5.28536 ms`, an exact speedup of `92.69x`
- `s11_map_reduce_x1000_items`: raw C `29.36070 ms` vs TypeScript `1662.24730 ms`, an exact speedup of `56.61x` on a much larger data volume
- `s12_chain_10000_x1000_items`: raw C `0.00010 ms` vs TypeScript `20.08890 ms`, with the TypeScript/RxJS version also failing to produce the expected result in that stress case
- `s13_zip_complex_small`: raw C `0.02273 ms` vs TypeScript `7.01493 ms`, an exact speedup of `308.62x` for a zipped, stateful operator chain
- `s14_zip_complex_large`: raw C `0.51960 ms` vs TypeScript `53568.75100 ms`, an exact speedup of `103096.13x` for a larger backend-style zipped workload

Taken together, these benchmarks show the intended use case of the library clearly: for ordinary backend pipeline work, the system is often around 100x faster than the equivalent TypeScript/RxJS implementation, and for heavy-duty workloads involving deep chains, zipped streams, or large-scale throughput, the measured gap can reach into the thousands or even more than 10000x faster depending on the scenario.

The repository currently contains:

- `core/`: the main C runtime and supporting tools
- `core/src/observable.c`: the observable/pipeline implementation
- `core/tools/`: codegen-related tooling
- `benchmarks/`: benchmark programs in C and JavaScript
- `tests/`: Python-based benchmark tests
- `testdata/`: checked-in DSL examples
- `out/`: generated output artifacts

## What It Does

The C runtime exposes observable-style operations such as:

- `range`
- `map`
- `filter`
- `reduce`
- `scan`
- `take` / `skip`
- `buffer`
- `throttleTime`
- `distinct` / `distinctUntilChanged`

The Python test suite exercises the benchmark scenario in `benchmarks/map_filter_reduce.c` and compares it with the JavaScript version in `benchmarks/map_filter_reduce.js`.

## DSL And Transpiler

FunctionalC includes a transpilation pipeline that converts an RxJS-style DSL into generated C. The main entry point is `core/tools/pipeline_codegen.c`, supported by the lexer, parser, lowering, planning, and code generation stages under `core/tools/`.

The DSL is designed for describing observable pipelines and the helper functions used by those pipelines. A typical DSL file looks like this:

```txt
fn square(x) { return mul(x, x); }
fn isEven(x) { return eq(mod(x, 2), 0); }
fn add(accum, next) { return plus(accum, next); }

range(1, N).pipe(
    map(square),
    filter(isEven),
    reduce(add, 0)
).subscribe(assign(result_sum));
```

At a high level, the DSL describes:

- an observable source such as `range(...)`, `of(...)`, `zip(...)`, `interval(...)`, or `timer(...)`
- a `.pipe(...)` chain of operators such as `map`, `filter`, `reduce`, `scan`, `take`, `skip`, `distinct`, `buffer`, and `throttleTime`
- a `.subscribe(...)` sink, currently represented as assignment-style output targets such as `assign(result_sum)`

The transpiler parses the DSL into an internal representation, validates the pipeline, and then emits C in one of two forms:

- a fused implementation for simple pipelines that can be specialized into a single tight loop
- a runtime-backed implementation that targets the generic observable runtime in `core/src/observable.c`

This allows the repository to express RxJS-style pipelines in a concise DSL while generating C that can either reuse the existing runtime or specialize selected operator chains for lower overhead.

### DSL Samples

Example DSL programs live under `testdata/`, including:

- `testdata/singular/`: single-operator examples
- `testdata/combinations/`: multi-operator pipelines such as `map_filter_reduce.dsl`

## Requirements

To run the Python tests successfully, you need:

- Python 3.14+
- `pytest`
- Node.js and `npm`
- `gcc`
- `make`

On Windows, the tests resolve `npm.cmd` and `make.exe` automatically.

## Running The Python Tests

From the repository root:

```powershell
cd tests
python -m pytest -q
```

To show the benchmark timings in the test output:

```powershell
cd tests
python -m pytest -s -q
```

Typical output looks like this:

```text
C -O2: 8.14 ms
RxJS:  49.80 ms
2 passed, 1 skipped in 1.37s
```

## What The Tests Do

The Python test harness:

- installs JavaScript dependencies in `benchmarks/` if needed
- builds the C benchmark with `make`
- runs the C and JavaScript benchmark programs
- checks the computed result for correctness
- prints timing information when run with `-s`

## Notes

- The benchmark comparison test is skipped on Windows because raw performance ordering is toolchain-dependent there.
- The `out/` directory is generated output and is gitignored.
