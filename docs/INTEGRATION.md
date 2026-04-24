# FunctionalC → TypeScript Integration Plan

Goal: ship an npm package `crxjs` that lets users write

```ts
import { of, pipe, map } from 'crxjs'
```

and get native C speedups on the compilable subset, with a clean fallback to real `rxjs` on everything else.

**Core rule:** cross the JS ↔ native boundary **once per pipeline or chunk, never per element.** This rule silently decides which operators are admissible. If a feature can't honour it, it doesn't belong in the fast path.

**Positioning:** this is a fast path for *numeric stream processing*. Not a drop-in replacement for all of RxJS. Document it that way.

---

## Phase 0 — Freeze the MVP

**Done.** See [MVP.md](MVP.md) for the authoritative v1 contract — type/arithmetic rule, operator list, JS→DSL primitive table, rejection rules, platform matrix.

- [x] MVP contract frozen in `docs/MVP.md`.
- [ ] Tick through every checkbox in `MVP.md` §§1–7 before declaring Phase 0 fully closed.

---

## Phase 1 — Kernel-Emitting Planner Backend

**v1 reuses the existing `intptr_t` codegen** (`core/planner/c_codegen.c:503`). The entry-point-only framing I originally proposed was optimistic — the registry has no float/double argument kind (`core/planner/function_registry.h:22`), and adding one is Phase 7 work. For v1 we wrap what exists.

Because `intptr_t` on 64-bit targets holds an int32 without loss, the existing backend can drive int32 pipelines directly. Float64 and `number[]` inputs fall back to `rxjs` in v1 (see `MVP.md` §1).

- [ ] Add library-mode entry point that sets `emit_main = false` and wraps the segment in a stable ABI symbol.
- [ ] Define stable int32 ABI, e.g. `crx_run_i32_pipeline(const int32_t* in, size_t len, int32_t* out, size_t* out_len)` plus scalar-return variants for `reduce` / `last` / `first`. Inputs are zero-extended to `intptr_t` on entry, results narrowed on exit.
- [ ] Drop CLI framing from `core/planner/main.c:287` for library mode.
- [ ] **Exit criterion:** generated C compiles into a `.so` / `.dylib`, a tiny C harness invokes int32 pipelines, output matches DSL-mode output bit-for-bit.

---

## Phase 2 — TypeScript Front-End (Explicit `compile()` Helper)

v1 uses an **explicit opt-in helper**, not a transparent AST plugin. The transparent plugin is the biggest schedule-risk item in the project and is not required to validate performance or correctness. Deferred to Phase 7d.

Under v1, ordinary `import { of, map, ... } from 'crxjs'` is a drop-in rxjs re-export (identical behaviour, no speedup). The fast path is opted into per call site:

```ts
import { compile, from, map, filter, reduce } from 'crxjs'

const sumEvens = compile((input: Int32Array) =>
  from(input).pipe(
    map(x => x * 2),
    filter(x => x > 2),
    reduce((a, x) => a + x, 0),
  ),
)

sumEvens(data).subscribe(result => console.log(result))
```

The factory takes the runtime input as a parameter, not a closure capture — this maps 1:1 to the kernel ABI and makes the "no captures" rule uniform from the factory down through every predicate (`MVP.md` §2, §5).

### Build-time rewrite, not runtime resolution

The extractor **rewrites each `compile(factory)` call site at build time** to a direct loader call with the kernel hash embedded as a string literal. Runtime never walks the stack, never parses source locations, never reads a manifest for identity:

```ts
// Source:
const sumEvens = compile((input: Int32Array) => from(input).pipe(...))

// After the build step (conceptually):
const sumEvens = __crxjs_loadKernel('a7f3abc...')
```

This is deliberately boring: the build step has all the information, so the runtime does none of that work. Bundling, minification, source-map shifts, and stack-shape changes cannot break the binding. The manifest exists for humans and caching — not for the runtime loader.

- [ ] Build static extractor that finds `compile((input) => ...)` (and zero-arg `compile(() => ...)`) call sites and walks the factory AST.
- [ ] TS AST lowerer converts arrow-function bodies into the DSL primitive set per the table in `MVP.md` §4.
- [ ] Un-lowerable factories produce a build error pointing at the exact JS construct (`src/foo.ts:12 — closure capture of 'K' is not supported in crxjs v1`).
- [ ] **Call-site rewrite:** each `compile(...)` call is replaced at build time with a direct loader reference carrying the AST content hash as a literal. No runtime source-location resolution.
- [ ] Runtime wrapper returns a callable `(input) => Observable` (or `() => Observable` for zero-arg factories) that invokes the pre-built kernel.
- [ ] **Exit criterion:** one TS file containing a `compile(...)` call produces a kernel at build time and runs it at runtime post-bundle; a second file with an unsupported factory fails the build with a clear location; minifying the bundled output does not break the binding. ~30 golden tests for each row of the `MVP.md` §4 mapping table.

---

## Phase 3 — Native Boundary (`packages/crxjs-native`)

**Two-tier artifact model.** One stable N-API addon (`crxjs-native.node`) built once per (platform, Node ABI). The addon's only job is `dlopen`ing plain `.so` / `.dylib` kernels, marshalling `Int32Array` buffers into the kernel's C ABI, and returning the result. Per-kernel artifacts are plain shared libraries built with `gcc -shared` — no N-API, no node-gyp, no ABI churn per kernel.

### Frozen ABI (v1)

```c
// Non-terminating pipelines (map / filter / take / skip / ...)
int32_t crx_run_i32_pipeline(
    const int32_t* input, size_t input_len,
    int32_t* output, size_t output_cap, size_t* output_len);

// Terminating pipelines (reduce / last / first)
int32_t crx_run_i32_reduce(
    const int32_t* input, size_t input_len,
    int32_t* result_out);
```

**Output buffer contract (frozen):** JS preallocates `output` at `input_len`, kernel writes up to that cap, kernel sets `*output_len` to the actual count. JS returns `output.subarray(0, *output_len)` — zero-copy narrowing, no reallocation. Over-allocation cost is one up-front `new Int32Array(input_len)` per pipeline call. Alternative ABIs (kernel-allocates, two-pass sizing) were considered and rejected for v1 (lifetime complexity and 2× pipeline cost respectively).

### Kernel path resolution (frozen, **Option A**)

The build-time extractor embeds only the AST hash into each rewritten `compile(...)` call site. At runtime, `crxjs-native.loadKernel(hash)` resolves the artifact path as:

1. `$CRXJS_CACHE_DIR/<hash>/kernel.{so,dylib}` if the env var is set.
2. Otherwise `<process.cwd()>/.crxjs/cache/<hash>/kernel.{so,dylib}`.

Rule for users: `.crxjs/cache/` is a project artifact — commit it, CI-cache it, or regenerate it in the build step. Same lifecycle as `dist/` or `.next/`. Bundler-colocated resolution (finding kernels relative to the built module) is a Phase 7 concern; for v1, cwd-based is the one frozen rule.

- [ ] Ship one prebuilt `crxjs-native.node` per platform (`linux-x64`, `darwin-arm64`).
- [ ] Addon exposes `loadKernel(hash)`, `runI32Pipeline(handle, input, outputBuffer)`, and `runI32Reduce(handle, input)`.
- [ ] Zero-copy marshalling for `Int32Array` inputs (no hidden `.slice()`).
- [ ] Output narrowing via `subarray(0, out_len)` on the JS side — no post-pipeline copy.
- [ ] Scalar return for reducing operators (`reduce`, `last`, `first`); narrowed `Int32Array` return for non-terminating pipelines.
- [ ] JS-side subscriber loop: on receiving a buffer, call `next(item)` per element, then `complete()`. On receiving a scalar, call `next(scalar)` once, then `complete()`.
- [ ] Path resolution honours `CRXJS_CACHE_DIR` with a cwd-based fallback.
- [ ] **Exit criterion:** `s01_map_reduce` through the JS API matches the raw-C number within ~2×. This proves round-trip overhead isn't eating the speedup before you build more on top.

`s01_map_reduce` is 26 µs in C — well inside round-trip-overhead territory. Measure early.

---

## Phase 4 — Public Package (`packages/crxjs`)

- [ ] Re-export the entire `rxjs` API directly. Plain `import { of, map, ... } from 'crxjs'` is a drop-in for rxjs with no speedup.
- [ ] Add the `compile(factory)` helper (Phase 2) as the single opt-in fast-path entry point.
- [ ] Ship `.d.ts` mirroring the RxJS shapes you support plus the `compile` signature.
- [ ] **Exit criterion:** one package, one mental model: `import from 'crxjs'` == rxjs, `compile(...)` == native fast path. A user can mix both in the same file with identical `.subscribe(next, complete)` semantics.

---

## Phase 5 — Build Tooling & Caching (`packages/crxjs-compiler`)

- [ ] Hash every kernel by: extracted factory AST, **planner git SHA**, **codegen version**, compiler + flags, platform triple. **No Node ABI in the hash** — kernels are plain shared libraries, not addons. (Node ABI only applies to the one `crxjs-native.node`.)
- [ ] Emit kernels deterministically to `<project-root>/.crxjs/cache/<hash>/kernel.{so,dylib}` at build time.
- [ ] Emit a manifest (`.crxjs/cache/manifest.json`) recording artifact hashes, source locations, and stage counts — **for humans and CI cache tooling only**. The runtime does not read it for binding identity.
- [ ] Runtime binding: the hash literal embedded by Phase 2 is passed to `crxjs-native.loadKernel(hash)`, which resolves the path per Phase 3's rule (`CRXJS_CACHE_DIR` → `<cwd>/.crxjs/cache/<hash>/`). No manifest lookup, no stack walking.
- [ ] Pick one bundler integration first (Vite or esbuild) to wire the extractor into the build. Others later.

---

## Phase 6 — Prove The Speedup End-to-End

Extend `tests/bench/benchmark_matrix.py` to measure the full JS call path, not just the kernel.

- [ ] Add `crxjs` column alongside `rxjs`, `raw_c`, `library_c`, `planner_c`.
- [ ] **Exit criterion:** `s01_map_reduce`, `s02_map_filter_reduce`, `s11_map_reduce_x1000_items` through the JS API still beat RxJS materially. All three are **terminating** (reduce-ended) so the JS-side emission loop is a single `next` call, and the measurement reflects compute cost only.

Deliberately **not** in the v1 gate:

- `s12_chain_10000_x1000_items` — non-terminating pipeline over 1M elements. Under v1's Option 1 emission, the JS-side loop adds ~20–50 ms, comparable to RxJS's 20 ms. Not a credible "must beat RxJS" gate until chunked streaming lands. Moved to Phase 7b exit gate.
- `s14`, `s20`, `s25` — zip-based, tracked as the "Phase 7c headline claim."

---

## Phase 7 — Careful Expansion

Only after the numeric synchronous path is stable. Each item below is its own project, not a bundle.

- [ ] **7a. Typed numeric backend.** Add `RX_ARG_INT32` and `RX_ARG_FLOAT64` to `function_registry.h`, fork codegen paths to emit `int32_t` / `double` instead of `intptr_t`. Unlocks `Float64Array` and `number[]` on the compiled path.
- [ ] **7b. Chunked streaming kernel.** Resumable codegen: save state (iterator position, scan accumulator, filter counters) on yield, restore on resume. Roughly 1.5–3× win on cache-bound large-N pipelines, big memory-footprint improvement, enables streaming backpressure. **Exit criterion:** `s12_chain_10000_x1000_items` beats RxJS materially. 2–3 weeks of focused C work.
- [ ] **7c. `zip` and planner-backed multi-source cases.** **Exit criterion:** `s14`, `s20` through the JS API reproduce the README's zip-class speedups.
- [ ] **7d. Transparent Babel/SWC plugin.** Removes the explicit `compile()` opt-in for eligible pipelines. `import { of, map, ... } from 'crxjs'` auto-accelerates where possible. Ship build-time report (`crxjs: compiled 42 pipelines, fell back on 3 at src/foo.ts:12`) and optional `{ strict: true }` mode.
- [ ] **7e. ESM / dual-package.** Swap the CJS `require(...)` loader for a platform-aware import path.
- [ ] **7f. Bundler-colocated kernel resolution.** v1 resolves kernels via `CRXJS_CACHE_DIR` → `<cwd>/.crxjs/cache/` (Option A). Phase 7f adds bundler plugins (Vite `assetsInclude`, esbuild `file` loader, Webpack `asset/resource`) that copy kernels alongside the built module and resolve via `__dirname`. Makes built bundles self-contained without env vars. One plugin at a time.
- [ ] Ternary `? :` and unary minus in compiled predicates (needs new DSL AST nodes).
- [ ] Additional prebuilt targets: `linux-arm64`, `darwin-x64`, `win32-x64`.

**Explicitly out of scope for this project** (treat as separate projects, not "a few more operators"):

- `mergeMap` — requires recursively lifting inner observables into C; not compatible with the "cross once" rule without its own compile pass.
- Full RxJS scheduler behaviour.
- Hot observables, subjects.
- Browser support.
- Object / string pipelines with the same speedup story.
- Arbitrary JS closures over runtime state.

---

## Build Order (First Slice)

1. [ ] Kernel-emitting planner backend in `core/planner` (Phase 1), int32 only, emits plain `.so` / `.dylib`.
2. [ ] Stable `crxjs-native.node` addon that `dlopen`s kernels and marshals `Int32Array` (Phase 3).
3. [ ] JS-side subscriber loop that emits per-element `next()` from the returned buffer (Phase 3).
4. [ ] `rxjs` re-export under the `crxjs` package (Phase 4).
5. [ ] Explicit `compile(factory)` helper + static extractor. Identity key: AST content hash embedded as a literal at each call site. Source location is reported for errors but is not part of identity (Phase 2).
6. [ ] Kernel cache + manifest + one bundler integration (Phase 5).
7. [ ] Support `of` / `from(Int32Array)` + `map` / `filter` / `reduce` / `last` through `compile()`.

---

## Effort Estimate

| Milestone | Duration |
|---|---|
| v1 MVP (int32 path, explicit `compile()`, stable addon + plain-`.so` kernels, CJS, linux-x64 + darwin-arm64 prebuilts) | 2–4 weeks |
| Phase 7a — typed numeric backend (`Float64Array` + `number[]` compiled) | +1–2 weeks |
| Phase 7b — chunked streaming kernel (resumable state machine) | +2–3 weeks |
| Phase 7c — `zip` + headline zip benchmarks achievable via JS | +2–3 weeks |
| Phase 7d — transparent Babel/SWC plugin (removes `compile()` opt-in) | +2–3 weeks |
| Polish (ESM support, extra platforms, docs) | +1–2 weeks |

**Total: 2–4 weeks to shippable v1** with the explicit-compile path. The transparent-plugin UX and the zip-class headline numbers are both in Phase 7 and together add another 4–6 weeks.
