# Optimization Plan (Apple Silicon MMX / new dynarec)

## 1. Executive summary
Prioritize NEON-backed MMX paths in the new dynarec on Apple Silicon, guarded by `__APPLE__ && __aarch64__ && NEW_DYNAREC_BACKEND` plus a runtime backend check. Land changes in small PRs: dynarec templates first, then register-pinning and cache tuning, followed by harnesses, PGO/LTO, and CI coverage.

## 2. Priority PR list (ordered)
1) **Add Apple-ARM64 dynarec guard and backend switch**
   - Files: [src/codegen_new/codegen_backend_arm64_uops.c](src/codegen_new/codegen_backend_arm64_uops.c), [src/codegen_new/codegen_backend_arm64.c](src/codegen_new/codegen_backend_arm64.c)
   - Change: introduce `BACKEND_ARM64_APPLE` runtime selector and wrap Apple-specific emission with `#if defined(__APPLE__) && defined(__aarch64__) && defined(NEW_DYNAREC_BACKEND)`.
   - LOC/complexity: ~80, Low; Effort: 0.5 day.
   - Tests: build-only, smoke run; unit: force non-Apple backend to ensure fallback path works.
   - Bench harness: none (enablement only).
   - Guards: compile-time guard above; runtime `if (dynarec_backend == BACKEND_ARM64_APPLE)`.
   - Risk/rollback: low; revert by disabling the backend enum and guard.

2) **NEON MMX arithmetic/logic templates**
   - Files: [src/codegen_new/codegen_backend_arm64_uops.c](src/codegen_new/codegen_backend_arm64_uops.c), [src/codegen_new/codegen_ops_mmx_arith.c](src/codegen_new/codegen_ops_mmx_arith.c), [src/codegen_new/codegen_ops_mmx_logic.c](src/codegen_new/codegen_ops_mmx_logic.c), [src/codegen_new/codegen_ops_mmx_cmp.c](src/codegen_new/codegen_ops_mmx_cmp.c), [src/codegen_new/codegen_ops_mmx_shift.c](src/codegen_new/codegen_ops_mmx_shift.c)
   - Change: emit NEON 64-bit vector ops for PADDB/PADDUSB/PADDSW/PSUB*/PMULLW/PMULHW/PMADDWD/logic/compare/shift using intrinsics or direct opcodes; keep scalar fallback.
   - LOC/complexity: ~250, Medium; Effort: 1.5–2 days.
   - Tests: bit-exact MMX vectors vs interpreter; fuzzing random inputs; ASan/UBSan.
   - Bench harness: `src/tools/mmx_bench.c` (new) with per-op microbench.
   - Guards: compile-time + runtime backend guard.
   - Risk/rollback: medium; keep feature flag to fall back to existing uops.

3) **Pack/unpack/shuffle (PSHUFB, PACK*, UNPCK*) NEON templates**
   - Files: [src/codegen_new/codegen_ops_mmx_pack.c](src/codegen_new/codegen_ops_mmx_pack.c), [src/codegen_new/codegen_ops_mmx_loadstore.c](src/codegen_new/codegen_ops_mmx_loadstore.c), [src/codegen_new/codegen_backend_arm64_uops.c](src/codegen_new/codegen_backend_arm64_uops.c)
   - Change: add NEON table/shuffle templates (vtbl/zip/unzip) and cache constant tables; provide scalar fallback.
   - LOC/complexity: ~180, Medium; Effort: 1–1.5 days.
   - Tests: bit-exact pack/unpack/pshufb vectors; endian edge cases; fuzz.
   - Bench harness: extend `src/tools/mmx_bench.c` to cover pack/shuffle ops.
   - Guards: same compile/runtime guards.
   - Risk/rollback: medium; keep fallback tables.

4) **MMX register pinning & spill reduction**
   - Files: [src/codegen_new/codegen_allocator.c](src/codegen_new/codegen_allocator.c), [src/codegen_new/codegen_backend_arm64_uops.c](src/codegen_new/codegen_backend_arm64_uops.c), [src/codegen_new/codegen_reg.h](src/codegen_new/codegen_reg.h)
   - Change: reserve V8–V15 for MMX on Apple ARM64, extend allocator heuristics to keep `IREG_MM` live across consecutive MMX uops, coalesce redundant loads/stores.
   - LOC/complexity: ~160, Medium; Effort: 1–1.5 days.
   - Tests: dynarec regression suite; targeted trace with chained MMX ops to ensure no state loss; ASan.
   - Bench harness: `src/tools/mmx_bench.c` (chain MMX ops) + trace-level counter for spills.
   - Guards: compile/runtime guards; feature flag to disable pinning if needed.
   - Risk/rollback: medium; rollback by disabling pinning flag.

5) **Aligned MMX state + load/store stub tuning**
   - Files: [src/codegen_new/codegen_backend_arm64.c](src/codegen_new/codegen_backend_arm64.c), [src/cpu/x86_ops_mmx_arith.h](src/cpu/x86_ops_mmx_arith.h)
   - Change: align MMX state to 16 bytes, annotate loads/stores with `__builtin_assume_aligned`, add optional prefetch for large strides, and prefetch next code block in allocator.
   - LOC/complexity: ~120, Low/Medium; Effort: 0.5–1 day.
   - Tests: alignment assertion on startup; run interpreter MMX ops; ASan alignment checks.
   - Bench harness: `src/tools/mmx_bench.c` memory-heavy mode.
   - Guards: compile/runtime guards where NEON stubs are used.
   - Risk/rollback: low; revert alignment hints.

6) **Code-cache instrumentation and sizing for Apple Silicon**
   - Files: [src/codegen_new/codegen_backend_arm64.c](src/codegen_new/codegen_backend_arm64.c), [src/codegen_new/codegen.h](src/codegen_new/codegen.h)
   - Change: add counters for cache hit/miss/flush, adjustable block size (e.g., 8–16 KB) for ARM64, optional prefetch hints; expose metrics for logging.
   - LOC/complexity: ~140, Medium; Effort: 1 day.
   - Tests: unit log check; stress with frequent invalidations; ensure branches remain in-range.
   - Bench harness: capture metrics during `src/tools/mmx_bench.c` runs and real workloads.
   - Guards: compile/runtime guards; tunable via env/flag.
   - Risk/rollback: low; disable metrics and revert block size.

7) **Microbench + CI wiring**
   - Files: [src/tools/mmx_bench.c](src/tools/mmx_bench.c) (new), [CMakeLists.txt](CMakeLists.txt), CI configs
   - Change: add microbench harness, hook minimal CI run on macOS arm64, document usage.
   - LOC/complexity: ~180, Low; Effort: 0.5–1 day.
   - Tests: harness self-checks vs interpreter reference; CI smoke.
   - Bench harness: the harness itself; integrate results reporting.
   - Guards: harness selects backend via runtime flag; compile-time guard for Apple-specific code paths.
   - Risk/rollback: low; remove harness if flaky.

## 3. Milestones & timeline (rough)
- Milestone A (week 1): PRs 1–2 landed; basic NEON arithmetic/logic in dynarec; smoke tests on M1.
- Milestone B (week 2): PRs 3–4; pack/shuffle templates and MMX register pinning; start collecting spill metrics.
- Milestone C (week 3): PRs 5–6; alignment + cache tuning; metrics exposed; first PGO/LTO experiment.
- Milestone D (week 4): PR 7; harness + CI/macOS arm64 lane; consolidate results and guard toggles.

## 4. CI and testing plan
- CI runners: macOS arm64 (primary), macOS x86_64 and Linux x86_64 (fallback correctness). Use existing workflows if available; add lightweight job for `ctest` + harness sanity on macOS arm64.
- Tests per PR: bit-exact MMX vectors (interp vs dynarec), ASan/UBSan debug build, release build with NEON guards on, optional `-flto` check.
- Quick sanity: run `src/tools/mmx_bench` with `--mode interp` and `--mode dynarec` and compare outputs; run a short MMX demo workload to ensure no crashes.

## 5. Measurement & validation
- Acceptance per PR: no correctness regressions; dynarec fast path shows measurable improvement in `mmx_bench` (target ≥10% per-op gain for arithmetic/logic; ≥20% for pack/shuffle); code-cache flush rate reduced vs baseline.
- Collect metrics: ns/op, cycles/op, emitted bytes/op, code cache hit %, spill counts, and wall-clock fps on a representative workload.

## 6. Review & rollout
- Suggested reviewers: dynarec maintainer (TBD), Apple Silicon/ARM maintainer (TBD).
- Rollout: land behind compile/runtime guards; default-on for Apple ARM64 dynarec once benchmarks pass; keep flag to disable (`BACKEND_ARM64_APPLE` off) for easy rollback.

## 7. Appendix
- Environment (from [buildinstructions.md](buildinstructions.md)):
   - `BREW_PREFIX=$(brew --prefix)`
   - `export PATH="$BREW_PREFIX/opt/qt@6/bin:$PATH"`
   - `export PKG_CONFIG_PATH="$BREW_PREFIX/opt/freetype/lib/pkgconfig:$BREW_PREFIX/opt/libpng/lib/pkgconfig:$BREW_PREFIX/opt/libslirp/lib/pkgconfig:$BREW_PREFIX/opt/openal-soft/lib/pkgconfig:$BREW_PREFIX/opt/rtmidi/lib/pkgconfig:$BREW_PREFIX/opt/fluidsynth/lib/pkgconfig:$BREW_PREFIX/opt/sdl2/lib/pkgconfig:$BREW_PREFIX/opt/qt@6/lib/pkgconfig:$BREW_PREFIX/opt/libserialport/lib/pkgconfig:$BREW_PREFIX/opt/webp/lib/pkgconfig"`
   - `export CMAKE_PREFIX_PATH="$BREW_PREFIX:$BREW_PREFIX/opt/qt@6/lib/cmake:$BREW_PREFIX/opt/qt@6:$BREW_PREFIX/opt/sdl2:$BREW_PREFIX/opt/freetype:$BREW_PREFIX/opt/libpng:$BREW_PREFIX/opt/libslirp:$BREW_PREFIX/opt/openal-soft:$BREW_PREFIX/opt/rtmidi:$BREW_PREFIX/opt/fluidsynth:$BREW_PREFIX/opt/libserialport"`
- Build (release, Apple; matches buildinstructions): `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PWD/dist -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_MACOSX_BUNDLE=ON -DQT=ON -DUSE_QT6=ON -DOPENAL=ON -DRTMIDI=ON -DFLUIDSYNTH=ON -DMUNT=OFF -DDISCORD=OFF -DNEW_DYNAREC=ON -DLIBSERIALPORT_ROOT="$BREW_PREFIX/opt/libserialport" -DQT_QMAKE_EXECUTABLE="$BREW_PREFIX/opt/qt@6/bin/qmake" -DCMAKE_C_COMPILER=clang -DCMAKE_C_FLAGS='-O3 -mcpu=apple-m1 -march=armv8-a' && cmake --build build -j && cmake --install build`
- Run harness (once added): `build/src/tools/mmx_bench --iters 10000000 --mode dynarec` and `--mode interp`.
- PR checklist template:
  - [ ] Compile-time guard added: `#if defined(__APPLE__) && defined(__aarch64__) && defined(NEW_DYNAREC_BACKEND)`.
  - [ ] Runtime guard added: `if (dynarec_backend == BACKEND_ARM64_APPLE)`.
  - [ ] Fallback path preserved and tested.
  - [ ] Bench numbers recorded (before/after) and attached.
  - [ ] ASan/UBSan run (debug), release run (dynarec on).
  - [ ] Docs updated (optimizationreport/opitmizationplan if scope changes).
