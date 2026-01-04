# ARM64 Voodoo Dynarec - Tactical Implementation Guide

This document provides **concrete, copy-paste-ready implementations** for the ARM64 Voodoo dynarec port. It supplements the high-level `implementation_plan.md` with the low-level details needed to actually write the code.

---

## 1. Emission Infrastructure

### 1.1 Emission Macros

The Voodoo codegen will use direct emission like the x86-64 version, but adapted for ARM64's fixed 32-bit instruction size:

```c
/* Basic emission - ARM64 is always 32-bit instructions */
#define emit32(val)                                 \
    do {                                            \
        *(uint32_t *)&code_block[block_pos] = val;  \
        block_pos += 4;                             \
    } while (0)

/* For loading 64-bit addresses, emit multiple instructions */
#define emit64_addr(reg, addr) emit_movz_movk_64(reg, (uint64_t)(uintptr_t)(addr))
```

### 1.2 Register Field Encoding Macros

These match the existing CPU dynarec patterns in `codegen_backend_arm64_ops.c`:

```c
/* Register field positions in ARM64 instructions */
#define Rd(x)                     (x)           /* Destination register: bits 0-4 */
#define Rn(x)                     ((x) << 5)    /* First source register: bits 5-9 */
#define Rm(x)                     ((x) << 16)   /* Second source register: bits 16-20 */
#define Rt(x)                     (x)           /* Transfer register (same as Rd) */
#define Rt2(x)                    ((x) << 10)   /* Second transfer register */

/* Immediate field positions */
#define IMM12(imm)                ((imm) << 10)         /* 12-bit immediate: bits 10-21 */
#define IMM16(imm)                ((imm) << 5)          /* 16-bit immediate: bits 5-20 */
#define SHIFT_12(sh)              ((sh) << 22)          /* Immediate shift (0=none, 1=LSL#12) */
#define HW(hw)                    ((hw) << 21)          /* Halfword position for MOVK */

/* Shift amount for data processing */
#define SHIFT_IMM6(sh)            ((sh) << 10)

/* Branch offsets */
#define OFFSET19(off)             (((off >> 2) << 5) & 0x00ffffe0)  /* Conditional branch */
#define OFFSET26(off)             ((off >> 2) & 0x03ffffff)         /* Unconditional branch */

/* Vector shift amounts */
#define V_SHIFT_4H(sh)            (((sh) | 0x10) << 16)    /* For 16-bit elements */
#define V_SHIFT_2S(sh)            (((sh) | 0x20) << 16)    /* For 32-bit elements */
```

---

## 2. Opcode Constants

### 2.1 General Purpose Instructions

```c
/* Data Processing - Immediate */
#define ARM64_ADD_IMM_W           0x11000000   /* ADD Wd, Wn, #imm */
#define ARM64_ADD_IMM_X           0x91000000   /* ADD Xd, Xn, #imm */
#define ARM64_SUB_IMM_W           0x51000000   /* SUB Wd, Wn, #imm */
#define ARM64_SUB_IMM_X           0xd1000000   /* SUB Xd, Xn, #imm */
#define ARM64_CMP_IMM_W           0x71000000   /* CMP Wn, #imm (SUBS WZR, Wn, #imm) */
#define ARM64_CMP_IMM_X           0xf1000000   /* CMP Xn, #imm (SUBS XZR, Xn, #imm) */

/* Move Wide */
#define ARM64_MOVZ_W              0x52800000   /* MOVZ Wd, #imm16, LSL #hw */
#define ARM64_MOVZ_X              0xd2800000   /* MOVZ Xd, #imm16, LSL #hw */
#define ARM64_MOVK_W              0x72800000   /* MOVK Wd, #imm16, LSL #hw */
#define ARM64_MOVK_X              0xf2800000   /* MOVK Xd, #imm16, LSL #hw */

/* Data Processing - Register */
#define ARM64_ADD_REG             0x0b000000   /* ADD Wd, Wn, Wm, shift #imm */
#define ARM64_ADD_REG_X           0x8b000000   /* ADD Xd, Xn, Xm, shift #imm */
#define ARM64_SUB_REG             0x4b000000   /* SUB Wd, Wn, Wm, shift #imm */
#define ARM64_MUL                 0x1b007c00   /* MUL Wd, Wn, Wm */
#define ARM64_SDIV                0x1ac00c00   /* SDIV Wd, Wn, Wm */
#define ARM64_LSL_REG             0x1ac02000   /* LSLV Wd, Wn, Wm */
#define ARM64_LSR_REG             0x1ac02400   /* LSRV Wd, Wn, Wm */
#define ARM64_ASR_REG             0x1ac02800   /* ASRV Wd, Wn, Wm */
#define ARM64_CLZ                 0x5ac01000   /* CLZ Wd, Wn */

/* Logical - Register */
#define ARM64_AND_REG             0x0a000000   /* AND Wd, Wn, Wm */
#define ARM64_ORR_REG             0x2a000000   /* ORR Wd, Wn, Wm */
#define ARM64_EOR_REG             0x4a000000   /* EOR Wd, Wn, Wm */
#define ARM64_TST_REG             0x6a000000   /* TST Wn, Wm = ANDS WZR, Wn, Wm */

/* Conditional Select */
#define ARM64_CSEL                0x1a800000   /* CSEL Wd, Wn, Wm, cond */
#define ARM64_CSEL_X              0x9a800000   /* CSEL Xd, Xn, Xm, cond */

/* Condition codes for CSEL */
#define COND_EQ                   0x0
#define COND_NE                   0x1
#define COND_CS                   0x2   /* Unsigned >= (also HS) */
#define COND_CC                   0x3   /* Unsigned <  (also LO) */
#define COND_MI                   0x4
#define COND_PL                   0x5
#define COND_VS                   0x6
#define COND_VC                   0x7
#define COND_HI                   0x8   /* Unsigned > */
#define COND_LS                   0x9   /* Unsigned <= */
#define COND_GE                   0xa
#define COND_LT                   0xb
#define COND_GT                   0xc
#define COND_LE                   0xd

#define CSEL_COND(c)              ((c) << 12)

/* Bitfield operations */
#define ARM64_UBFM                0x53000000   /* UBFM (LSR imm, UBFX, UXTB, etc.) */
#define ARM64_SBFM                0x13000000   /* SBFM (ASR imm, SBFX, SXTB, etc.) */
```

### 2.2 Load/Store Instructions

```c
/* Load/Store - Unsigned Offset */
#define ARM64_LDR_IMM_W           0xb9400000   /* LDR Wt, [Xn, #imm] */
#define ARM64_LDR_IMM_X           0xf9400000   /* LDR Xt, [Xn, #imm] */
#define ARM64_LDRB_IMM            0x39400000   /* LDRB Wt, [Xn, #imm] */
#define ARM64_LDRH_IMM            0x79400000   /* LDRH Wt, [Xn, #imm] */
#define ARM64_LDRSB_IMM           0x39c00000   /* LDRSB Wt, [Xn, #imm] */
#define ARM64_LDRSH_IMM           0x79c00000   /* LDRSH Wt, [Xn, #imm] */
#define ARM64_LDRSW_IMM           0xb9800000   /* LDRSW Xt, [Xn, #imm] */

#define ARM64_STR_IMM_W           0xb9000000   /* STR Wt, [Xn, #imm] */
#define ARM64_STR_IMM_X           0xf9000000   /* STR Xt, [Xn, #imm] */
#define ARM64_STRB_IMM            0x39000000   /* STRB Wt, [Xn, #imm] */
#define ARM64_STRH_IMM            0x79000000   /* STRH Wt, [Xn, #imm] */

/* Load/Store - Register offset (with extend/shift) */
#define ARM64_LDR_REG_W           0xb8606800   /* LDR Wt, [Xn, Xm, SXTX] */
#define ARM64_LDR_REG_X           0xf8606800   /* LDR Xt, [Xn, Xm, SXTX] */
#define ARM64_LDRB_REG            0x38606800   /* LDRB Wt, [Xn, Xm, SXTX] */
#define ARM64_LDRH_REG            0x78606800   /* LDRH Wt, [Xn, Xm, SXTX] */
#define ARM64_STR_REG_W           0xb8206800   /* STR Wt, [Xn, Xm, SXTX] */
#define ARM64_STRB_REG            0x38206800   /* STRB Wt, [Xn, Xm, SXTX] */
#define ARM64_STRH_REG            0x78206800   /* STRH Wt, [Xn, Xm, SXTX] */
```

### 2.3 Branch Instructions

```c
/* Branches */
#define ARM64_B                   0x14000000   /* B label (PC-relative) */
#define ARM64_BL                  0x94000000   /* BL label */
#define ARM64_BR                  0xd61f0000   /* BR Xn */
#define ARM64_BLR                 0xd63f0000   /* BLR Xn */
#define ARM64_RET                 0xd65f0000   /* RET {Xn} - default X30 */

/* Conditional Branch */
#define ARM64_B_COND              0x54000000   /* B.cond label */
/* Add condition to bits 3:0, e.g.: ARM64_B_COND | COND_EQ gives B.EQ */

/* Compare and Branch */
#define ARM64_CBZ_W               0x34000000   /* CBZ Wt, label */
#define ARM64_CBZ_X               0xb4000000   /* CBZ Xt, label */
#define ARM64_CBNZ_W              0x35000000   /* CBNZ Wt, label */
#define ARM64_CBNZ_X              0xb5000000   /* CBNZ Xt, label */
```

### 2.4 NEON SIMD Instructions

```c
/* Vector Data Movement */
#define ARM64_FMOV_S_W            0x1e270000   /* FMOV Sd, Wn (GP->SIMD) */
#define ARM64_FMOV_W_S            0x1e260000   /* FMOV Wd, Sn (SIMD->GP) */
#define ARM64_INS_S               0x4e040400   /* INS Vd.S[idx], Wn */
#define ARM64_UMOV_S              0x0e043c00   /* UMOV Wd, Vn.S[idx] */

/* Vector Loads/Stores */
#define ARM64_LDR_Q               0x3dc00000   /* LDR Qt, [Xn, #imm] (128-bit) */
#define ARM64_LDR_D               0xfd400000   /* LDR Dt, [Xn, #imm] (64-bit) */
#define ARM64_LDR_S               0xbd400000   /* LDR St, [Xn, #imm] (32-bit) */
#define ARM64_STR_Q               0x3d800000   /* STR Qt, [Xn, #imm] */
#define ARM64_STR_D               0xfd000000   /* STR Dt, [Xn, #imm] */
#define ARM64_STR_S               0xbd000000   /* STR St, [Xn, #imm] */

/* Vector Arithmetic - 8x8-bit (8B) */
#define ARM64_ADD_8B              0x0e208400   /* ADD Vd.8B, Vn.8B, Vm.8B */
#define ARM64_SUB_8B              0x2e208400   /* SUB Vd.8B, Vn.8B, Vm.8B */

/* Vector Arithmetic - 4x16-bit (4H) */
#define ARM64_ADD_4H              0x0e608400   /* ADD Vd.4H, Vn.4H, Vm.4H */
#define ARM64_SUB_4H              0x2e608400   /* SUB Vd.4H, Vn.4H, Vm.4H */
#define ARM64_MUL_4H              0x0e609c00   /* MUL Vd.4H, Vn.4H, Vm.4H */

/* Vector Arithmetic - 2x32-bit (2S) */
#define ARM64_ADD_2S              0x0ea08400   /* ADD Vd.2S, Vn.2S, Vm.2S */
#define ARM64_SUB_2S              0x2ea08400   /* SUB Vd.2S, Vn.2S, Vm.2S */

/* Saturating Add/Subtract */
#define ARM64_UQADD_4H            0x2e600c00   /* UQADD Vd.4H, Vn.4H, Vm.4H */
#define ARM64_UQSUB_4H            0x2e602c00   /* UQSUB Vd.4H, Vn.4H, Vm.4H */
#define ARM64_SQADD_4H            0x0e600c00   /* SQADD Vd.4H, Vn.4H, Vm.4H */
#define ARM64_SQSUB_4H            0x0e602c00   /* SQSUB Vd.4H, Vn.4H, Vm.4H */

/* Vector Widening Multiply */
#define ARM64_SMULL_4S_4H         0x0e60c000   /* SMULL Vd.4S, Vn.4H, Vm.4H (lower 4 elements) */
#define ARM64_SMULL2_4S_8H        0x4e60c000   /* SMULL2 Vd.4S, Vn.8H, Vm.8H (upper 4 elements) */
#define ARM64_UMULL_4S_4H         0x2e60c000   /* UMULL Vd.4S, Vn.4H, Vm.4H */

/* Vector Logical */
#define ARM64_AND_V               0x0e201c00   /* AND Vd.8B, Vn.8B, Vm.8B */
#define ARM64_ORR_V               0x0ea01c00   /* ORR Vd.8B, Vn.8B, Vm.8B */
#define ARM64_EOR_V               0x2e201c00   /* EOR Vd.8B, Vn.8B, Vm.8B */
#define ARM64_BIC_V               0x0e601c00   /* BIC Vd.8B, Vn.8B, Vm.8B (Vn AND NOT Vm) */

/* Vector Shifts - Immediate */
#define ARM64_SHL_D               0x0f005400   /* SHL Vd.8B/4H/2S, Vn, #imm (64-bit arrangement) */
#define ARM64_SHL_Q               0x4f005400   /* SHL Vd.16B/8H/4S/2D, Vn, #imm (128-bit) */
#define ARM64_USHR_D              0x2f000400   /* USHR Vd, Vn, #imm (unsigned right shift) */
#define ARM64_USHR_Q              0x6f000400   /* USHR Vd, Vn, #imm (128-bit) */
#define ARM64_SSHR_D              0x0f000400   /* SSHR Vd, Vn, #imm (signed right shift) */
#define ARM64_SSHR_Q              0x4f000400   /* SSHR Vd, Vn, #imm (128-bit) */

/* Vector Extend */
#define ARM64_UXTL                0x2f00a400   /* UXTL Vd.8H, Vn.8B (zero-extend bytes to halfwords) */
#define ARM64_UXTL_4S             0x2f10a400   /* UXTL Vd.4S, Vn.4H (zero-extend halfwords to words) */
#define ARM64_SXTL                0x0f00a400   /* SXTL Vd.8H, Vn.8B (sign-extend) */

/* Vector Narrow */
#define ARM64_XTN                 0x0e212800   /* XTN Vd.8B, Vn.8H (truncate halfwords to bytes) */
#define ARM64_XTN_4H              0x0e612800   /* XTN Vd.4H, Vn.4S */
#define ARM64_SQXTN_8B            0x0e214800   /* SQXTN Vd.8B, Vn.8H (saturating) */
#define ARM64_SQXTUN_8B           0x2e212800   /* SQXTUN Vd.8B, Vn.8H (saturating unsigned) */
#define ARM64_UQXTN_8B            0x2e214800   /* UQXTN Vd.8B, Vn.8H */

/* Vector Interleave (Zip) */
#define ARM64_ZIP1_8B             0x0e003800   /* ZIP1 Vd.8B, Vn.8B, Vm.8B */
#define ARM64_ZIP1_4H             0x0e403800   /* ZIP1 Vd.4H, Vn.4H, Vm.4H */
#define ARM64_ZIP1_2S             0x0e803800   /* ZIP1 Vd.2S, Vn.2S, Vm.2S */
#define ARM64_ZIP2_8B             0x0e007800   /* ZIP2 Vd.8B, Vn.8B, Vm.8B */
#define ARM64_ZIP2_4H             0x0e407800   /* ZIP2 Vd.4H, Vn.4H, Vm.4H */

/* Vector Compare */
#define ARM64_CMEQ_4H             0x2e608c00   /* CMEQ Vd.4H, Vn.4H, Vm.4H */
#define ARM64_CMGT_4H             0x0e603400   /* CMGT Vd.4H, Vn.4H, Vm.4H */
#define ARM64_CMHI_4H             0x2e603400   /* CMHI Vd.4H, Vn.4H, Vm.4H (unsigned >) */

/* Vector Element Insert/Extract */
#define ARM64_INS_H               0x6e020400   /* INS Vd.H[idx], Wn */
#define ARM64_UMOV_H              0x0e023c00   /* UMOV Wd, Vn.H[idx] */
#define ARM64_DUP_H               0x0e020400   /* DUP Vd.4H, Vn.H[idx] */

/* Vector EXT (Byte Extract) */
#define ARM64_EXT_8B              0x2e000000   /* EXT Vd.8B, Vn.8B, Vm.8B, #idx */
```

---

## 3. Complete Encoder Functions

### 3.1 Loading 64-bit Immediate Addresses

ARM64 can only load 16 bits at a time, so loading a 64-bit pointer requires up to 4 instructions:

```c
static inline void
emit_mov_imm64(int reg, uint64_t val)
{
    /* MOVZ Xreg, #(val & 0xFFFF), LSL #0 */
    emit32(ARM64_MOVZ_X | Rd(reg) | IMM16(val & 0xffff) | HW(0));
    
    if (val & 0xffff0000ull) {
        /* MOVK Xreg, #((val >> 16) & 0xFFFF), LSL #16 */
        emit32(ARM64_MOVK_X | Rd(reg) | IMM16((val >> 16) & 0xffff) | HW(1));
    }
    if (val & 0xffff00000000ull) {
        /* MOVK Xreg, #((val >> 32) & 0xFFFF), LSL #32 */
        emit32(ARM64_MOVK_X | Rd(reg) | IMM16((val >> 32) & 0xffff) | HW(2));
    }
    if (val & 0xffff000000000000ull) {
        /* MOVK Xreg, #((val >> 48) & 0xFFFF), LSL #48 */
        emit32(ARM64_MOVK_X | Rd(reg) | IMM16((val >> 48) & 0xffff) | HW(3));
    }
}
```

### 3.2 ADD/SUB with Immediate

```c
static inline void
emit_add_imm(int dst, int src, uint32_t imm)
{
    if (imm == 0) {
        if (dst != src)
            emit32(ARM64_ORR_REG | Rd(dst) | Rn(REG_XZR) | Rm(src)); /* MOV */
    } else if (imm <= 0xfff) {
        emit32(ARM64_ADD_IMM_W | Rd(dst) | Rn(src) | IMM12(imm) | SHIFT_12(0));
    } else if ((imm & 0xfff) == 0 && (imm >> 12) <= 0xfff) {
        emit32(ARM64_ADD_IMM_W | Rd(dst) | Rn(src) | IMM12(imm >> 12) | SHIFT_12(1));
    } else {
        /* Load immediate to scratch register, then ADD */
        emit_mov_imm64(REG_X16, imm);
        emit32(ARM64_ADD_REG | Rd(dst) | Rn(src) | Rm(REG_W16));
    }
}
```

### 3.3 Load/Store with Offset

```c
/* Offset encoding for different sizes:
 * LDRB: offset is byte-aligned (no scaling)
 * LDRH: offset must be 2-byte aligned, encoded as offset/2
 * LDR W: offset must be 4-byte aligned, encoded as offset/4
 * LDR X: offset must be 8-byte aligned, encoded as offset/8
 */

static inline void
emit_ldr_offset(int dst, int base, int offset)
{
    if (offset >= 0 && offset < 16384 && (offset & 3) == 0) {
        emit32(ARM64_LDR_IMM_W | Rt(dst) | Rn(base) | ((offset >> 2) << 10));
    } else {
        /* Use scratch register for large/unaligned offsets */
        emit_mov_imm64(REG_X16, offset);
        emit32(ARM64_LDR_REG_W | Rt(dst) | Rn(base) | Rm(REG_X16));
    }
}
```

### 3.4 Conditional Branches with Forward Patching

```c
/* Returns position to patch later */
static inline int
emit_branch_cond(int cond)
{
    int patch_pos = block_pos;
    /* Emit placeholder - offset will be patched */
    emit32(ARM64_B_COND | cond);
    return patch_pos;
}

/* Patch a conditional branch to jump to current position */
static inline void
patch_branch(int patch_pos)
{
    int offset = block_pos - patch_pos;
    uint32_t *insn = (uint32_t *)&code_block[patch_pos];
    *insn |= OFFSET19(offset);
}

/* Usage example (depth test): */
/*
    emit32(ARM64_CMP_REG | Rn(depth_reg) | Rm(z_reg));
    int skip_pos = emit_branch_cond(COND_LE);  // Skip if depth <= z
    // ... code to write pixel ...
    patch_branch(skip_pos);  // <- jumps here on fail
*/
```

---

## 4. Prologue and Epilogue

### 4.1 ARM64 Function Prologue

The Voodoo codegen entry point needs to save callee-saved registers and set up the dedicated register assignments:

```c
static inline void
emit_prologue(voodoo_t *voodoo, voodoo_params_t *params, voodoo_state_t *state)
{
    /* Save callee-saved registers to stack:
     * X19-X28 (10 registers) + X29 (FP) + X30 (LR) = 12 * 8 = 96 bytes
     * V8-V15 (8 NEON registers) = 8 * 16 = 128 bytes
     * Total: 224 bytes, rounded to 16-byte alignment = 224 bytes
     */
    
    /* STP X29, X30, [SP, #-224]! */
    emit32(0xa98f7bfd);  /* Pre-indexed store pair with writeback */
    
    /* STP X19, X20, [SP, #16] */
    emit32(0xa9014ff3);
    /* STP X21, X22, [SP, #32] */
    emit32(0xa90257f5);
    /* STP X23, X24, [SP, #48] */
    emit32(0xa9035ff7);
    /* STP X25, X26, [SP, #64] */
    emit32(0xa90467f9);
    /* STP X27, X28, [SP, #80] */
    emit32(0xa9056ffb);
    
    /* Save NEON callee-saved registers V8-V15 */
    /* STP D8, D9, [SP, #96] */
    emit32(0x6d0627e8);
    /* STP D10, D11, [SP, #112] */
    emit32(0x6d072fea);
    /* STP D12, D13, [SP, #128] */
    emit32(0x6d0837ec);
    /* STP D14, D15, [SP, #144] */
    emit32(0x6d093fee);
    
    /* Set up dedicated registers from function arguments
     * On macOS ARM64 (AAPCS64):
     *   X0 = voodoo_state_t* state     -> X19
     *   X1 = voodoo_params_t* params   -> X20
     *   X2 = real_y                    -> X21
     */
    emit32(ARM64_ORR_REG | (1 << 31) | Rd(REG_X19) | Rn(REG_XZR) | Rm(REG_X0)); /* MOV X19, X0 */
    emit32(ARM64_ORR_REG | (1 << 31) | Rd(REG_X20) | Rn(REG_XZR) | Rm(REG_X1)); /* MOV X20, X1 */
    emit32(ARM64_ORR_REG | (1 << 31) | Rd(REG_X21) | Rn(REG_XZR) | Rm(REG_X2)); /* MOV X21, X2 */
    
    /* Load constant table pointers into dedicated registers */
    emit_mov_imm64(REG_X22, (uintptr_t)&logtable);
    emit_mov_imm64(REG_X23, (uintptr_t)&alookup);
    emit_mov_imm64(REG_X24, (uintptr_t)&aminuslookup);
    emit_mov_imm64(REG_X25, (uintptr_t)&bilinear_lookup);
    
    /* Load NEON constant vectors into callee-saved V8-V11 */
    /* Constants: 0x0001000100010001 (01_w), 0x00ff00ff00ff00ff (ff_w), etc. */
    emit_mov_imm64(REG_X16, (uintptr_t)&xmm_01_w);
    emit32(ARM64_LDR_Q | Rt(REG_V8) | Rn(REG_X16));  /* V8 = 01_w constant */
    
    emit_mov_imm64(REG_X16, (uintptr_t)&xmm_ff_w);
    emit32(ARM64_LDR_Q | Rt(REG_V9) | Rn(REG_X16));  /* V9 = ff_w constant */
    
    emit_mov_imm64(REG_X16, (uintptr_t)&xmm_ff_b);
    emit32(ARM64_LDR_Q | Rt(REG_V10) | Rn(REG_X16)); /* V10 = ff_b constant */
    
    emit_mov_imm64(REG_X16, (uintptr_t)&minus_254);
    emit32(ARM64_LDR_Q | Rt(REG_V11) | Rn(REG_X16)); /* V11 = minus_254 constant */
}
```

### 4.2 ARM64 Function Epilogue

```c
static inline void
emit_epilogue(void)
{
    /* Restore NEON callee-saved registers */
    /* LDP D8, D9, [SP, #96] */
    emit32(0x6d4627e8);
    /* LDP D10, D11, [SP, #112] */
    emit32(0x6d472fea);
    /* LDP D12, D13, [SP, #128] */
    emit32(0x6d4837ec);
    /* LDP D14, D15, [SP, #144] */
    emit32(0x6d493fee);
    
    /* Restore general-purpose callee-saved registers */
    /* LDP X19, X20, [SP, #16] */
    emit32(0xa9414ff3);
    /* LDP X21, X22, [SP, #32] */
    emit32(0xa94257f5);
    /* LDP X23, X24, [SP, #48] */
    emit32(0xa9435ff7);
    /* LDP X25, X26, [SP, #64] */
    emit32(0xa94467f9);
    /* LDP X27, X28, [SP, #80] */
    emit32(0xa9456ffb);
    
    /* LDP X29, X30, [SP], #224 */
    emit32(0xa8cf7bfd);  /* Post-indexed load pair with writeback */
    
    /* RET */
    emit32(ARM64_RET | Rn(REG_X30));
}
```

---

## 5. Key Pipeline Operation Patterns

### 5.1 Unpack RGBA Pixel to 16-bit Channels

SSE2 `PUNPCKLBW XMM0, XMM_ZERO` → NEON `UXTL`:

```c
/* Input:  V0.8B contains packed RGBA (only low 4 bytes used)
 * Output: V0.4H contains RGBA as 4x16-bit values (0x00RR, 0x00GG, 0x00BB, 0x00AA)
 */
static inline void
emit_unpack_rgba_to_16bit(int dst_vreg, int src_vreg)
{
    /* UXTL Vd.8H, Vn.8B - zero-extend bytes to halfwords
     * But we only need 4 lanes, so we use the D (64-bit) form
     */
    emit32(ARM64_UXTL | Rd(dst_vreg) | Rn(src_vreg));
}
```

### 5.2 Pack 16-bit Channels Back to RGBA

SSE2 `PACKUSWB` → NEON `SQXTUN`:

```c
/* Input:  V0.4H contains 16-bit RGBA values  
 * Output: V0.8B lower 4 bytes contain packed RGBA
 */
static inline void
emit_pack_16bit_to_rgba(int dst_vreg, int src_vreg)
{
    /* SQXTUN Vd.8B, Vn.8H - saturating extract narrow unsigned */
    emit32(ARM64_SQXTUN_8B | Rd(dst_vreg) | Rn(src_vreg));
}
```

### 5.3 Multiply 16-bit Vectors with 32-bit Result

SSE2 `PMULLW`+`PMULHW` pattern → NEON `SMULL`:

```c
/* Multiply V0.4H * V1.4H, get 32-bit results in V2.4S, then right-shift and narrow
 * This is the common blend operation: result = (a * b) >> 8
 */
static inline void
emit_mul_shr8_4h(int dst_vreg, int src_a, int src_b, int scratch)
{
    /* SMULL Vd.4S, Vn.4H, Vm.4H - signed widening multiply to 32-bit */
    emit32(ARM64_SMULL_4S_4H | Rd(scratch) | Rn(src_a) | Rm(src_b));
    
    /* SSHR Vd.4S, Vn.4S, #8 - arithmetic right shift by 8 */
    emit32(ARM64_SSHR_Q | Rd(scratch) | Rn(scratch) | V_SHIFT_2S(32 - 8));
    
    /* SQXTN Vd.4H, Vn.4S - saturating narrow to 16-bit */
    emit32(ARM64_SQXTN_8B + 0x00400000 | Rd(dst_vreg) | Rn(scratch));  /* SQXTN for 4H->4S variant */
}
```

### 5.4 Broadcast Single Element

SSE2 `PSHUFLW XMM0, XMM1, 0xFF` → NEON `DUP`:

```c
/* Broadcast element N from V1.4H to all lanes of V0.4H */
static inline void
emit_dup_lane_4h(int dst_vreg, int src_vreg, int element)
{
    /* DUP Vd.4H, Vn.H[index] */
    uint32_t index_field = (element & 3) << 19;  /* Element index in bits 20:19 */
    emit32(0x0e020400 | Rd(dst_vreg) | Rn(src_vreg) | index_field);
}
```

### 5.5 Conditional Move (CMOV-like)

SSE2 doesn't have this, but x86 uses `CMOV`. ARM64 uses `CSEL`:

```c
/* If condition is true, dst = src_if_true, else dst = src_if_false */
static inline void
emit_csel(int dst, int src_if_true, int src_if_false, int cond)
{
    emit32(ARM64_CSEL | Rd(dst) | Rn(src_if_true) | Rm(src_if_false) | CSEL_COND(cond));
}
```

---

## 6. macOS JIT Memory Handling

### 6.1 Code Block Allocation

```c
#include <pthread.h>
#include <libkern/OSCacheControl.h>
#include <sys/mman.h>

static void *
alloc_jit_memory(size_t size)
{
    void *ptr = mmap(NULL, size,
                     PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT,
                     -1, 0);
    if (ptr == MAP_FAILED)
        return NULL;
    return ptr;
}

static void
free_jit_memory(void *ptr, size_t size)
{
    munmap(ptr, size);
}
```

### 6.2 Write/Execute Transition

```c
/* Call before writing to code buffer */
static inline void
jit_begin_write(void)
{
    pthread_jit_write_protect_np(0);  /* Enable write, disable execute */
}

/* Call after writing to code buffer, before executing */
static inline void
jit_end_write(void *code_ptr, size_t code_size)
{
    pthread_jit_write_protect_np(1);  /* Disable write, enable execute */
    sys_icache_invalidate(code_ptr, code_size);  /* Flush instruction cache */
}
```

### 6.3 Integrated Usage Pattern

```c
void
voodoo_generate_arm64(voodoo_t *voodoo, voodoo_params_t *params, voodoo_state_t *state)
{
    uint8_t *code_block = voodoo->arm64_data[...].code_block;
    
    jit_begin_write();
    
    block_pos = 0;
    emit_prologue(voodoo, params, state);
    
    /* ... generate pixel pipeline code ... */
    
    emit_epilogue();
    
    jit_end_write(code_block, block_pos);
}
```

---

## 7. Data Structure Definition

```c
typedef struct voodoo_arm64_data_t {
    uint8_t  code_block[BLOCK_SIZE];  /* 8192 bytes */
    int      xdir;
    uint32_t alphaMode;
    uint32_t fbzMode;
    uint32_t fogMode;
    uint32_t fbzColorPath;
    uint32_t textureMode[2];
    uint32_t tLOD[2];
    uint32_t trexInit1;
    int      is_tiled;
} voodoo_arm64_data_t;

/* Dedicated register assignments (matching prologue) */
#define REG_STATE         REG_X19   /* voodoo_state_t* */
#define REG_PARAMS        REG_X20   /* voodoo_params_t* */
#define REG_REAL_Y        REG_X21   /* Current scanline */
#define REG_LOGTABLE      REG_X22   /* logtable pointer */
#define REG_ALOOKUP       REG_X23   /* alookup table */
#define REG_AMINUSLOOKUP  REG_X24   /* aminuslookup table */
#define REG_BILINEAR      REG_X25   /* bilinear_lookup table */

/* NEON constant registers */
#define VREG_01_W         REG_V8    /* 0x0001000100010001 */
#define VREG_FF_W         REG_V9    /* 0x00ff00ff00ff00ff */
#define VREG_FF_B         REG_V10   /* 0x00000000ffffffff */
#define VREG_MINUS254     REG_V11   /* 0xff02ff02ff02ff02 */

/* Scratch registers */
#define REG_SCRATCH       REG_X16   /* Intra-procedure call scratch */
#define REG_SCRATCH2      REG_X17
```

---

## 8. Complete Example: Simple Depth Test

This shows how a depth test would be emitted:

```c
static inline int
emit_depth_test(uint8_t *code_block, int block_pos, int depthop)
{
    /* Load current depth buffer value
     * Assuming X0 = depth buffer pointer, X1 = new Z value
     */
    
    /* LDR W2, [X0] - load existing depth */
    emit32(ARM64_LDR_IMM_W | Rt(REG_W2) | Rn(REG_X0));
    
    /* CMP W1, W2 - compare new Z with existing */
    emit32(ARM64_CMP_REG | Rn(REG_W1) | Rm(REG_W2));
    
    int skip_pos = block_pos;
    
    /* Emit conditional branch based on depth operation */
    switch (depthop) {
        case DEPTHOP_LESS:
            emit32(ARM64_B_COND | COND_GE);  /* Skip if new >= existing */
            break;
        case DEPTHOP_LEQUAL:
            emit32(ARM64_B_COND | COND_GT);  /* Skip if new > existing */
            break;
        case DEPTHOP_GREATER:
            emit32(ARM64_B_COND | COND_LE);  /* Skip if new <= existing */
            break;
        case DEPTHOP_GEQUAL:
            emit32(ARM64_B_COND | COND_LT);  /* Skip if new < existing */
            break;
        case DEPTHOP_EQUAL:
            emit32(ARM64_B_COND | COND_NE);  /* Skip if new != existing */
            break;
        case DEPTHOP_NOTEQUAL:
            emit32(ARM64_B_COND | COND_EQ);  /* Skip if new == existing */
            break;
        case DEPTHOP_ALWAYS:
            /* No branch needed - always pass */
            break;
        case DEPTHOP_NEVER:
            emit32(ARM64_B | OFFSET26(0));  /* Always skip - patched later */
            break;
    }
    
    return skip_pos;  /* Caller will patch this branch target */
}
```

---

## 9. Reference: Existing 86Box ARM64 Code

The existing CPU dynarec in `src/codegen_new/codegen_backend_arm64_ops.c` contains working implementations of all these patterns. Key functions to reference:

| Function | What it does |
|:---------|:-------------|
| `host_arm64_ADD_IMM` | ADD with immediate handling |
| `host_arm64_mov_imm` | Loading arbitrary 32/64-bit immediates |
| `host_arm64_ADD_V4H` | NEON vector add |
| `host_arm64_SMULL_V4S_4H` | Widening multiply |
| `host_arm64_ZIP1_V4H` | Vector interleave |
| `host_arm64_USHR_V4H` | Vector unsigned shift right |
| `codegen_addlong` | The core emission function |

---

## 10. Translation Checklist

When porting each section of the x86-64 codegen:

- [ ] Identify the SSE2 instruction sequence
- [ ] Look up equivalent NEON instructions in Section 2.4
- [ ] Check if existing helper exists in CPU dynarec
- [ ] Write emission code using patterns from Section 3
- [ ] Test with a simple input before integrating

---

This guide provides the concrete building blocks needed to implement `vid_voodoo_codegen_arm64.h`. The patterns here are directly derived from the working ARM64 CPU dynarec in this codebase.
