---
status: implemented
tags: [profiling, performance, tooling]
---

# CPU sampling profiler

## Problem

[Profiling and tracing](Profiling%20and%20tracing.md) covers the
instrumented, per-frame timeline story (Chrome Tracing done, Tracy
planned). That story answers *"why was frame 4723 slow?"*.

It does **not** answer *"across a 5-minute play session, where is the
engine actually spending CPU cycles — inside Box2D? inside the batch
renderer's sort? inside Lua's GC?"*. That question needs a **statistical
sampling profiler** with a flame graph UI, not instrumentation.

Attempting to wire this up revealed a concrete tooling dead end. The
current unstaged `devenv.nix` adds `perf`, `flamegraph`, `pprof`, and
scripts `game-perf-record`, `game-perf-stat`, `game-pprof`. The
`game-pprof` script is a stub that fails because:

1. `pprof` (the Google UI) does not natively read Linux `perf.data`.
2. The official bridge is
   [google/perf_data_converter](https://github.com/google/perf_data_converter),
   which provides the `perf_to_profile` binary.
3. `perf_data_converter` **does not build with GCC 15** (our devenv
   GCC) — abseil-cpp compile errors.
4. It is **currently broken in nixpkgs** — no working derivation.
5. **Vendoring is painful** — it builds with Bazel, not CMake, and
   pulls in abseil, protobuf, and LLVM's DWARF libraries.

So `game-perf-record` + `flamegraph.pl` works (produces
`build/flamegraph.svg`), but the pprof web UI — with its call graph,
focus/ignore filters, source annotation, and top view — does not.

This doc inventories the options and picks one (or a small combo) to
actually commit to.

## Goals

1. **Real-time speed** — the profiler must not slow the engine more
   than a few percent. Rules out Callgrind.
2. **Flame graph + call graph UI** — flame graph alone is not enough;
   need an interactive call graph with filters for serious analysis.
3. **Source annotation** — click a function, see which lines sampled.
4. **Works today on NixOS** — no "wait for upstream" dependencies.
5. **Minimal vendoring** — consistent with the engine's philosophy.
   Prefer tools already in devenv or one-line nixpkgs additions.
6. **Works on the standard `Game` binary** — no separate build target,
   no global `#define`, no code changes.

Non-goals (covered elsewhere): per-frame timeline (Chrome Tracing /
Tracy), GPU profiling (RenderDoc, Tracy GPU zones), memory profiling
(Tracy, heaptrack), deterministic instruction counting (Callgrind).

## Options

### Option A — gperftools + pprof (already in devenv)

gperftools' CPU profiler is a statistical sampler that writes **pprof
format natively**. No converter needed. Both `gperftools` and `pprof`
are already in `devenv.nix`.

Workflow:

```sh
CPUPROFILE=build/cpu.prof CPUPROFILE_FREQUENCY=997 \
  LD_PRELOAD=$(nix eval --raw nixpkgs#gperftools)/lib/libprofiler.so \
  ./build/game run assets -- testBenchmark
pprof -http :8080 ./build/game build/cpu.prof
```

Or programmatic control via `ProfilerStart()` / `ProfilerStop()` from
`<gperftools/profiler.h>`, gated on an F-key.

**Pros**
- Zero vendoring — already in devenv.
- Writes pprof format directly — **bypasses the perf_data_converter
  problem entirely**.
- The pprof web UI is the feature the user actually wanted: flame
  graph, call graph, top view, peek, source, disassembly.
- Programmatic `ProfilerStart`/`ProfilerStop` gives us scoped captures
  (e.g. profile only the Lua `update` call, only the render phase).
- Symbolization works out-of-the-box on unstripped binaries.

**Cons**
- Uses a `SIGPROF` timer; sample rate capped around 1 kHz (versus perf
  at 4-10 kHz+). Less resolution for micro-optimization.
- Linux perf's hardware counter support (branch misses, cache misses,
  LBR call stacks) is not available — gperftools is walltime only.
- `SIGPROF` can interact poorly with libraries that swallow signals.
  Fine for our SDL/OpenGL stack in practice.
- Requires linking `-lprofiler` OR using `LD_PRELOAD`. LD_PRELOAD is
  simpler and leaves the binary unchanged.

**Effort** — ~30 minutes. Replace the broken `game-pprof` script with
one that runs the game under `LD_PRELOAD=libprofiler.so`, deletes the
perf.data pipeline, and opens pprof directly.

### Option B — samply (perf under the hood, Firefox Profiler UI)

[`samply`](https://github.com/mstange/samply) is a Rust sampling
profiler that uses `perf_event_open` on Linux and uploads results to
the Firefox Profiler web UI (runs locally, no data leaves the machine).
**Packaged in nixpkgs.**

```sh
samply record ./build/game run assets -- testBenchmark
# Automatically opens the Firefox Profiler with the capture loaded.
```

**Pros**
- Single command. No converter. No extra scripts to maintain.
- Firefox Profiler UI is excellent: flame graph, inverted call tree,
  stack chart, marker table, source view, assembly view, per-thread
  timelines, filtering, diffing.
- Reuses the kernel's perf infrastructure — same sample quality as
  `perf record` (high rate, DWARF unwinding, LBR when available).
- Supports attaching to a running process (`samply record --pid`).
- Works with **unmodified release binaries**.
- Actively maintained, cross-platform (same workflow on macOS).

**Cons**
- One more package in devenv (~20MB).
- Firefox Profiler UI is served from a CDN by default; `samply` has a
  `--save` flag to produce a local `.json.gz` for offline viewing.
- Still needs `perf_event_paranoid` relaxed (same as plain `perf
  record` — already required by the current script).

**Effort** — ~10 minutes. Add `samply` to `devenv.nix`, add a
`game-samply` script.

### Option C — hotspot (KDAB's perf GUI)

[`hotspot`](https://github.com/KDAB/hotspot) is a Qt GUI that reads
`perf.data` **directly**. No `perf_data_converter`, no pprof.
**Packaged in nixpkgs** (`pkgs.hotspot`).

```sh
perf record -g --call-graph dwarf -F 997 -- ./build/game run assets
hotspot perf.data
```

**Pros**
- Reads `perf.data` directly — **keeps the existing `game-perf-record`
  script and just replaces the pprof viewer step**.
- Flame graph, top-down tree, bottom-up tree, caller/callee view,
  source annotation, disassembly — feature parity with pprof.
- Written by KDAB specifically to replace `perf report` + pprof on
  Linux. Strong for native code.
- Integrates off-CPU analysis, tracepoints, hardware counters.
- No network access, local-only.

**Cons**
- Qt GUI — drags in KDE libraries (~100-200MB closure). Heaviest
  option.
- Desktop app, not web — no easy sharing of captures with others.
- UI is dense; learning curve steeper than pprof's web view.

**Effort** — ~5 minutes. Add `hotspot` to `devenv.nix`, replace
`game-pprof` with `game-hotspot` that just runs `hotspot
build/perf.data`.

### Option D — perf report + FlameGraph (keep what works)

The current unstaged scripts produce `build/flamegraph.svg` via
`perf script | stackcollapse-perf.pl | flamegraph.pl`. That pipeline
**already works**. `perf report -i build/perf.data` also works as a
TUI call graph viewer.

**Pros**
- Zero new dependencies beyond what's already unstaged.
- `perf report` is a full interactive TUI: call tree, annotate,
  source, hot lines.
- FlameGraph SVGs are sharable, embed anywhere, zoom/search in the
  browser.

**Cons**
- No unified GUI that combines flame graph + call graph + source
  annotation the way pprof / Firefox Profiler / hotspot do.
- FlameGraph SVGs are read-only — no filtering, no diffing.
- `perf report` TUI is powerful but not a flame graph.

**Effort** — zero. Just drop the broken `game-pprof` script.

### Option E — Vendor perf_data_converter and fix GCC 15 build

What the unstaged script's TODO implies. Would unblock the full pprof
workflow on top of perf.data.

**Pros**
- The "correct" answer to what the user originally wanted.

**Cons**
- Bazel build — would need to either (a) keep Bazel as a new devenv
  dependency, or (b) rewrite to CMake (non-trivial — abseil, protobuf,
  LLVM DWARF deps).
- GCC 15 abseil compile errors need a real fix or a pin to an older
  GCC for just this one tool.
- Upstream may fix it eventually. Investing engineering time now is
  speculative.
- **Violates goal 5** (minimal vendoring).

**Effort** — days to weeks, most of it build-system yak shaving.

### Option F — Tracy's built-in sampling profiler

Tracy (Phase 2 in [Profiling and tracing](Profiling%20and%20tracing.md))
has a kernel-level sampling profiler built in when run with
`CAP_SYS_ADMIN` or as root. It combines with Tracy's zone
instrumentation in the same viewer.

**Pros**
- Unifies sampling + instrumented zones + GPU zones + memory + locks
  in one view. Best single-tool answer long-term.
- No separate workflow — same `game-tracy` command.

**Cons**
- **Blocked on Phase 2** of the Profiling and tracing plan (vendoring
  Tracy). We want CPU sampling *now*, not after that lands.
- Requires root or capabilities to enable the sampler.

**Effort** — whatever Tracy Phase 2 costs. Out of scope for this
short-term decision.

## Comparison

| Option | Already in devenv? | New deps | Call graph UI | Source view | Effort |
|---|---|---|---|---|---|
| A. gperftools + pprof | **Yes** | none | pprof web | yes | ~30m |
| B. samply | no | +samply (~20MB) | Firefox Profiler | yes | ~10m |
| C. hotspot | no | +hotspot (~150MB Qt) | hotspot Qt | yes | ~5m |
| D. perf report + FlameGraph | almost (unstaged) | none | `perf report` TUI | via `perf annotate` | 0 |
| E. vendor perf_data_converter | no | Bazel, abseil, protobuf | pprof web | yes | days |
| F. Tracy sampling | no (Phase 2) | Tracy (already planned) | Tracy viewer | yes | blocked |

## Decision

**Adopted Option B (samply).** Dropped the unstaged perf /
FlameGraph / pprof scripts entirely.

The devenv now exposes a single `game-samply` script that records the
engine running `testBenchmark` under `samply record --save-only`,
writes `build/samply.json.gz`, and opens it in the Firefox Profiler
via `samply load`. Packages `flamegraph`, `graphviz`, `perf`, and
`pprof` were removed; `samply` was added.

### Rationale

- **One tool, one command.** `samply record -- ./build/game ...`
  replaces the whole `perf record` → `perf script` →
  `stackcollapse-perf.pl` → `flamegraph.pl` → `perf_to_profile` →
  `pprof` pipeline. No converter means no perf_data_converter dead
  end.
- **Firefox Profiler UI is excellent** — flame graph, inverted call
  tree, stack chart, marker table, source view, assembly view,
  per-thread timelines, filtering, diffing. Everything we wanted from
  pprof, plus better per-thread visualization.
- **Unmodified release binary.** No `LD_PRELOAD`, no `-lprofiler`
  link, no `#define`. Keeps the engine build flags unchanged, and the
  same workflow will work once we add programmatic control in the
  future.
- **Reuses the kernel perf infrastructure** — same sample quality as
  plain `perf record` (DWARF unwinding, high sample rate). The
  `perf_event_paranoid` sysctl needs to allow user sampling, which
  the machine is already configured for.
- **Cross-platform.** Same workflow on macOS if/when we get a mac
  devenv.
- **Shareable captures.** `build/samply.json.gz` is a portable
  artifact — can be attached to bug reports and diffed across commits.

### Rejected alternatives

- **Option A (gperftools + pprof)** — viable, but uses `SIGPROF` and
  the pprof UI, both of which samply supersedes. gperftools stays in
  devenv because it's still the cleanest path for programmatic
  scoped captures (`ProfilerStart`/`ProfilerStop`) if we want them
  later from Lua.
- **Option C (hotspot)** — ~150MB of Qt for a tool we'd run
  occasionally.
- **Option D (perf + FlameGraph)** — removed. No longer worth
  maintaining two CPU workflows; samply's UI subsumes the static SVG.
- **Option E (vendor perf_data_converter)** — yak shave, no longer
  relevant.
- **Option F (Tracy sampling)** — still the long-term answer, but
  gated on Tracy Phase 2 of [Profiling and tracing](Profiling%20and%20tracing.md).
  samply fills the gap cleanly in the meantime.

## Open questions

- **Scripted captures from Lua?** Worth it only if aggregate captures
  across a whole run turn out to be too noisy. Defer until then.
- **CI integration?** Could run `testBenchmark` under the profiler in
  CI and fail on regressions in specific symbols. Out of scope until
  we have a regression to catch.
- **Symbol quality for Lua frames?** gperftools shows Lua's C stack
  (lua_pcall, luaV_execute) but not Lua-level function names. This is
  a fundamental limitation of sampling a non-JITed interpreter.
  LuaJIT + its built-in profiler is the answer; see
  [LuaJIT Migration](LuaJIT%20Migration.md).
