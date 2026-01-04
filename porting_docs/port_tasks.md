# ARM64 Voodoo Dynarec Port - Task Breakdown

## Phase 0: Infrastructure Setup (Days 1-2)
- [x] Add `dynarec_enabled` field to `voodoo_t` structure if not already present
- [x] Modify `vid_voodoo_render.h` platform detection to support ARM64
- [x] Create skeleton `vid_voodoo_codegen_arm64.h` with data structures
- [/] Implement `voodoo_codegen_init()` / `voodoo_codegen_close()` for ARM64
- [/] Add runtime toggle (env var `VOODOO_DYNAREC`) for testing
- [ ] Verify interpreter fallback works correctly

## Phase 1: Minimal Viable Dynarec (Days 3-5)
- [ ] Implement ARM64 prologue/epilogue with register saves
- [ ] Implement macOS W^X compliance with `MAP_JIT` and `pthread_jit_write_protect_np()`
- [ ] Create basic emission infrastructure (`emit32`, register field macros)
- [ ] Implement GP data movement (load/store offsets)
- [ ] Implement basic SIMD: `FMOV`, `UXTL`, `SQXTUN`, `UMOV`
- [ ] Implement flat-shaded triangle rendering (solid color)
- [ ] Test: Verify solid red triangle renders correctly

## Phase 2: Color Interpolation (Days 6-8)
- [ ] Implement gradient interpolation (dRdX, dGdX, dBdX, dAdX)
- [ ] Implement `SMULL`/`SSHR` for fixed-point multiplication
- [ ] Implement saturating arithmetic (`UQADD`, `UQSUB`, `SQADD`, `SQSUB`)
- [ ] Test: Gouraud-shaded triangle

## Phase 3: Alpha Blending (Days 9-11)
- [ ] Implement alpha channel support
- [ ] Implement `MUL.4H` for blending
- [ ] Implement logical operations (`EOR`, `AND`, `ORR`, `BIC`)
- [ ] Test: Semi-transparent triangle over background

## Phase 4: Depth Buffering (Days 12-14)
- [ ] Implement depth test (`CMGT`, `CMEQ`, `CMHI`)
- [ ] Implement depth write
- [ ] Implement conditional branching for depth test results
- [ ] Test: Overlapping triangles with Z-test

## Phase 5: Single Texture (Days 15-18)
- [ ] Implement S/T coordinate interpolation
- [ ] Implement texture fetch (point sampling)
- [ ] Implement texture format decoding (RGB565, ARGB1555)
- [ ] Implement `ZIP1`/`ZIP2` for unpacking
- [ ] Test: Textured quad with checkerboard

## Phase 6: Bilinear Filtering (Days 19-21)
- [ ] Implement 4-texel fetch
- [ ] Implement lerp operations (`DUP`, `EXT`, `PMULL`)
- [ ] Test: Rotating textured quad

## Phase 7: Color/Alpha Combine (Days 22-24)
- [ ] Implement fbzColorPath combine equations
- [ ] Implement alphaMode combine equations  
- [ ] Implement all blend modes
- [ ] Test: 3DMark 99 rendering tests

## Phase 8: Fog (Days 25-26)
- [ ] Implement fog table lookup
- [ ] Implement fog blending
- [ ] Test: Foggy scene

## Phase 9: Dual TMU (Days 27-28)
- [ ] Implement second texture unit
- [ ] Implement multi-texture combine modes
- [ ] Test: 3DMark 99 Nature test

## Phase 10: Voodoo 3 Features (Days 29-30)
- [ ] Implement tiled framebuffer addressing
- [ ] Implement 32-bit color support (RGBA8888)
- [ ] Test: Windows desktop in Voodoo 3 2D mode

## Testing and Polish (Days 31-35)
- [ ] Run full test suite (GLQuake, Quake 2, Unreal, 3DMark 99)
- [ ] Fix any remaining bugs
- [ ] Optimize hot paths
- [ ] Verify all acceptance criteria (Part 13.5)
- [ ] Final performance benchmarks
