# Worked Example: Porting PMULLW to ARM64

**Complete step-by-step walkthrough of porting a single SIMD operation from x86-64 to ARM64.**

## 1. Understanding the x86-64 Operation

**SSE2 Instruction**: `PMULLW XMM0, XMM1`
- Multiplies 8 pairs of 16-bit values
- Stores low 16 bits of each result
- `XMM0[i] = (XMM0[i] * XMM1[i]) & 0xFFFF`

**x86-64 Code in vid_voodoo_codegen_x86-64.h**:
```c
static inline void emit_pmullw_xmm_xmm(int dst, int src) {
    addbyte(0x66);  // Operand size prefix
    addbyte(0x0f);  // Two-byte opcode
    addbyte(0xd5);  // PMULLW
    addbyte(0xc0 | (dst << 3) | src);  // ModRM byte
}

// Used for alpha blending
emit_pmullw_xmm_xmm(REG_XMM0, REG_XMM2);  // XMM0 *= XMM2 (low 16 bits)
```

## 2. Finding the ARM64 Equivalent

**NEON Instruction**: `MUL Vd.8H, Vn.8H, Vm.8H`
- Multiplies 8 pairs of 16-bit values (same as PMULLW)
- Stores low 16 bits of each result
- Exact functional equivalent

**From VOODOO_ARM64_PORT_GUIDE.md Part 4.10**:
```c
#define ARM64_MUL_4H  0x0e609c00   /* MUL Vd.4H, Vn.4H, Vm.4H */
```

For 8 elements (full 128-bit register), use `.8H` arrangement:
```c
#define ARM64_MUL_8H  0x4e609c00   /* MUL Vd.8H, Vn.8H, Vm.8H */
```

## 3. Encoding the ARM64 Instruction

**Instruction format**:
```
MUL Vd.8H, Vn.8H, Vm.8H
 31 30 29 28    24 23 22 21 20   16 15    10 9   5 4   0
┌──┬──┬──┬───────┬──┬──┬──┬───────┬────────┬─────┬─────┐
│0 │Q │0 │01110 │sz│1 │Rm│100111 │   Rn   │ Rd  │
└──┴──┴──┴───────┴──┴──┴──┴───────┴────────┴─────┴─────┘

Q=1 (128-bit, 8 elements)
sz=01 (16-bit halfword)
```

**Building the opcode**:
```c
// Base opcode for MUL .8H arrangement
#define ARM64_MUL_8H  0x4e609c00

// Register encoding (from Part 3)
#define Rd(x)  ((x) & 0x1f)         // Bits 0-4
#define Rn(x)  (((x) & 0x1f) << 5)  // Bits 5-9
#define Rm(x)  (((x) & 0x1f) << 16) // Bits 16-20
```

## 4. ARM64 Port Implementation

```c
// In vid_voodoo_codegen_arm64.h
static inline void emit_mul_8h(int dst, int src1, int src2) {
    emit32(ARM64_MUL_8H | Rd(dst) | Rn(src1) | Rm(src2));
}

// Usage (equivalent to x86-64 version)
emit_mul_8h(REG_V0, REG_V0, REG_V2);  // V0 *= V2 (element-wise, low 16 bits)
```

## 5. Verifying the Encoding

**Example**: `MUL V3.8H, V5.8H, V7.8H`

**Hand calculation**:
```
Rd=3, Rn=5, Rm=7

Instruction = 0x4e609c00 | (3) | (5 << 5) | (7 << 16)
            = 0x4e609c00 | 0x03 | 0xa0 | 0x70000
            = 0x4e679ca3
```

**Verify with LLDB**:
```bash
# After emitting the instruction
(lldb) x/1xw data->code_block
0x1234abcd: 0x4e679ca3

# Disassemble to confirm
(lldb) dis -s data->code_block -c 1
0x1234abcd: 4e679ca3  mul v3.8h, v5.8h, v7.8h
```

**Expected binary breakdown**:
```
0x4e679ca3 = 0100 1110 0110 0111 1001 1100 1010 0011
             │  │  │   │     │  │   │     │      │
             │  │  │   │     │  │   │     │      └─ Rd=3
             │  │  │   │     │  │   │     └──────── Rn=5
             │  │  │   │     │  │   └────────────── opcode
             │  │  │   │     │  └────────────────── Rm=7
             │  │  │   │     └───────────────────── sz=01 (16-bit)
             │  │  │   └─────────────────────────── opcode
             │  │  └─────────────────────────────── 0
             │  └────────────────────────────────── Q=1 (128-bit)
             └───────────────────────────────────── 0
```

## 6. Complete Function Context

**x86-64 version** (from existing dynarec):
```c
// Alpha blending: result = (color * alpha) >> 8
static void emit_alpha_blend_x86(void) {
    // XMM0 = pixel color (16-bit channels)
    // XMM2 = alpha value (duplicated to all channels)
    
    emit_pmullw_xmm_xmm(REG_XMM0, REG_XMM2);    // color * alpha
    emit_psrlw_imm(REG_XMM0, 8);                // shift right 8 bits
}
```

**ARM64 port**:
```c
// Alpha blending: result = (color * alpha) >> 8
static void emit_alpha_blend_arm64(void) {
    // V0 = pixel color (16-bit channels)
    // V2 = alpha value (duplicated to all channels)
    
    emit_mul_8h(REG_V0, REG_V0, REG_V2);        // color * alpha
    emit32(ARM64_USHR_Q | Rd(REG_V0) | Rn(REG_V0) | V_SHIFT_4H(8));  // >> 8
}
```

## 7. Testing the Port

**Test case**: Multiply two known vectors

```c
// Input:
// V0 = {100, 200, 300, 400, 500, 600, 700, 800}
// V2 = {2, 2, 2, 2, 2, 2, 2, 2}

// Expected output (low 16 bits of each product):
// V0 = {200, 400, 600, 800, 1000, 1200, 1400, 1600}
```

**Verification in LLDB**:
```bash
(lldb) b emit_alpha_blend_arm64
(lldb) run
(lldb) register read v0 --format uint16_t[]
# Before: {100, 200, 300, 400, 500, 600, 700, 800}
(lldb) ni  // Execute MUL instruction
(lldb) register read v0 --format uint16_t[]
# After: {200, 400, 600, 800, 1000, 1200, 1400, 1600}
```

**Correctness check script**:
```python
#!/usr/bin/env python3
# scripts/verify_mul.py
import numpy as np

# Input vectors
v0 = np.array([100, 200, 300, 400, 500, 600, 700, 800], dtype=np.uint16)
v2 = np.array([2, 2, 2, 2, 2, 2, 2, 2], dtype=np.uint16)

# Multiply (keeping low 16 bits)
result = (v0 * v2) & 0xFFFF

print("Expected:", result)
# Expected: [ 200  400  600  800 1000 1200 1400 1600]
```

## 8. Common Issues and Solutions

| Issue | Symptom | Solution |
|:------|:--------|:---------|
| Wrong arrangement | Only 4 results instead of 8 | Use `.8H` (Q=1) not `.4H` (Q=0) |
| Register encoding | SIGILL illegal instruction | Verify Rd, Rn, Rm are 0-31 |
| Overflow handling | Incorrect results for large values | NEON MUL keeps low 16 bits automatically |
| Signedness | Wrong sign extension | Use unsigned (UMULL) or signed (SMULL) as needed |

## 9. Integration Checklist

After porting PMULLW to MUL:

- [ ] Opcode constant defined in Part 4
- [ ] Emission function created
- [ ] Verified instruction encoding with LLDB
- [ ] Tested with known input vectors
- [ ] Compared output against interpreter
- [ ] Documented in SSE2→NEON translation table (Part 7.5)
- [ ] Updated migration checklist (Part 0.6)

## 10. Lesson Learned

This same process applies to **every SIMD operation**. The pattern is:

1. Understand x86-64 operation semantics
2. Find NEON equivalent instruction
3. Look up opcode constant
4. Encode instruction with register fields
5. Verify bytecode with LLDB disassembly
6. Test with known values
7. Compare against interpreter

## See Also

- Main guide: `VOODOO_ARM64_PORT_GUIDE.md` Part 7 (SSE2 to NEON Translation)
- Common pitfalls: `ARM64_COMMON_PITFALLS.md`
- Full opcode reference: Part 4 of main guide
