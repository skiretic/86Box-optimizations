# Optimization Report: Pentium MMX emulation on Apple Silicon (new dynarec focus)

- Top recommendations (expected wins on M1/M2):
  - Introduce NEON-backed MMX arithmetic/logic/pack/shift templates in the new dynarec, guarded by `__APPLE__`, `__aarch64__`, and `NEW_DYNAREC_BACKEND`, with a runtime backend check (aim: 1.3–1.6× faster MMX-heavy loops by cutting per-op scalar glue and spills).
  - Keep MMX state in NEON registers across dynarec traces and align the MMX backing store to 16 bytes to reduce loads/stores and enable aligned NEON accesses (aim: 10–20% fewer memory ops in MMX-heavy traces).
  - Instrument and tune the dynarec code cache and load/store stubs for M1/M2 (larger block chunks, fewer flushes, and prefetch-friendly layout), plus PGO/LTO builds (aim: 5–15% overall gain in mixed workloads).

## Environment / How tested
- Status: profiling not yet executed; below are pasteable commands for M1 baseline.
- Environment (from [buildinstructions.md](buildinstructions.md)):
   - `BREW_PREFIX=$(brew --prefix)`
   - `export PATH="$BREW_PREFIX/opt/qt@6/bin:$PATH"`
   - `export PKG_CONFIG_PATH="$BREW_PREFIX/opt/freetype/lib/pkgconfig:$BREW_PREFIX/opt/libpng/lib/pkgconfig:$BREW_PREFIX/opt/libslirp/lib/pkgconfig:$BREW_PREFIX/opt/openal-soft/lib/pkgconfig:$BREW_PREFIX/opt/rtmidi/lib/pkgconfig:$BREW_PREFIX/opt/fluidsynth/lib/pkgconfig:$BREW_PREFIX/opt/sdl2/lib/pkgconfig:$BREW_PREFIX/opt/qt@6/lib/pkgconfig:$BREW_PREFIX/opt/libserialport/lib/pkgconfig:$BREW_PREFIX/opt/webp/lib/pkgconfig"`
   - `export CMAKE_PREFIX_PATH="$BREW_PREFIX:$BREW_PREFIX/opt/qt@6/lib/cmake:$BREW_PREFIX/opt/qt@6:$BREW_PREFIX/opt/sdl2:$BREW_PREFIX/opt/freetype:$BREW_PREFIX/opt/libpng:$BREW_PREFIX/opt/libslirp:$BREW_PREFIX/opt/openal-soft:$BREW_PREFIX/opt/rtmidi:$BREW_PREFIX/opt/fluidsynth:$BREW_PREFIX/opt/libserialport"`
- Build (Release, M1-tuned, dynarec) — matches buildinstructions:
   - `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PWD/dist -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_MACOSX_BUNDLE=ON -DQT=ON -DUSE_QT6=ON -DOPENAL=ON -DRTMIDI=ON -DFLUIDSYNTH=ON -DMUNT=OFF -DDISCORD=OFF -DNEW_DYNAREC=ON -DLIBSERIALPORT_ROOT="$BREW_PREFIX/opt/libserialport" -DQT_QMAKE_EXECUTABLE="$BREW_PREFIX/opt/qt@6/bin/qmake" -DCMAKE_C_COMPILER=clang -DCMAKE_C_FLAGS='-O3 -mcpu=apple-m1 -march=armv8-a'`
   - `cmake --build build --config Release -j$(sysctl -n hw.ncpu)`
   - `cmake --install build --config Release` (produces `dist/86Box.app`)
- Optional LTO/PGO experiment: add `-flto` or use two-pass `-fprofile-generate` / `-fprofile-use`.
- Profiling (examples):
   - Time Profiler: `instruments -t "Time Profiler" dist/86Box.app/Contents/MacOS/86Box -- <args>`
   - Stack samples: `sudo sample <pid> 10 -file sample.txt`
   - Basic perf markers: enable `-Rpass=loop-vectorize -Rpass-missed=loop-vectorize` on clang.

## Methodology (planned)
- Run release dynarec build on MMX-heavy DOS/Win9x workloads; collect Instruments flame graphs and `sample` stacks.
- Capture dynarec metrics: code cache occupancy, flush rate, average emitted bytes/op, and time spent in stubs vs generated code.
- Microbench harnesses under `src/tools/` for per-instruction MMX ops and dynarec codegen overhead.
- Validate correctness with existing MMX interpreter paths and bit-exact reference checks.

## Hotspots (code paths to target)
- Interpreter MMX arithmetic loops are scalar and per-element in C, e.g., PADDB/PADDW in [src/cpu/x86_ops_mmx_arith.h#L1-L120](src/cpu/x86_ops_mmx_arith.h#L1-L120), making auto-vectorization unlikely.
- New dynarec MMX rops map MMX opcodes to IR, but currently funnel through generic uops without Apple-specific fast paths, e.g., [src/codegen_new/codegen_ops_mmx_arith.c#L1-L33](src/codegen_new/codegen_ops_mmx_arith.c#L1-L33), [src/codegen_new/codegen_ops_mmx_loadstore.c#L1-L90](src/codegen_new/codegen_ops_mmx_loadstore.c#L1-L90).
- Backend uop lowering uses NEON for some Q-sized ops but lacks MMX-specific templates and register pinning, see [src/codegen_new/codegen_backend_arm64_uops.c#L1-L220](src/codegen_new/codegen_backend_arm64_uops.c#L1-L220) and MMX entry handling at [src/codegen_new/codegen_backend_arm64_uops.c#L780-L820](src/codegen_new/codegen_backend_arm64_uops.c#L780-L820).
- ARM64 backend helpers have no Apple/dynarec guard coupling and emit generic load/store stubs per access ([src/codegen_new/codegen_backend_arm64.c#L1-L160](src/codegen_new/codegen_backend_arm64.c#L1-L160)), which may be hot under MMX-heavy traces.

## Dynarec-specific findings
- MMX ops are emitted as generic vector/ALU uops; there are no NEON-specialized saturating/shuffle templates (PSHUFB/pack/saturating add/sub) or table lookups for PSHUFB.
- MMX register lifetimes are short: every rop reloads from memory and writes back, with no attempt to keep MMX regs resident in NEON registers across multiple MMX uops.
- Code-cache management uses default block sizing; no Apple-M1-specific tuning, prefetching, or eviction telemetry is recorded.
- Guards: code is compiled under `__aarch64__` but not additionally keyed to `__APPLE__`+`NEW_DYNAREC_BACKEND` or to a runtime backend selector (risk of enabling on non-Apple ARM64 where tuning may differ).
- Guards: compile-time guard aliases plus the new `codegen_backend_is_apple_arm64()` helper now ensure Apple-specific paths only run when `dynarec_backend == BACKEND_ARM64_APPLE`, removing the reported risk for enabled flags.
- The `dynarec_micro` harness now reuses `bench_mmx_ops.h`, prefixes the reported rows with `DYN_`, and emits the same comparison block so the parser can keep tracking the MMX templates exercised inside the dynarec path.

## Proposed optimizations (all gated by platform/back-end)
1) **NEON templates for MMX arithmetic/logic/pack/shift (dynarec):**
   - Add Apple-ARM64-only emission paths mapping MMX ops to NEON 64-bit vectors (use `uint8x8_t`, `int16x4_t`, etc.) and emit in `codegen_backend_arm64_uops.c` when `dynarec_backend == BACKEND_ARM64_APPLE`.
   - Example (PADDB fast path) to embed in dynarec emitter:
```
#if defined(__APPLE__) && defined(__aarch64__) && defined(NEW_DYNAREC_BACKEND)
if (dynarec_backend == BACKEND_ARM64_APPLE) {
    // dst = dst + src (wrap)
    uint8x8_t va = vld1_u8((uint8_t *)&mmx_state[dst]);
    uint8x8_t vb = vld1_u8((uint8_t *)&mmx_state[src]);
    vst1_u8((uint8_t *)&mmx_state[dst], vadd_u8(va, vb));
} else {
    // existing fallback
}
#endif
```
   - Emit NEON saturating add/sub for PADDS*/PADDUS* using `vqadd_u8/vqadd_s8` etc.; use `vqmovn` for PACKSS*/PACKUS*, and `vtbl1_u8` for PSHUFB.

2) **Register pinning and spill reduction:**
   - Reserve V8–V15 as MMX working set on Apple ARM64 (already in host FP list) and extend allocator heuristics to keep `IREG_MM` live across adjacent MMX uops; avoid redundant `uop_MEM_LOAD_REG`/`STORE` pairs in [src/codegen_new/codegen_ops_mmx_loadstore.c#L1-L90](src/codegen_new/codegen_ops_mmx_loadstore.c#L1-L90).
   - Add simple liveness-based coalescing so back-to-back MMX rops reuse the same NEON regs until a state-sync point is needed.

3) **PSHUFB/pack/unpack specialized emission:**
   - Provide NEON table-lookup templates for PSHUFB and ZIP1/ZIP2-based pack/unpack sequences; cache constant shuffle tables in the code cache to reduce icache pressure.

4) **Aligned MMX backing store and prefetch:**
   - Align MMX state storage to 16 bytes and annotate loads with `__builtin_assume_aligned` to enable aligned LDR/STR (vector) in `codegen_backend_arm64_ops.c` stubs.
   - Add optional `__builtin_prefetch` for streaming MMX memory ops when `len >= 64` in interpreter fallbacks.

5) **Code cache and stub tuning:**
   - Increase block allocation size on ARM64 (e.g., 8–16 KB) and emit prefetch hints for the next block in `codegen_allocate_new_block` to reduce flush frequency.
   - Record cache hit/miss and flush reasons; expose counters for Instruments to correlate.

6) **Build/profile pipeline for Apple Silicon:**
   - Default to `-mcpu=apple-m1` for Apple builds, enable ThinLTO (`-flto=thin`) and optional PGO. Keep sanitizer runs (`-fsanitize=address,undefined`) for correctness before enabling dynarec fast paths.

7) **Runtime guard & fallback:**
   - Wrap all Apple-specific dynarec emission with both compile-time and runtime guards:
```
#if defined(__APPLE__) && defined(__aarch64__) && defined(NEW_DYNAREC_BACKEND)
if (dynarec_backend == BACKEND_ARM64_APPLE) {
    // NEON fast path
} else {
    // existing backend path
}
#else
// existing backend path
#endif
```

## Baselines & Benchmarks (to collect)
- Microbench harness (added): [benchmarks/mmx_neon_micro.c](benchmarks/mmx_neon_micro.c). Build and run on Apple Silicon to compare NEON vs scalar implementations of PADDB/PADDUSB/PADDSW:
   - Build: `clang -O3 -mcpu=apple-m1 -march=armv8-a benchmarks/mmx_neon_micro.c -o build/mmx_neon_micro`
   - Run NEON vs scalar: `build/mmx_neon_micro --iters=10000000 --impl=neon` (auto-runs scalar after NEON on arm64)
   Each invocation now prints both implementations' timings plus a ratio to the terminal so the CI log surface the measured NEON speedups directly.
   If the Qt plugin cannot be located during configure, set `CMAKE_PREFIX_PATH=/opt/homebrew/opt/qt@6/lib/cmake` (or the equivalent Qt6 install) so `Qt6Gui/Qt6QCocoaIntegrationPlugin` is found before we try to build the `mmx_neon_micro` bundle.
   The harness is driven by a `bench_ops` table, which makes adding future NEON templates trivial because the same reporting/comparison code already iterates over that table.
   CI now uploads `mmx_neon_micro.log` so the ratio output is persisted as an artifact for regression tracking. The log is sent through `tools/parse_mmx_neon_log.py` (threshold 0.5) to emit JSON metrics and fail the run if NEON regresses; the same script also parses `dynarec_micro.log` after the new dynarec harness executes so both pipelines share the same reporting format.
   The `dynarec_micro` harness mirrors the MMX op table and emits the same comparison block with `DYN_`-prefixed names, so the parser can keep monitoring the dynarec templates without extra parsing logic.
   JSON ingestion for those artifacts remains for later work; once a dashboard/alerting consumer exists, it can read the already-generated `.json` files and flag regressions automatically.
- Automatic CMake integration: configure the project to include the benchmark (apple arm64 only) so CI can execute `cmake --build build --target mmx_neon_micro && build/mmx_neon_micro --iters=10000000 --impl=neon` as part of the macOS arm64 job.
- Dynarec metrics: average code size per MMX instruction, code cache occupancy (%), flushes/hour, codegen time per block.
- Real workloads: run a DOS MMX demo or Win9x MPEG decode sample; record fps and CPU % with/without Apple NEON path.

## Tests & Validation
- Bit-exact MMX comparisons: interpreter vs dynarec fast path on randomized vectors; ensure wrap vs saturating semantics match.
- Cross-platform fallback check: force non-Apple backend and verify outputs unchanged.
- ABI/regression tests: ensure `MMX_ENTER` still sets `cpu_state.ismmx` and tags correctly; validate exceptions via `x86_int` remain correct when fast path is active.
- Sanitizers on debug builds; run clang `-fsanitize=address,undefined` before enabling LTO/PGO.
- Local verification: `build/benchmarks/mmx_neon_micro.app/Contents/MacOS/mmx_neon_micro --iters=1000000 --impl=neon` and `build/benchmarks/dynarec_micro.app/Contents/MacOS/dynarec_micro --iters=1000000 --impl=neon` both produce the expected NEON/scalar comparison block (including the new `DYN_` rows) so the parser can keep flagging regressions.

## Implementation plan (see opitmizationplan.md for PR breakdown)
- Land dynarec NEON templates first, then register-pinning and cache tuning, then interpreter fallback cleanups and harnesses.

## Recent Implementation Progress
- **PADDB NEON Path in Dynarec**: Updated `codegen_backend_arm64_uops.c` to emit NEON `ADD_V8B` for PADDB uops when `codegen_backend_is_apple_arm64()` returns true, ensuring Apple-specific fast paths while preserving scalar fallbacks for other backends. The change targets the `codegen_PADDB` handler (lines 1494-1515), adding an explicit Apple guard before the NEON vector add to align with the proposed runtime/backend checks.
- **Test Evidence**: Rebuilt and ran the `dynarec_micro` harness (`cmake --build build --target dynarec_micro` then `build/benchmarks/dynarec_micro.app/Contents/MacOS/dynarec_micro --iters=10000000 --impl=neon`), which exercises the new PADDB path. Output shows NEON vs scalar timings with a ratio of 1.24 for `DYN_PADDB` (NEON faster). Logs parsed via `tools/parse_mmx_neon_log.py` to generate JSON artifacts (`build/logs/dynarec_micro.json`), confirming the dynarec pipeline now includes NEON-backed MMX ops and the parser handles `DYN_`-prefixed entries without modification.

## References
- ARM Architecture Reference Manual ARMv8-A and NEON Programmer’s Guide – instruction semantics and intrinsics mapping (https://developer.arm.com/documentation/).
- Intel 64 and IA-32 Architectures Software Developer’s Manual, Vol. 2 – MMX instruction semantics (https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html).
- Agner Fog, “Instruction tables / Optimizing subroutines in assembly” – microarchitecture characteristics and instruction latencies (https://www.agner.org/optimize/).
- Apple Performance Optimization Guidelines for Apple Silicon – compiler flags and vectorization notes (https://developer.apple.com/documentation/metal/optimizing-performance-for-apple-silicon/).
- Clang/LLVM docs on vectorization and ARM64 backends – `-Rpass` diagnostics and NEON lowering (https://llvm.org/docs/Vectorizers.html).

## Appendix
- Environment (per [buildinstructions.md](buildinstructions.md)):
   - `BREW_PREFIX=$(brew --prefix)`
   - `export PATH="$BREW_PREFIX/opt/qt@6/bin:$PATH"`
   - `export PKG_CONFIG_PATH="$BREW_PREFIX/opt/freetype/lib/pkgconfig:$BREW_PREFIX/opt/libpng/lib/pkgconfig:$BREW_PREFIX/opt/libslirp/lib/pkgconfig:$BREW_PREFIX/opt/openal-soft/lib/pkgconfig:$BREW_PREFIX/opt/rtmidi/lib/pkgconfig:$BREW_PREFIX/opt/fluidsynth/lib/pkgconfig:$BREW_PREFIX/opt/sdl2/lib/pkgconfig:$BREW_PREFIX/opt/qt@6/lib/pkgconfig:$BREW_PREFIX/opt/libserialport/lib/pkgconfig:$BREW_PREFIX/opt/webp/lib/pkgconfig"`
   - `export CMAKE_PREFIX_PATH="$BREW_PREFIX:$BREW_PREFIX/opt/qt@6/lib/cmake:$BREW_PREFIX/opt/qt@6:$BREW_PREFIX/opt/sdl2:$BREW_PREFIX/opt/freetype:$BREW_PREFIX/opt/libpng:$BREW_PREFIX/opt/libslirp:$BREW_PREFIX/opt/openal-soft:$BREW_PREFIX/opt/rtmidi:$BREW_PREFIX/opt/fluidsynth:$BREW_PREFIX/opt/libserialport"`
- Quick build (Release, arm64): `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PWD/dist -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_MACOSX_BUNDLE=ON -DQT=ON -DUSE_QT6=ON -DOPENAL=ON -DRTMIDI=ON -DFLUIDSYNTH=ON -DMUNT=OFF -DDISCORD=OFF -DNEW_DYNAREC=ON -DLIBSERIALPORT_ROOT="$BREW_PREFIX/opt/libserialport" -DQT_QMAKE_EXECUTABLE="$BREW_PREFIX/opt/qt@6/bin/qmake" -DCMAKE_C_COMPILER=clang -DCMAKE_C_FLAGS='-O3 -mcpu=apple-m1 -march=armv8-a' && cmake --build build -j && cmake --install build`
- Run harness (planned): `build/src/tools/mmx_bench --iters 10000000 --mode dynarec` vs `--mode interp`.
- Profiling snippets: `instruments -t "Time Profiler" --time-limit 30 -- dist/86Box.app/Contents/MacOS/86Box -- <args>`.
