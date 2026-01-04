# ARM64 Voodoo Dynarec Port Plan

## Executive Summary

This document details a comprehensive plan to port the 86Box Voodoo graphics card emulator's dynamic recompiler (dynarec) from x86-64 to ARM64 architecture. The port targets Apple Silicon (M1+) running macOS, with the ARM64-specific code in a separate file (vid_voodoo_codegen_arm64.h) mirroring the existing vid_voodoo_codegen_x86-64.h structure.

The existing x86-64 dynarec generates SSE2 machine code at runtime for Voodoo pixel pipeline acceleration. This ARM64 port will generate equivalent NEON instructions, achieving comparable performance while maintaining full compatibility with all Voodoo-family devices (Voodoo 1, Voodoo 2, Banshee, Voodoo 3, Velocity 100/200).

---

## 1. Architecture Analysis of x86-64 Dynarec

### 1.1 Overall Code Generation Strategy

The existing dynarec in `vid_voodoo_codegen_x86-64.h` uses:

*   **Direct byte emission**: Machine code bytes are written to a buffer via `addbyte()`, `addword()`, `addlong()`, and `addquad()` macros.
*   **State-dependent specialization**: Code blocks are generated based on `alphaMode`, `fbzMode`, `fogMode`, `fbzColorPath`, `textureMode`, and `tLOD` state.
*   **Block caching**: 8 blocks of 8KB each per odd/even scanline, indexed by state parameters.
*   **Inline function calls**: No external function calls during pixel processing (entire pipeline is inlined).

### 1.2 Key Data Structures

```c
typedef struct voodoo_x86_data_t {
    uint8_t  code_block[BLOCK_SIZE];  // 8192 bytes
    int      xdir;
    uint32_t alphaMode, fbzMode, fogMode, fbzColorPath;
    uint32_t textureMode[2], tLOD[2];
    uint32_t trexInit1;
    int      is_tiled;
} voodoo_x86_data_t;
```

### 1.3 Register Allocation Scheme (x86-64)

| Register | Usage |
| :--- | :--- |
| **RDI** | `voodoo_state_t*` pointer |
| **RSI/R15** | `voodoo_params_t*` pointer |
| **R14** | `real_y` scanline |
| **R9** | `logtable` pointer |
| **R10** | `alookup` table |
| **R11** | `aminuslookup` table |
| **R12** | `xmm_00_ff_w` constants |
| **R13** | `i_00_ff_w` constants |
| **XMM0-XMM7** | Pixel/texture data (unpacked 16-bit channels) |
| **XMM8-XMM11** | Constants (`01_w`, `ff_w`, `ff_b`, `minus_254`) |

### 1.4 SSE2 Instruction Patterns

The dynarec uses these SSE2 operations extensively:

| Category | Instructions |
| :--- | :--- |
| **Unpack/Pack** | `PUNPCKLBW`, `PUNPCKHBW`, `PACKUSWB`, `PACKSSDW` |
| **Arithmetic** | `PADDW`, `PSUBW`, `PMULLW`, `PMULHW`, `PSRAD`, `PSRLW`, `PSLLW` |
| **Logic** | `PAND`, `POR`, `PXOR` |
| **Compare** | `PCMPEQ` with masking |
| **Shuffle** | `PSHUFLW`, `PSLLDQ`, `PSRLDQ`, `PINSRW` |
| **Move** | `MOVD`, `MOVQ`, `MOVDQU`, `MOVDQA` |

---

## 2. ARM64 Design Overview

### 2.1 New File Structure

> [!IMPORTANT]
> The ARM64 port will be implemented in a separate file to maintain clean separation from the x86-64 implementation.

| File | Purpose |
| :--- | :--- |
| `vid_voodoo_codegen_arm64.h`| ARM64/NEON codegen implementation |
| `vid_voodoo_render.h` | Modified to include ARM64 codegen conditionally |
| `vid_voodoo_render.c` | Modified to select codegen at runtime |

### 2.2 ARM64 Register Allocation

| ARM64 Register | Usage |
| :--- | :--- |
| **X0-X7** | Scratch/arguments (caller-saved) |
| **X8** | Scratch |
| **X9-X15** | `logtable`, `alookup`, lookup table pointers |
| **X16-X17** | Intra-procedure-call scratch |
| **X19-X28** | Callee-saved general purpose |
| **X29** | Frame pointer |
| **X30** | Link register |
| **Dedicated** | |
| **X19** | `voodoo_state_t*` pointer |
| **X20** | `voodoo_params_t*` pointer |
| **X21** | `real_y` |
| **X22-X28** | Lookup table pointers |
| **NEON** | |
| **V0-V7** | Scratch/computation (caller-saved) |
| **V8-V15** | Callee-saved, used for constants |
| **V16-V31** | Scratch/pixel data |

### 2.3 Data Type Mapping

The Voodoo pipeline primarily works with:

*   Packed 8-bit RGBA (32-bit pixels)
*   Unpacked 16-bit channels (for math without overflow)

**NEON register usage:**

*   **Lower 64 bits (D registers)**: 4x16-bit RGBA channels
*   **Full 128 bits (Q registers)**: 8x16-bit for dual-pixel processing

---

## 3. Instruction Mapping Tables

### 3.1 General Purpose Instructions

| x86-64 | ARM64 | Notes |
| :--- | :--- | :--- |
| `MOV Rd, imm32` | `MOV Wd, imm` / `MOVZ` + `MOVK` | ARM64 requires sequence for large immediates |
| `MOV Rd, [base+off]` | `LDR Wd, [Xn, #off]` | Offset must be scaled |
| `MOV [base+off], Rs` | `STR Ws, [Xn, #off]` | |
| `ADD Rd, Rs, imm` | `ADD Wd, Wn, #imm` | 12-bit immediate limit |
| `SUB Rd, Rs, imm` | `SUB Wd, Wn, #imm` | |
| `CMP Ra, Rb` | `CMP Wa, Wb` | Sets NZCV flags |
| `CMOV* Rd, Rs` | `CSEL Wd, Wn, Wm, cond` | ARM64 conditional select |
| `SAR Rd, imm` | `ASR Wd, Wn, #imm` | Arithmetic shift right |
| `SHR Rd, imm` | `LSR Wd, Wn, #imm` | Logical shift right |
| `SHL Rd, imm` | `LSL Wd, Wn, #imm` | Logical shift left |
| `IMUL Rd, Rs` | `MUL Wd, Wn, Wm` | |
| `IDIV Rs` | `SDIV Wd, Wn, Wm` | No remainder - requires additional ops |
| `BSR Rd, Rs` | `CLZ Wd, Wn` + `SUB` | Count leading zeros, then subtract from 31 |
| `TEST Ra, Rb` | `TST Wa, Wb` | AND setting flags only |
| `RET` | `RET (X30)` | Branch to link register |

### 3.2 Branch Instructions

| x86-64 | ARM64 | Notes |
| :--- | :--- | :--- |
| `JMP rel32` | `B label` | +/-128MB range |
| `JE/JZ` | `B.EQ` | |
| `JNE/JNZ` | `B.NE` | |
| `JA/JNBE` | `B.HI` | Unsigned greater |
| `JAE/JNC` | `B.HS/B.CS` | Unsigned >= |
| `JB/JC` | `B.LO/B.CC` | Unsigned less |
| `JBE/JNA` | `B.LS` | Unsigned <= |
| `JG/JNLE` | `B.GT` | Signed greater |
| `JGE/JNL` | `B.GE` | Signed >= |
| `JL/JNGE` | `B.LT` | Signed less |
| `JLE/JNG` | `B.LE` | Signed <= |

---

## 4. SIMD Translation Guide (SSE2 to NEON)

### 4.1 Data Movement

| SSE2 | NEON | Notes |
| :--- | :--- | :--- |
| `MOVD XMM, r32` | `FMOV Sd, Wn` or `INS Vd.S[0], Wn` | Move GP to SIMD |
| `MOVD r32, XMM` | `FMOV Wd, Sn` or `UMOV Wd, Vn.S[0]` | Move SIMD to GP |
| `MOVQ XMM, m64` | `LDR Dd, [Xn]` | Load 64-bit |
| `MOVDQU XMM, m128` | `LDR Qd, [Xn]` | Load 128-bit |
| `MOVDQA` | Same as above | ARM64 handles alignment gracefully |

### 4.2 Unpack/Pack Operations

| SSE2 | NEON | Notes |
| :--- | :--- | :--- |
| `PUNPCKLBW` | `ZIP1 Vd.16B, Vn.16B, Vm.16B` | Interleave low bytes |
| `PUNPCKHBW` | `ZIP2 Vd.16B, Vn.16B, Vm.16B` | Interleave high bytes |
| `PUNPCKLWD` | `ZIP1 Vd.8H, Vn.8H, Vm.8H` | Interleave low words |
| `PACKUSWB` | `SQXTUN Vd.8B, Vn.8H` + merge | Saturating narrow unsigned |
| `PACKSSDW` | `SQXTN Vd.4H, Vn.4S` | Saturating narrow signed |

Example: Byte unpack to 16-bit
```armasm
; x86-64: PUNPCKLBW XMM0, XMM2 (XMM2=zero)
; ARM64 equivalent:
    UXTL    V0.8H, V0.8B        ; Zero-extend bytes to halfwords
```

### 4.3 Arithmetic Operations

| SSE2 | NEON | Notes |
| :--- | :--- | :--- |
| `PADDW` | `ADD Vd.8H, Vn.8H, Vm.8H` | Add 16-bit |
| `PSUBW` | `SUB Vd.8H, Vn.8H, Vm.8H` | Subtract 16-bit |
| `PMULLW` | `MUL Vd.8H, Vn.8H, Vm.8H` | Multiply low 16-bit |
| `PMULHW` | `SMULL` + extract high | Multiply high 16-bit (signed) |
| `PMULHUW` | `UMULL` + extract high | Multiply high 16-bit (unsigned) |
| `PADDUSW` | `UQADD Vd.8H, Vn.8H, Vm.8H` | Saturating add unsigned |
| `PSUBUSW` | `UQSUB Vd.8H, Vn.8H, Vm.8H` | Saturating subtract unsigned |

Example: `PMULLW` + `PMULHW` pattern
```armasm
; x86-64: Multiply XMM0 * XMM3, get low and high products
    PMULLW XMM1, XMM0, XMM3
    PMULHW XMM4, XMM0, XMM3
    PUNPCKLWD XMM1, XMM4      ; Interleave to get 32-bit results
    PSRAD XMM1, 8             ; Shift right
; ARM64 equivalent:
    SMULL   V1.4S, V0.4H, V3.4H   ; Signed multiply to 32-bit
    SSHR    V1.4S, V1.4S, #8      ; Arithmetic right shift
    SQXTN   V1.4H, V1.4S          ; Narrow back to 16-bit
```

### 4.4 Shift Operations

| SSE2 | NEON | Notes |
| :--- | :--- | :--- |
| `PSRLW imm` | `USHR Vd.8H, Vn.8H, #imm` | Logical right shift |
| `PSRAW imm` | `SSHR Vd.8H, Vn.8H, #imm` | Arithmetic right shift |
| `PSLLW imm` | `SHL Vd.8H, Vn.8H, #imm` | Left shift |
| `PSRLD imm` | `USHR Vd.4S, Vn.4S, #imm` | 32-bit logical right |
| `PSRAD imm` | `SSHR Vd.4S, Vn.4S, #imm` | 32-bit arithmetic right |
| `PSLLDQ imm` | `EXT Vd.16B, Vzero, Vn.16B, #(16-imm)` | Byte shift left |
| `PSRLDQ imm` | `EXT Vd.16B, Vn.16B, Vzero, #imm` | Byte shift right |

### 4.5 Logical Operations

| SSE2 | NEON | Notes |
| :--- | :--- | :--- |
| `PAND` | `AND Vd.16B, Vn.16B, Vm.16B` | |
| `POR` | `ORR Vd.16B, Vn.16B, Vm.16B` | |
| `PXOR` | `EOR Vd.16B, Vn.16B, Vm.16B` | |
| `PANDN` | `BIC Vd.16B, Vm.16B, Vn.16B` | Note operand order |

### 4.6 Shuffle/Permute Operations

| SSE2 | NEON | Notes |
| :--- | :--- | :--- |
| `PSHUFLW XMM, imm` | `TBL` with index vector | Complex shuffle |
| `PSHUFHW XMM, imm` | `TBL` with index vector | |
| `PSHUFD XMM, imm` | `TBL` or `DUP`/`EXT` combos | |
| `PINSRW XMM, r32, imm` | `INS Vd.H[imm], Wn` | Insert halfword |
| `PEXTRW r32, XMM, imm` | `UMOV Wd, Vn.H[imm]` | Extract halfword |

Example: Broadcast single element
```armasm
; x86-64: PSHUFLW XMM0, XMM3, 0xFF  ; Broadcast element 3
; ARM64:
    DUP     V0.4H, V3.H[3]          ; Duplicate element 3 to all lanes
```

---

## 5. Code Structure and #ifdef Strategy

### 5.1 Platform Detection (Following 86Box Conventions)

Based on codebase analysis, 86Box uses:

```c
#if defined(__aarch64__) || defined(_M_ARM64)
    // ARM64-specific code
    #if defined(__APPLE__)
        // macOS ARM64-specific (JIT signing, MAP_JIT)
    #endif
#elif defined(__x86_64__) || defined(_M_X64)
    // x86-64-specific code
#else
    #define NO_CODEGEN
#endif
```

### 5.2 File Organization

*   `src/include/86box/`
    *   `vid_voodoo_codegen_x86-64.h` (existing)
    *   `vid_voodoo_codegen_arm64.h` **[NEW]**
    *   `vid_voodoo_render.h` (modified)
*   `src/video/`
    *   `vid_voodoo_render.c` (modified)

### 5.3 Render Header Modification

```c
// vid_voodoo_render.h
#if defined(__aarch64__) || defined(_M_ARM64)
#    include <86box/vid_voodoo_codegen_arm64.h>
#elif defined(__x86_64__) || defined(_M_X64)
#    include <86box/vid_voodoo_codegen_x86-64.h>
#else
#    define NO_CODEGEN
#endif
```

---

## 6. Implementation Roadmap (Phases)

| Phase | Description | Estimated Time |
| :--- | :--- | :--- |
| **Phase 1: Foundation** | Create `vid_voodoo_codegen_arm64.h`, emission macros, prologue/epilogue, encoders. | 2-3 days |
| **Phase 2: SIMD Ops** | Port NEON operations (UXTL, vector arithmetic, shifts, logic), complex patterns. | 3-4 days |
| **Phase 3: Control Flow** | Port conditional ops (Depth test, Alpha test, Chroma key, Stipple), branch handling. | 2 days |
| **Phase 4: Texture Ops** | Port `codegen_texture_fetch()` (S/T coord, LOD, W divide, Bilinear filtering). | 3-4 days |
| **Phase 5: Full Pipeline** | Color combine, Alpha combine, Fog, Alpha blending, Depth R/W, RGB565 output. | 3-4 days |
| **Phase 6: macOS JIT** | W^X enforcement (MAP_JIT, `pthread_jit_write_protect_np`), code signing. | 1-2 days |
| **Phase 7: Testing** | Interpreter comparison, visual regression, profiling, optimization. | 3-5 days |

**Total Estimated Time: 17-24 days**

---

## 7. macOS JIT Requirements

### 7.1 Memory Management

```c
#if defined(__APPLE__) && defined(__aarch64__)
#include <pthread.h>
#include <libkern/OSCacheControl.h>
// Allocate JIT memory
void *code_buffer = mmap(NULL, size, 
    PROT_READ | PROT_WRITE | PROT_EXEC,
    MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
// Before writing code
pthread_jit_write_protect_np(0);  // Enable write
// ... generate code ...
// After writing code
pthread_jit_write_protect_np(1);  // Enable execute
sys_icache_invalidate(code_buffer, code_size);  // Flush I-cache
#endif
```

### 7.2 Entitlements

The application must have these entitlements in `Entitlements.plist`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "...">
<plist version="1.0">
<dict>
    <key>com.apple.security.cs.allow-jit</key>
    <true/>
    <key>com.apple.security.cs.allow-unsigned-executable-memory</key>
    <true/>
</dict>
</plist>
```

### 7.3 Existing Infrastructure

86Box already has macOS JIT support in `codegen_backend_arm64.c`:

```c
#if defined(__APPLE__) && defined(__aarch64__) && defined(NEW_DYNAREC_BACKEND)
    dynarec_backend = BACKEND_ARM64_APPLE;
#endif
```

The Voodoo codegen should follow the same pattern used by the CPU dynarec.

---

## 8. Hardware Compatibility Matrix

| Device | TMUs | Tiled FB | Notes |
| :--- | :--- | :--- | :--- |
| **Voodoo 1** | 1 | No | 3D-only, pass-through 2D |
| **Voodoo 2** | 2 | No | SLI capable |
| **Banshee** | 1 | Yes | Integrated 2D/3D |
| **Voodoo 3** | 1 | Yes | Varying clocks (1000/2000/3000/3500) |
| **Velocity 100/200** | 1 | Yes | Budget Voodoo 3 |

All devices share the same pixel pipeline codegen, with variations handled by:
*   `voodoo->dual_tmus` flag for TMU count
*   `params->col_tiled` / `params->aux_tiled` for tiled addressing
*   `voodoo->trexInit1` for feature flags

---

## 9. Interpreter Fallback and User Control

### 9.1 Runtime Selection

```c
// In voodoo device configuration
void voodoo_set_dynarec_mode(voodoo_t *voodoo, int use_dynarec) {
    voodoo->use_dynarec = use_dynarec;
    
#if defined(__aarch64__) || defined(_M_ARM64)
    if (use_dynarec) {
        // Initialize ARM64 codegen
        voodoo_codegen_init_arm64(voodoo);
    }
#elif defined(__x86_64__) || defined(_M_X64)
    if (use_dynarec) {
        voodoo_codegen_init(voodoo);
    }
#endif
}
```

### 9.2 UI Configuration

A dropdown in the Voodoo device settings:
*   "Rendering Mode: [Dynamic Recompiler] / [Interpreter]"
*   Stored in VM configuration as `voodoo_use_dynarec = 1/0`
*   Default: Dynarec enabled where supported

---

## 10. Testing Plan

### 10.1 Unit Tests

Testing will rely on:
*   **Synthetic pixel tests**: Create test harness that runs same pixel data through interpreter and dynarec, comparing outputs.
*   **Instruction encoder tests**: Verify ARM64 instruction encoding produces correct opcodes.

### 10.2 Integration Tests

*   **Interpreter parity**: Run games with dynarec disabled, capture reference frames.
*   **Dynarec comparison**: Run same games with dynarec enabled, compare frames.
*   **All Voodoo models**: Test with each supported device type.

### 10.3 Visual Regression Tests

Recommended test games/demos:
*   GLQuake (Voodoo 1/2 compatible)
*   Unreal (dual TMU testing)
*   3DMark 99 (comprehensive feature coverage)

### 10.4 Manual Verification Steps

1.  **Build 86Box for macOS ARM64**:
    ```bash
    mkdir build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make -j$(sysctl -n hw.ncpu)
    ```
2.  Create test VM with Voodoo 3 card.
3.  Install Windows 98 + GLQuake.
4.  Run with dynarec disabled - verify rendering.
5.  Run with dynarec enabled - compare visual output.
6.  Check console for any crashes or errors.

---

## 11. Risk Assessment and Mitigation

| Risk | Likelihood | Impact | Mitigation |
| :--- | :--- | :--- | :--- |
| **NEON precision** | Medium | Medium | Use exact same math (avoid fused ops where precision matters). |
| **Branch range limits** | Low | High | Implement veneer/trampoline for long jumps. |
| **W^X violations** | Medium | High | Follow existing CPU dynarec patterns exactly. |
| **Performance regression** | Low | Medium | Profile early, optimize NEON usage. |
| **Encoding bugs** | Medium | High | Test each encoder in isolation before integration. |
| **Tiled FB bugs** | Medium | Medium | Extensive testing with Voodoo 3/Banshee. |

---

## 12. Performance Expectations

*   **Expected speedup over interpreter**: 3-10x (same as x86-64 dynarec).
*   **Key optimization opportunities**:
    *   NEON has 32 vector registers vs x86-64's 16 - fewer spills.
    *   Fused multiply-add instructions (where precision allows).
    *   Better branch prediction on M-series chips.

---

## 13. Verification Plan

### Build Verification
```bash
# Build command (macOS ARM64)
cd /Users/anthony/projects/code/86Box-voodoo-dynarec-v2
mkdir build-arm64 && cd build-arm64
cmake .. -DCMAKE_BUILD_TYPE=Debug -DVOODOO_DYNAREC=ON
make -j8
# Verify build includes ARM64 codegen
nm 86Box.app/Contents/MacOS/86Box | grep voodoo_codegen_init
```

### Functional Verification
*   **Interpreter baseline**: Run test suite with `VOODOO_DYNAREC=0` environment variable.
*   **Dynarec test**: Run same test suite with `VOODOO_DYNAREC=1`.
*   **Compare**: Frame hashes should match (or be within acceptable tolerance).

---

## 14. Reference Links

*   [ARM64 ISA](https://developer.arm.com/documentation/ddi0487/latest/)
*   [ARM NEON Guide](https://developer.arm.com/documentation/den0018/latest/)
*   [Apple Silicon JIT Memory](https://developer.apple.com/documentation/apple-silicon/porting-just-in-time-compilers-to-apple-silicon)
*   [3dfx Voodoo Hardware Reference](http://voodoo.mirrors.sk/files/voodoo12v1.1.pdf)
*   [86Box Source Code](https://github.com/86Box/86Box)

---

## Proposed Changes Summary

### New Files
*   `src/include/86box/vid_voodoo_codegen_arm64.h` - ARM64 NEON codegen (approx. 3500 lines mirroring x86-64 version)

### Modified Files
*   `src/include/86box/vid_voodoo_render.h` - Add ARM64 include path and remove NO_CODEGEN for ARM64
*   `src/video/vid_voodoo.c` - Add runtime dynarec selection
*   `CMakeLists.txt` - Add ARM64 codegen to build
