# crxjs v1 MVP Contract

This document freezes the v1 scope. It is the answer to Phase 0 of [INTEGRATION.md](INTEGRATION.md). Anything not listed here is out of scope for v1 and should not block the release.

**Positioning:** drop-in replacement for RxJS for *numeric synchronous cold pipelines*. Everything else falls back to real `rxjs` with identical semantics.

**Core rule (non-negotiable):** cross the JS ↔ native boundary **once per pipeline**, never per element.

---

## 1. Type & Arithmetic Rule

The current C backend emits `intptr_t` throughout (`core/planner/c_codegen.c:503`) and the registry has no float/double argument kind (`core/planner/function_registry.h:22`). A typed numeric backend is a prerequisite for `Float64Array` and is explicitly deferred (see §8 non-goals and `INTEGRATION.md` Phase 7).

**v1 compiled path accepts `Int32Array` only.** The two code paths have *different* handling of unsupported types:

| Input | Inside `compile(...)` | Via plain `import from 'crxjs'` (no `compile()`) |
|---|---|---|
| `Int32Array` | **Compiled** — zero-copy, int32 arithmetic, overflow wraps (C semantics, documented) | rxjs re-export, no speedup |
| `of(intLiteral, ...)` | **Compiled** — `of(1, 2, 3)` ✓ ; `of(1.5, 2)` ✗ (build error) | rxjs re-export, no speedup |
| `Float64Array` | **Build error** — "Float64Array is not supported in crxjs v1; use rxjs directly or wait for Phase 7a" | rxjs re-export, no speedup |
| `number[]` | **Build error** — "number[] is not supported in crxjs v1; pass an Int32Array, or use rxjs directly" | rxjs re-export, no speedup |

The rule is uniform: **`compile()` is strict**, because the user explicitly opted into native compilation. Any unsupported construct in that path is a build error with a pointer at the exact source location. The plain rxjs re-export accepts anything RxJS accepts.

- [ ] Confirm `Int32Array` is the only input that hits the compiled path in v1.
- [ ] Confirm zero-copy is verified for `Int32Array` inputs (no hidden `.slice()`).
- [ ] Confirm `Float64Array` and `number[]` inside `compile(...)` produce build errors with helpful messages.

---

## 2. Invocation API

crxjs ships two entry points in one package:

- **Drop-in path** — ordinary `import { of, map, ... } from 'crxjs'` is a re-export of `rxjs`. Identical behaviour, no speedup. Users migrating an existing codebase change one import path and nothing else breaks.
- **Fast path** — `compile(factory)` is an **explicit opt-in** that compiles the factory to a native kernel at build time. The factory is a function from input buffer to observable pipeline; the compiled result is a reusable function from input to an Observable-shaped handle with matching `.subscribe(next, complete)` semantics.

```ts
import { compile, from, map, filter, reduce } from 'crxjs'

const sumEvens = compile((input: Int32Array) =>
  from(input).pipe(
    map(x => x * 2),
    filter(x => x > 2),
    reduce((acc, x) => acc + x, 0),
  ),
)

sumEvens(data).subscribe(result => console.log(result))
sumEvens(otherData).subscribe(result => console.log(result)) // same kernel, different input
```

### Why the factory takes an input parameter

The factory is **not** a closure. All runtime data flows in through its parameter, and the body of the factory is a static description of the pipeline. This has three consequences that matter:

- **No captures anywhere.** The "no closure captures" rule (§5) applies uniformly from the factory down to every predicate. There is no special case where the factory is allowed to close over variables but the predicates aren't. Cleaner rule, easier to enforce, easier to explain.
- **One compiled kernel, many invocations.** The same native `.so` / `.dylib` is reused across calls — no per-input recompilation, no per-input cache pollution.
- **The factory signature is the kernel ABI.** `(input: Int32Array) => Observable<number>` maps 1:1 onto the C entry point `crx_run_i32_pipeline(const int32_t* in, size_t len, ...)`. Nothing hidden.

Zero-arg factories are also valid for literal sources:

```ts
const first100Squares = compile(() =>
  range(1, 100).pipe(map(x => x * x), reduce((a, x) => a + x, 0)),
)
first100Squares().subscribe(...)
```

### Why explicit in v1

A transparent Babel/SWC plugin is the single biggest schedule-risk item in the project, and is not required to prove performance or correctness. Explicit `compile()` ships faster, is trivially debuggable (the user sees exactly what compiled), and matches naturally onto the existing DSL/transpiler. A transparent plugin comes later (`INTEGRATION.md` Phase 7d) and removes the opt-in for eligible pipelines.

### Per-element subscriber semantics

1. The native kernel runs the **entire pipeline to completion inside C**. The JS ↔ native boundary is crossed exactly twice per pipeline: once entering, once exiting.
2. The kernel returns a result buffer (or scalar for reducing operators).
3. The crxjs subscriber wrapper then emits per element from JS by looping over that buffer and calling `next(item)` for each one, followed by `complete()`.

The compute is batched; the emission is not. From the subscriber's point of view, behaviour matches RxJS exactly.

### Cost of per-element emission

The JS-side emission loop adds roughly 20–50 ns per element (one user-function call through V8, no native boundary crossing). For the current benchmark suite:

| Benchmark | RxJS | Raw C | Compiled crxjs (estimated) |
|---|---|---|---|
| `s01_map_reduce` (small, terminating) | 3.5 ms | 0.03 ms | ~0.03 ms (reduce emits once) |
| `s11_map_reduce_x1000_items` (1M items, terminating) | 1662 ms | 29 ms | ~29 ms (reduce emits once) |
| `s02_map_filter_reduce` (small, terminating) | 5.3 ms | 0.06 ms | ~0.06 ms (reduce emits once) |
| `s12_chain_10000_x1000_items` (non-terminating, 1M elements) | 20 ms | 0.0001 ms | ~20–50 ms (emission-bound, **not a v1 gate**) |

Reducing pipelines are unaffected because they emit one scalar. Non-terminating pipelines over very large N become emission-bound; this is the expected v1 cost and the reason `s12` is excluded from the Phase 6 proof gate and deferred to Phase 7b (chunked streaming). See [INTEGRATION.md](INTEGRATION.md) Phase 7b for the streaming kernel that reclaims most of that cost.

- [ ] Confirm `.subscribe(next, complete)` calls `next` per element, not per batch.
- [ ] Confirm `complete` is called once at end.
- [ ] Document emission-loop cost publicly so users aren't surprised.

---

## 3. Supported Operators

Sources: `of`, `from`, `range`.
Operators: `map`, `filter`, `reduce`, `scan`, `take`, `skip`, `takeWhile`, `skipWhile`, `last`, `first`, `distinctUntilChanged`.

Anything else falls through to `rxjs` and does not get the speedup.

- [ ] Every operator above has a corresponding entry in `core/planner/function_registry.c` and `core/tools/operator_registry.c` (they do — this is a sanity check, not new work).

---

## 4. JS → DSL Primitive Mapping Table

The complete list of JS expressions admissible inside `map` / `filter` / `scan` / `reduce` / `takeWhile` / `skipWhile` / `distinctUntilChanged` arrow bodies.

Primitive names below must match the strings recognised by the current DSL codegen. The DSL AST only supports identifier, number, and call expressions (`core/tools/dsl_ast.h:6`), so *every* row below is lowered to an S-expression call form like `mul(x, x)`, not a JS operator node.

### Arithmetic
| JS | DSL primitive |
|---|---|
| `a + b` | `plus(a, b)` |
| `a - b` | `minus(a, b)` |
| `a * b` | `mul(a, b)` |
| `a / b` | `div(a, b)` |
| `a % b` | `mod(a, b)` |

### Comparison (strict only)
| JS | DSL primitive |
|---|---|
| `a === b` | `eq(a, b)` |
| `a !== b` | `neq(a, b)` |
| `a < b` | `lt(a, b)` |
| `a > b` | `gt(a, b)` |
| `a <= b` | `lte(a, b)` |
| `a >= b` | `gte(a, b)` |

### Logical
| JS | DSL primitive |
|---|---|
| `a && b` | `and(a, b)` |
| `a \|\| b` | `or(a, b)` |
| `!a` | `not(a)` |

### Control
| JS | Form |
|---|---|
| `return expr` | implicit — arrow body tail expression is the return |

### Literals
| JS | Accepted |
|---|---|
| Integer literals (`0`, `42`, `-7`) | ✅ |
| `true` / `false` | ✅ (lowered to `1` / `0`) |

### Explicitly excluded in v1 (pipeline falls back to `rxjs`)
- Unary minus `-a` — no AST node, use `minus(0, a)` inline only if your lowerer rewrites it; otherwise reject.
- Ternary `cond ? a : b` — no AST node in current DSL. Deferred.
- Float literals (`1.5`, `-3.14`) — compiled path is int32 only in v1.
- `Math.*` (all of it)
- Bitwise: `& | ^ ~ << >> >>>`
- Loose equality: `==` `!=`
- String operations, template literals
- Method calls of any kind (`x.toFixed(2)`, etc.)
- Property access except the arrow parameter itself
- `typeof`, `instanceof`
- Array / object literals
- Spread / rest
- Closure captures of any kind (see §5)

- [ ] Golden-test file listing every row above with expected DSL IR output.
- [ ] Golden-test file listing every excluded construct with expected compile-error output.

---

## 5. Rejection Rules

Any of these inside a `compile(...)` call cause a **hard build-time error** pointing at the exact source location. There is no silent fallback — the user explicitly asked for native compilation.

- [ ] Closure captures of any kind (including by the factory): `const K = 2; compile((x) => from(x).pipe(map(v => v * K)))` → rejected.
- [ ] Non-literal `reduce` / `scanfrom` seeds: `reduce(fn, someVar)` → rejected.
- [ ] Predicate referencing anything other than its own parameters.
- [ ] Any use of excluded constructs from §4.
- [ ] Factory input type is not `Int32Array` (e.g. `Float64Array`, `number[]`, custom types) → rejected with a message directing the user to rxjs directly or to Phase 7a.
- [ ] Async anything (`async` arrows, `await`, returning a Promise).

Because v1 uses an **explicit** `compile()` opt-in (not a transparent plugin — see §2), rejection is a **hard build error** at the specific `compile()` call site. The user explicitly asked for native compilation; silently falling back would defeat the purpose of the opt-in.

Example rejection output:

```
crxjs: build error
  src/pipelines.ts:14  in compile(...)
  └─ closure capture of 'K' is not supported in crxjs v1
     move the value inline, or use rxjs directly on this pipeline

src/analytics.ts:42  in compile(...)
  └─ Math.abs is not supported in crxjs v1
```

If the user wants `rxjs` semantics for any pipeline, they simply don't wrap it in `compile()` — ordinary `import { of, map, ... } from 'crxjs'` is a drop-in rxjs re-export.

---

## 6. Build Output & Manifest

Every build that contains `compile(...)` calls produces:

1. A set of plain shared-library kernels under `.crxjs/cache/<hash>/kernel.so` (or `.dylib`).
2. A manifest `.crxjs/cache/manifest.json` mapping source locations to artifact hashes.
3. A short success report:

```
crxjs: compiled 3 pipelines
  src/pipelines.ts:14  → .crxjs/cache/a7f3.../kernel.so  (14 stages, 412 bytes)
  src/analytics.ts:42  → .crxjs/cache/b219.../kernel.so  ( 6 stages, 288 bytes)
  src/ranking.ts:88    → .crxjs/cache/c845.../kernel.so  ( 9 stages, 336 bytes)
```

Identity is by source location + AST content hash. If the extractor can cheaply derive a label from the surrounding `const` binding (e.g. `const sumEvens = compile(...)`), it may surface that alongside the location in the report — but it is never part of the API.

The transparent-plugin build-time report (showing which un-wrapped pipelines *would* have compiled) is a Phase 7d concern, not v1.

- [ ] Manifest format finalised (stable schema — users may want to check artifacts into CI caches).
- [ ] Success report format finalised.

---

## 7. Target Platforms & Module System

v1 prebuilt binaries:

- [ ] `linux-x64`
- [ ] `darwin-arm64`

Deferred (not in v1): `linux-arm64`, `darwin-x64`, `win32-*`.

v1 module system:

- [ ] **CommonJS only.** One stable `crxjs-native.node` addon is resolved via `require`; it then loads per-kernel plain `.so` / `.dylib` files via `dlopen`. ESM (`await import(...)`) and dual-package support are deferred.
- [ ] Min Node: **20 LTS** (N-API 9).

---

## 8. Explicit Non-Goals

These are **not bugs** in v1. Do not accept issues asking for them until a separate project is scoped:

- **Transparent Babel / SWC plugin.** v1 is explicit `compile(factory)` only. Auto-accelerating ordinary `import`ed pipelines is Phase 7d.
- **Typed numeric backend** (`Float64Array`, `number[]`, float literals). Current codegen emits `intptr_t` only; float path requires a new `RxArgumentType` and a forked codegen. Phase 7a.
- **Chunked streaming kernel.** v1 is batched compute + JS-side per-element emission. Chunked streaming is a ~1.5–3× win on cache-bound large-N pipelines and a big memory win, but requires a resumable state-machine codegen. Phase 7b.
- `mergeMap`, `switchMap`, `concatMap`, `exhaustMap`.
- `zip` — Phase 7, not v1.
- `buffer`, `throttleTime`, `debounceTime`, `delay`, any scheduler-aware operator.
- Hot observables, `Subject`, `BehaviorSubject`, `ReplaySubject`.
- Async sources: `fromEvent`, `fromPromise`, `interval`, `timer`.
- Object or string element types in compiled pipelines.
- Closure captures, even of constants.
- Ternary `? :` and unary minus inside compiled predicates.
- ESM dual-package / `await import()` loader path.
- Browser support.
- Source maps from runtime C errors back to TS lines.

All of the above are handled by the `rxjs` fallback path with identical semantics. They just don't get the speedup.

---

## 9. Phase 0 Exit Criteria

Phase 0 is complete when:

- [ ] Every checkbox in §§1–7 is ticked.
- [ ] A one-file code example demonstrating a compiled and a fallback pipeline compiles the compiled one and runs both under identical `.subscribe` semantics. (This file becomes the first integration test.)
- [ ] This document is reviewed and the operator list, mapping table, and rejection rules are considered frozen for v1. Changes after this point require an explicit amendment entry below.

---

## Amendments

Append-only. Date, decision, reason.
