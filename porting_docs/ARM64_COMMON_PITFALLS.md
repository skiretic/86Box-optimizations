# ARM64 Common Pitfalls and Solutions

**ARM64-specific gotchas when porting from x86-64.**

## 1. Immediate Value Encoding Restrictions

### Problem

ARM64 has strict limits on immediate values that can be encoded directly in instructions.

### x86-64 Behavior
```c
// Can use any 32-bit immediate
ADD EAX, 0x12345678  // Works fine
```

### ARM64 Restrictions
```c
// ADD immediate is limited to 12 bits (0-4095), optionally shifted by 12
ADD W0, W1, #4096     // ERROR: Out of range
ADD W0, W1, #0x1234   // ERROR: > 0xFFF
```

### Solution
```c
static inline void emit_add_imm(int dst, int src, uint32_t imm) {
    if (imm <= 0xfff) {
        // Direct 12-bit immediate
        emit32(ARM64_ADD_IMM_W | Rd(dst) | Rn(src) | IMM12(imm));
    } else if ((imm & 0xfff) == 0 && (imm >> 12) <= 0xfff) {
        // 12-bit immediate shifted left by 12
        emit32(ARM64_ADD_IMM_W | Rd(dst) | Rn(src) | IMM12(imm >> 12) | SHIFT_12(1));
    } else {
        // Load into scratch register first
        emit_mov_imm64(REG_X16, imm);
        emit32(ARM64_ADD_REG | Rd(dst) | Rn(src) | Rm(REG_W16));
    }
}
```

## 2. Load/Store Offset Scaling

### Problem

ARM64 load/store offsets must be aligned to the access size.

### x86-64 Behavior
```c
MOV EAX, [RDI + 13]  // Any offset works
```

### ARM64 Requirement
```c
LDR W0, [X19, #13]   // ERROR: Not 4-byte aligned
LDR W0, [X19, #12]   // OK: 12 is divisible by 4
LDR B0, [X19, #13]   // OK: Byte access, any offset
```

### Solution
```c
static inline void emit_ldr_offset(int dst, int base, int offset) {
    if (offset >= 0 && offset < 16384 && (offset & 3) == 0) {
        // Offset is aligned and in range
        emit32(ARM64_LDR_IMM_W | Rt(dst) | Rn(base) | ((offset >> 2) << 10));
    } else {
        // Use register offset
        emit_mov_imm64(REG_X16, offset);
        emit32(ARM64_LDR_REG_W | Rt(dst) | Rn(base) | Rm(REG_X16));
    }
}
```

**Offset encoding per size**:
- Byte (B): Offset can be 0-4095 (no scaling)
- Halfword (H): Offset must be multiple of 2, encoded as offset/2
- Word (W): Offset must be multiple of 4, encoded as offset/4
- Doubleword (X): Offset must be multiple of 8, encoded as offset/8

## 3. Condition Code Differences

### Problem

ARM64 and x86-64 have different condition code mappings.

### Comparison

| x86-64 | ARM64 | Meaning |
|:-------|:------|:--------|
| JE/JZ | B.EQ | Equal / Zero |
| JNE/JNZ | B.NE | Not equal / Not zero |
| JA | B.HI | Unsigned greater |
| JAE | B.HS (CS) | Unsigned greater or equal |
| JB | B.LO (CC) | Unsigned less |
| JBE | B.LS | Unsigned less or equal |
| JG | B.GT | Signed greater |
| JGE | B.GE | Signed greater or equal |
| JL | B.LT | Signed less |
| JLE | B.LE | Signed less or equal |

### Gotcha: Carry Flag

x86-64 "above" (A) and "below" (B) map to ARM64 "higher" (HI) and "lower" (LO), but the carry flag is **inverted**:

```c
// x86-64: JB (jump if below) tests CF=1
// ARM64: B.LO (branch if lower) tests C=0

// x86-64: JAE (jump if above or equal) tests CF=0
// ARM64: B.HS (branch if higher or same) tests C=1
```

### Solution
```c
// When porting x86-64 comparisons, verify the condition carefully
// x86-64:
CMP EAX, 100
JA  skip      // Jump if EAX > 100 (unsigned)

// ARM64:
CMP W0, #100
B.HI skip     // Branch if W0 > 100 (unsigned)
```

## 4. NEON Lane Indexing

### Problem

NEON lane indexing differs from SSE element selection.

### x86-64 PINSRW
```c
// Insert word into XMM register at index
PINSRW XMM0, EAX, 3  // Insert into element 3 (0-based)
```

### ARM64 INS
```c
// Lane index is part of the opcode encoding
INS V0.H[3], W0  // Insert into lane 3

// Encoding: different bits for different sizes
#define ARM64_INS_H  0x6e020400  // Halfword insert
uint32_t index = (lane_num & 7) << 19;  // Bits 19-21 for .H
emit32(ARM64_INS_H | Rd(vreg) | Rn(gpreg) | index);
```

### Element Size Encoding

| Type | Lanes | Index bits | Index shift |
|:-----|:------|:-----------|:------------|
| .B (byte) | 0-15 | 19-22 | << 19 |
| .H (halfword) | 0-7 | 19-21 | << 19 |
| .S (word) | 0-3 | 20-21 | << 20 |
| .D (doubleword) | 0-1 | 21 | << 21 |

## 5. Stack Alignment Requirements

### Problem

ARM64 requires 16-byte stack alignment at function calls.

### x86-64 Behavior
```c
// Stack just needs to be 8-byte aligned on call
PUSH RBP  // 8 bytes
```

### ARM64 Requirement
```c
// Stack pointer must be 16-byte aligned at all times
STP X29, X30, [SP, #-224]!  // Must be multiple of 16
```

### Common Error
```bash
# SIGBUS crash with odd stack pointer
(lldb) register read sp
sp = 0x00007ff7bfefff08  # Not 16-byte aligned (ends in 8)
```

### Solution
```c
// Ensure frame size is multiple of 16
#define FRAME_SIZE 224  // Must be multiple of 16

// Prologue
emit32(0xa98f7bfd);  // STP X29, X30, [SP, #-224]!

// Epilogue
emit32(0xa8cf7bfd);  // LDP X29, X30, [SP], #224
```

## 6. Register Clobbering

### Problem

ARM64 calling convention has different caller/callee-saved registers than x86-64.

### x86-64 Callee-Saved
RBX, RBP, R12-R15

### ARM64 Callee-Saved
X19-X28, X29 (FP), X30 (LR)
V8-V15 (lower 64 bits only)

### Gotcha

X16 and X17 are **intra-procedure-call scratch** registers. They can be clobbered by:
- Veneers (long branch trampolines)
- Dynamic linker
- PLT stubs

### Solution
```c
// Use X16/X17 only as temporary scratch
emit_mov_imm64(REG_X16, addr);  // OK: Immediately consumed
emit32(ARM64_LDR_REG_X | Rt(REG_X0) | Rn(REG_X19) | Rm(REG_X16));

// DON'T use X16/X17 for values that must survive across instructions
emit_mov_imm64(REG_X16, value);
// ... other instructions ...
emit32(ARM64_ADD_REG | Rd(REG_X0) | Rn(REG_X16) | Rm(REG_X1));  // BAD: X16 may be clobbered
```

## 7. NEON vs SSE Saturation

### Problem

NEON and SSE handle saturation differently.

### SSE2 PADDUSW
```c
// Saturating add: clamps to 0xFFFF
PADDUSW XMM0, XMM1  // If sum > 0xFFFF, result = 0xFFFF
```

### NEON UQADD
```c
// Same behavior
UQADD V0.8H, V0.8H, V1.8H  // Saturating unsigned add
```

### Gotcha: PACKUSWB

SSE2 `PACKUSWB` saturates **signed** values to unsigned:
```c
// PACKUSWB: Saturate signed 16-bit to unsigned 8-bit
// Input: -1 (0xFFFF) → Output: 0
// Input: 300 → Output: 255
```

NEON has two separate instructions:
```c
SQXTUN V0.8B, V0.8H  // Signed to unsigned with saturation (use this)
UQXTN V0.8B, V0.8H   // Unsigned to unsigned with saturation
```

## 8. Shift Amount Encoding

### Problem

ARM64 shift amounts are encoded differently per element size.

### SSE2 PSRLW
```c
PSRLW XMM0, 8  // Shift all 16-bit elements right by 8
```

### NEON USHR
```c
// Shift amount must be encoded based on element size
USHR V0.8H, V0.8H, #8

// Encoding for 16-bit elements:
#define V_SHIFT_4H(sh)  (((sh) | 0x10) << 16)
emit32(ARM64_USHR_Q | Rd(REG_V0) | Rn(REG_V0) | V_SHIFT_4H(8));
```

### Shift Amount Encoding

| Element Size | Encoding | Valid Range |
|:-------------|:---------|:------------|
| .8B / .16B (8-bit) | `(shift \| 0x08) << 16` | 1-8 |
| .4H / .8H (16-bit) | `(shift \| 0x10) << 16` | 1-16 |
| .2S / .4S (32-bit) | `(shift \| 0x20) << 16` | 1-32 |
| .2D (64-bit) | `(shift \| 0x40) << 16` | 1-64 |

## 9. Branch Range Limitations

### Problem

ARM64 conditional branches have limited range.

### Branch Ranges
- `B.cond` (conditional): ±1MB (19-bit signed offset)
- `B` (unconditional): ±128MB (26-bit signed offset)
- `CBZ/CBNZ`: ±1MB (19-bit signed offset)

### When to Worry

For JIT code generating large blocks (>1MB):
```c
// May be out of range if block is large
int patch = emit_branch_cond(COND_EQ);
// ... thousands of instructions ...
patch_branch(patch);  // ERROR: Offset > ±1MB
```

### Solution
```c
// Use branch inversion with unconditional branch
// Instead of: B.EQ far_label
// Do:
B.NE skip      // Invert condition
B far_label    // Unconditional has longer range
skip:
```

## 10. W vs X Register Confusion

### Problem  

ARM64 has separate 32-bit (W) and 64-bit (X) registers, same number.

### Common Error
```c
// Mixing W and X registers
LDR W0, [X19, #offset]  // Load 32-bit into W0
ADD X0, X0, #100        // ERROR: Should use W0 or extend first
```

### Rules
- W registers: 32-bit operations, zero-extend to 64-bit
- X registers: 64-bit operations
- W0 and X0 refer to the same physical register

### Solution
```c
// Be consistent with register width
LDR W0, [X19, #offset]  // 32-bit load
ADD W0, W0, #100        // 32-bit add (correct)

// Or extend to 64-bit
LDR W0, [X19, #offset]  // 32-bit load (zero-extends to X0)
ADD X0, X0, #100        // 64-bit add (also correct)
```

## General Debugging Tips

1. **Always disassemble generated code**: Use LLDB `dis` command to verify instruction bytes
2. **Check alignment**: SIGBUS often means misaligned stack or data
3. **Verify register allocation**: Use `register read` to check callee-saved registers aren't clobbered
4. **Test immediates**: For any constant, verify it fits encoding restrictions
5. **Compare against interpreter**: Pixel-perfect comparison catches subtle SIMD errors

## See Also

- Main guide: `VOODOO_ARM64_PORT_GUIDE.md`
- Worked example: `WORKED_EXAMPLE_PMULLW.md`
- ARM64 ISA manual: https://developer.arm.com/documentation/ddi0487/latest/
