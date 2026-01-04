# ARM64 Voodoo Dynarec Port - Changelog

**Project**: 86Box Voodoo ARM64 Dynamic Recompiler Port
**Start Date**: [YYYY-MM-DD]
**Target Completion**: 22-35 days from start

---

## Guidelines

- Update this file after completing each phase or significant milestone
- For granular, item-by-item progress, use **[port_tasks.md](port_tasks.md)**
- Record both successes and challenges encountered
- Note any deviations from the original plan
- Track performance improvements as they're achieved
- Document bugs found and fixed

---

## [Unreleased]

### Phase 0: Infrastructure (Days 1-2)
**Status**: Not started
**Target Date**: [YYYY-MM-DD]

#### Tasks
- [ ] Add `dynarec_enabled` field to `voodoo_t` structure
- [ ] Implement config file integration
- [ ] Add runtime dispatch logic in `vid_voodoo_render.c`
- [ ] Verify interpreter fallback works
- [ ] Set up frame capture for correctness validation

#### Blockers
- None

#### Notes
- 

---

### Phase 1: Minimal Viable Dynarec (Days 3-5)
**Status**: Not started
**Target Date**: [YYYY-MM-DD]

#### Tasks
- [ ] Create `vid_voodoo_codegen_arm64.h` skeleton
- [ ] Implement prologue/epilogue
- [ ] Implement basic SIMD operations (UXTL, SQXTUN, FMOV, UMOV)
- [ ] Implement flat-shaded triangle rendering
- [ ] Test: Solid red triangle renders correctly

#### Performance
- Target: N/A (correctness only)
- Achieved: [X]x speedup
- Baseline interpreter: [X] FPS

#### Bugs Found
- 

#### Notes
- 

---

### Phase 2: Color Interpolation (Days 6-8)
**Status**: Not started
**Target Date**: [YYYY-MM-DD]

#### Tasks
- [ ] Implement gradient interpolation (dRdX, dGdX, dBdX)
- [ ] Add SMULL, SSHR, SQXTN operations
- [ ] Add saturating arithmetic (UQADD, UQSUB)
- [ ] Test: Gouraud-shaded triangle

#### Performance
- Target: N/A
- Achieved: [X]x speedup

#### Bugs Found
- 

#### Notes
- 

---

### Phase 3: Alpha Blending (Days 9-11)
**Status**: Not started
**Target Date**: [YYYY-MM-DD]

#### Tasks
- [ ] Implement alpha channel support
- [ ] Add MUL.4H for blending
- [ ] Add logical operations (EOR, AND, ORR)
- [ ] Test: Semi-transparent triangle over background

#### Performance
- Target: N/A
- Achieved: [X]x speedup

#### Bugs Found
- 

#### Notes
- 

---

### Phase 4: Depth Buffering (Days 12-14)
**Status**: Not started
**Target Date**: [YYYY-MM-DD]

#### Tasks
- [ ] Implement depth test (CMGT, CMEQ)
- [ ] Implement depth write
- [ ] Add BIC for masking
- [ ] Test: Overlapping triangles with Z-test

#### Performance
- Target: N/A
- Achieved: [X]x speedup

#### Bugs Found
- 

#### Notes
- 

---

### Phase 5: Single Texture (Days 15-18)
**Status**: Not started
**Target Date**: [YYYY-MM-DD]

#### Tasks
- [ ] Implement S/T coordinate interpolation
- [ ] Implement texture fetch (point sampling)
- [ ] Add texture format decoding (RGB565, ARGB1555)
- [ ] Add ZIP1/ZIP2 for unpacking
- [ ] Test: Textured quad with checkerboard

#### Performance
- Target: 2x speedup minimum
- Achieved: [X]x speedup

#### Bugs Found
- 

#### Notes
- 

---

### Phase 6: Bilinear Filtering (Days 19-21)
**Status**: Not started
**Target Date**: [YYYY-MM-DD]

#### Tasks
- [ ] Implement 4-texel fetch
- [ ] Add lerp operations (DUP, EXT, PMULL)
- [ ] Test: Rotating textured quad

#### Performance
- Target: 2.5x speedup
- Achieved: [X]x speedup

#### Bugs Found
- 

#### Notes
- 

---

### Phase 7: Color/Alpha Combine (Days 22-24)
**Status**: Not started
**Target Date**: [YYYY-MM-DD]

#### Tasks
- [ ] Implement fbzColorPath combine equations
- [ ] Implement alphaMode combine equations
- [ ] Add all blend modes
- [ ] Test: 3DMark 99 rendering tests

#### Performance
- Target: 2.5x speedup
- Achieved: [X]x speedup

#### Bugs Found
- 

#### Notes
- 

---

### Phase 8: Fog (Days 25-26)
**Status**: Not started
**Target Date**: [YYYY-MM-DD]

#### Tasks
- [ ] Implement fog table lookup
- [ ] Implement fog blending
- [ ] Test: Foggy scene

#### Performance
- Target: 3x speedup
- Achieved: [X]x speedup

#### Bugs Found
- 

#### Notes
- 

---

### Phase 9: Dual TMU (Days 27-28)
**Status**: Not started
**Target Date**: [YYYY-MM-DD]

#### Tasks
- [ ] Implement second texture unit
- [ ] Add multi-texture combine modes
- [ ] Test: 3DMark 99 Nature test

#### Performance
- Target: 3x speedup
- Achieved: [X]x speedup

#### Bugs Found
- 

#### Notes
- 

---

### Phase 10: Voodoo 3 Features (Days 29-30)
**Status**: Not started
**Target Date**: [YYYY-MM-DD]

#### Tasks
- [ ] Implement tiled framebuffer addressing
- [ ] Add 32-bit color support (RGBA8888)
- [ ] Test: Windows desktop in Voodoo 3 2D mode

#### Performance
- Target: 3.5x speedup
- Achieved: [X]x speedup

#### Bugs Found
- 

#### Notes
- 

---

### Testing and Polish (Days 31-35)
**Status**: Not started
**Target Date**: [YYYY-MM-DD]

#### Tasks
- [ ] Run full test suite (GLQuake, Quake 2, Unreal, 3DMark 99)
- [ ] Fix any remaining bugs
- [ ] Optimize hot paths (see PERFORMANCE_OPTIMIZATION.md)
- [ ] Verify all acceptance criteria (Part 13.5)
- [ ] Final performance benchmarks

#### Performance
- Target: 3-4x speedup
- Achieved: [X]x speedup

#### Bugs Found
- 

#### Notes
- 

---

## Performance Milestones

| Date | Phase | GLQuake FPS | Speedup | Notes |
|:-----|:------|:------------|:--------|:------|
| [YYYY-MM-DD] | Baseline | 15 | 1.0x | Interpreter |
| [YYYY-MM-DD] | Phase 5 | [X] | [X]x | First textured rendering |
| [YYYY-MM-DD] | Phase 10 | [X] | [X]x | All features complete |
| [YYYY-MM-DD] | Optimized | [X] | [X]x | After performance tuning |

---

## Lessons Learned

### What Worked Well
- 

### What Was Challenging
- 

### What Would I Do Differently
- 

---

## Deviations from Plan

### Scope Changes
- 

### Timeline Adjustments
- 

### Technical Decisions
- 

---

## Final Metrics

**Completion Date**: [YYYY-MM-DD]
**Total Days**: [X]
**Lines of Code**: [X]
**Final Speedup**: [X]x vs interpreter
**Test Pass Rate**: [X]%
**Known Issues**: [X]

---

## Sign-Off Checklist

From Part 13.5 (Definition of Done):

### Correctness
- [ ] 99% of pixels within 1 LSB of interpreter
- [ ] All automated tests pass
- [ ] No visible rendering artifacts
- [ ] Golden image MD5 matches

### Performance
- [ ] GLQuake: ≥38 FPS (2.5x minimum)
- [ ] 3DMark 99: ≥20 FPS (2.5x minimum)
- [ ] Fillrate: ≥12.5 Mpix/s (2.5x minimum)

### Stability
- [ ] 1 hour stress test without crashes
- [ ] No memory leaks with ASan
- [ ] No race conditions (4 threads)
- [ ] No W^X violations

### Compatibility
- [ ] Voodoo 1, 2, Banshee, 3 all work
- [ ] Windows 95/98/ME/DOS support
- [ ] GLQuake, Quake 2, Unreal, 3DMark 99, Half-Life, NFS3 all run

### Documentation
- [ ] All SIMD operations documented
- [ ] Instruction encoding verified
- [ ] Register allocation documented
- [ ] Known limitations documented
- [ ] This changelog complete

**Port Status**: [ ] COMPLETE / [ ] IN PROGRESS / [ ] BLOCKED

**Signed**: [Name]
**Date**: [YYYY-MM-DD]
