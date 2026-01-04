# Performance Optimization Guide

**Post-correctness performance tuning for the ARM64 Voodoo dynarec.**

## Prerequisites

This guide assumes you have:
1. A working ARM64 dynarec that produces pixel-perfect output
2. Achieved at least 2x speedup over interpreter
3. No crashes or memory leaks

**Rule**: Never optimize before correctness is verified. Performance doesn't matter if output is wrong.

---

## 1. Measure Before Optimizing

### 1.1 Profiling with Instruments

```bash
# Profile with Time Profiler
xctrace record --template "Time Profiler" \
  -D voodoo_profile.trace \
  ./dist/86Box.app/Contents/MacOS/86Box -P "/path/to/vm/"

# Analyze hotspots
open voodoo_profile.trace
# Look for: voodoo_render_scanline, voodoo_generate_arm64, SIMD operations
```

### 1.2 Performance Counters

```bash
# Use perf (if available on macOS via Rosetta workarounds)
# Or use Xcode xctrace record --template "Counters"

# Key metrics to watch:
# - Instructions per cycle (IPC): Target > 2.0
# - Branch mispredictions: < 5%
# - Cache misses: < 10%
# - NEON utilization: > 80%
```

### 1.3 Benchmark-Driven Development

```bash
# Baseline before optimization
./scripts/run_voodoo_bench.sh > baseline.log

# After each optimization
./scripts/run_voodoo_bench.sh > optimized.log

# Compare
diff baseline.log optimized.log
```

---

## 2. Branch Prediction Hints

### 2.1 Likely/Unlikely Branches

ARM64 has static branch prediction. Help the CPU by:

**Arrange hot path first**:
```c
// BAD: Cold path first
if (unlikely_condition) {
    // Rare case
} else {
    // Common case - predicted not-taken
}

// GOOD: Hot path first
if (likely_condition) {
    // Common case - predicted taken
} else {
    // Rare case
}
```

**Use likely/unlikely macros**:
```c
#define likely(x)    __builtin_expect(!!(x), 1)
#define unlikely(x)  __builtin_expect(!!(x), 0)

if (unlikely(voodoo->dynarec_enabled == 0)) {
    // Interpreter fallback (rare)
    return;
}
// Dynarec path (common)
```

### 2.2 Reduce Branching in SIMD Code

Replace conditional logic with NEON select operations:

**Before** (branchy):
```c
// For each pixel, clamp to 0-255
for (int i = 0; i < 8; i++) {
    if (pixel[i] > 255) pixel[i] = 255;
    if (pixel[i] < 0) pixel[i] = 0;
}
```

**After** (branchless with NEON):
```c
// Saturating pack handles clamping automatically
SQXTUN V0.8B, V0.8H  // Clamps to 0-255 without branches
```

---

## 3. NEON Instruction Scheduling

### 3.1 Avoid Pipeline Stalls

ARM64 Cortex cores have out-of-order execution, but dependencies can stall:

**BAD** (data dependency):
```c
MUL V0.8H, V0.8H, V1.8H  // V0 = V0 * V1
ADD V0.8H, V0.8H, V2.8H  // Waits for MUL result (stall)
```

**GOOD** (interleave independent ops):
```c
MUL V0.8H, V0.8H, V1.8H  // V0 = V0 * V1
MUL V3.8H, V3.8H, V4.8H  // Independent, can execute in parallel
ADD V0.8H, V0.8H, V2.8H  // By now, first MUL is done
ADD V3.8H, V3.8H, V5.8H
```

### 3.2 Load/Store Pairing

Use `STP/LDP` for consecutive memory access:

**Before**:
```c
STR W0, [X19, #0]
STR W1, [X19, #4]
STR W2, [X19, #8]
STR W3, [X19, #12]
```

**After** :
```c
STP W0, W1, [X19, #0]   // Store 2 words in one instruction
STP W2, W3, [X19, #8]
```

### 3.3 Prefetch Data

For large framebuffer access:
```c
// Prefetch next scanline while processing current
PRFM PLDL1KEEP, [X19, #scanline_stride]
```

---

## 4. Cache-Friendly Data Layout

### 4.1 Align Code Blocks

Ensure JIT code blocks are cache-line aligned:

```c
// Align code_block to 64-byte cache line
#define BLOCK_SIZE 8192
#define CACHE_LINE 64

typedef struct voodoo_arm64_data_t {
    uint8_t code_block[BLOCK_SIZE] __attribute__((aligned(CACHE_LINE)));
    // ... other fields ...
} voodoo_arm64_data_t;
```

### 4.2 Pack Hot Data Together

Keep frequently accessed fields in the same cache line:

```c
// BAD: Frequently accessed fields spread out
typedef struct voodoo_t {
    uint8_t *fb_mem;       // Cache line 0
    // ... 64 bytes of other stuff ...
    int dynarec_enabled;   // Cache line 1 (miss!)
} voodoo_t;

// GOOD: Group hot fields
typedef struct voodoo_t {
    // Hot fields first (same cache line)
    uint8_t *fb_mem;
    int dynarec_enabled;
    void *codegen_data;
    // ... cold fields later ...
} voodoo_t;
```

### 4.3 Minimize Cache Thrashing

For parallel render threads:
```c
// Give each thread its own cache line to avoid false sharing
typedef struct render_thread_data_t {
    int pixel_count;
    int texel_count;
    char padding[64 - 8];  // Pad to cache line size
} __attribute__((aligned(64))) render_thread_data_t;
```

---

## 5. Reduce JIT Compilation Overhead

### 5.1 Block Caching Strategy

Reuse generated code when pipeline state hasn't changed:

```c
// Check if block can be reused
if (data->fbzMode == params->fbzMode &&
    data->alphaMode == params->alphaMode &&
    data->fogMode == params->fogMode &&
    data->textureMode[0] == params->textureMode[0]) {
    // Reuse existing block (no recompilation)
    return data->code_block;
}
// Otherwise, regenerate
```

### 5.2 Fast Path for Common Cases

Generate optimized code for common pipeline states:

```c
// Fast path: No texture, no alpha, no fog (common for solid colors)
if (is_simple_fill(params)) {
    emit_simple_fill_arm64();  // Highly optimized 20-instruction path
} else {
    emit_full_pipeline_arm64();  // Full 200-instruction path
}
```

### 5.3 Lazy Compilation

Only compile blocks when actually executed:

```c
// Mark block as uncompiled
data->is_compiled = 0;

// On first execution, compile
if (!data->is_compiled) {
    voodoo_generate_arm64(...);
    data->is_compiled = 1;
}
```

---

## 6. NEON-Specific Optimizations

### 6.1 Use Widening/Narrowing Instructions

Instead of separate extend + operate:

**Before**:
```c
UXTL V0.8H, V0.8B    // Extend 8-bit to 16-bit
MUL V0.8H, V0.8H, V1.8H
SQXTUN V0.8B, V0.8H  // Narrow back to 8-bit
```

**After** (if possible):
```c
UMULL V0.4S, V0.4H, V1.4H  // Widening multiply (16-bit to 32-bit)
// ... operations in 32-bit ...
XTN V0.4H, V0.4S           // Narrow 32-bit to 16-bit
```

### 6.2 Exploit Fused Operations

Use fused multiply-add when applicable:

```c
// Instead of: result = a + (b * c)
MUL V0.8H, V1.8H, V2.8H
ADD V0.8H, V0.8H, V3.8H

// Use MLA (multiply-accumulate)
MLA V3.8H, V1.8H, V2.8H  // V3 = V3 + (V1 * V2)
```

### 6.3 Minimize GP ↔ SIMD Transfers

**Slow**:
```c
FMOV W0, S0      // SIMD to GP
ADD W0, W0, #1
FMOV S0, W0      // GP back to SIMD
```

**Fast** (stay in SIMD):
```c
DUP V1.4S, W0    // Broadcast GP to all lanes once
ADD V0.4S, V0.4S, V1.4S  // SIMD add
```

---

## 7. Parallelization

### 7.1 Optimize Thread Dispatch

Minimize synchronization overhead:

```c
// BAD: Lock per pixel
for (each pixel) {
    mutex_lock(&render_mutex);
    render_pixel(x, y);
    mutex_unlock(&render_mutex);
}

// GOOD: Lock per scanline
mutex_lock(&render_mutex);
for (each pixel in scanline) {
    render_pixel(x, y);
}
mutex_unlock(&render_mutex);
```

### 7.2 Load Balancing

Distribute work evenly across threads:

```c
// Even/odd scanline assignment
int thread_id = scanline % 4;  // 4 render threads
if (voodoo->render_voodoo_busy[thread_id] == 0) {
    dispatch_scanline(thread_id, scanline);
}
```

---

## 8. Code Size Optimization

### 8.1 Reduce Block Size

Smaller code fits better in instruction cache:

**Target**: <2KB per code block (optimal for L1 icache)

**Techniques**:
- Share common sequences (e.g., prologue/epilogue)
- Use helper functions for rare cases
- Avoid inlining rarely executed paths

### 8.2 Tail Call Optimization

For function exits:
```c
// Instead of CALL + RET
BL helper_function
RET

// Use tail call (branch, not call)
B helper_function  // Helper will RET for us
```

---

## 9. Benchmarking Results

### Expected Improvements

| Optimization | Speedup | Complexity |
|:-------------|:--------|:-----------|
| Branch prediction hints  | +5-10% | Low |
| Instruction scheduling | +10-15% | Medium |
| Cache alignment | +5-8% | Low |
| Block caching | +20-30% | Medium |
| NEON fusion | +15-20% | High |
| Thread optimization | +10-15% | Medium |

**Cumulative target**: 3.5-4x speedup vs interpreter (from 2.5x baseline)

### Measurement

```bash
# Before optimization
GLQuake: 38 FPS (2.5x speedup)

# After all optimizations
GLQuake: 52 FPS (3.5x speedup)

# Breakdown per optimization:
# - Branch hints: +2 FPS
# - Scheduling: +4 FPS
# - Caching: +6 FPS
# - NEON: +2 FPS
```

---

## 10. Optimization Checklist

Before declaring optimization complete:

- [ ] Profiled with Instruments to find hotspots
- [ ] Branch prediction hints added to hot paths
- [ ] NEON instructions scheduled to avoid stalls
- [ ] Data structures aligned to cache lines
- [ ] Block caching reduces recompilation
- [ ] Fast paths for common cases
- [ ] GP↔SIMD transfers minimized
- [ ] Thread synchronization optimized
- [ ] Code size within L1 icache budget
- [ ] Benchmarks show >3x speedup vs interpreter
- [ ] No performance regression in any test case

---

## When to Stop Optimizing

**Stop when**:
1. Speedup meets target (3-4x vs interpreter)
2. Further optimization yields <2% improvement
3. Code becomes unmaintainable
4. Optimization time exceeds user benefit

**Remember**: Premature optimization is the root of all evil. Correctness first, performance second.

---

## See Also

- Main guide: `VOODOO_ARM64_PORT_GUIDE.md` Part 13.5 (Performance criteria)
- Common pitfalls: `ARM64_COMMON_PITFALLS.md`
- NEON optimization guide: https://developer.arm.com/documentation/102159/latest/
