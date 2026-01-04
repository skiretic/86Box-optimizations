/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Voodoo Graphics ARM64/NEON dynamic recompiler.
 *
 *          Ported from vid_voodoo_codegen_x86-64.h
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/> (original x86-64)
 *          ARM64 port contributors
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2024 ARM64 port contributors.
 */

#ifndef VIDEO_VOODOO_CODEGEN_ARM64_H
#define VIDEO_VOODOO_CODEGEN_ARM64_H

#ifdef __APPLE__
#    include <libkern/OSCacheControl.h>
#    include <pthread.h>
#endif
#include <sys/mman.h>
#include <arm_neon.h>

/*
 * Block cache configuration - same as x86-64
 */
#define BLOCK_NUM  8
#define BLOCK_MASK (BLOCK_NUM - 1)
#define BLOCK_SIZE 8192

#define LOD_MASK   (LOD_TMIRROR_S | LOD_TMIRROR_T)

/*
 * ARM64 codegen data structure - mirrors x86-64 version
 */
typedef struct voodoo_arm64_data_t {
    uint8_t  code_block[BLOCK_SIZE]; /* Generated machine code */
    int      xdir;                   /* Scanline direction */
    uint32_t alphaMode;              /* Cached state for invalidation */
    uint32_t fbzMode;
    uint32_t fogMode;
    uint32_t fbzColorPath;
    uint32_t textureMode[2];
    uint32_t tLOD[2];
    uint32_t trexInit1;
    int      is_tiled;
} voodoo_arm64_data_t;

static int last_block[4]          = { 0, 0, 0, 0 };
static int next_block_to_write[4] = { 0, 0, 0, 0 };

/*
 * ARM64 uses fixed 32-bit instructions
 */
#define emit32(val)                                 \
    do {                                            \
        *(uint32_t *) &code_block[block_pos] = val; \
        block_pos += 4;                             \
    } while (0)

/*
 * ARM64 Register Definitions (AAPCS64)
 */
#define REG_X0  0
#define REG_X1  1
#define REG_X2  2
#define REG_X3  3
#define REG_X4  4
#define REG_X5  5
#define REG_X6  6
#define REG_X7  7
#define REG_X8  8
#define REG_X9  9
#define REG_X10 10
#define REG_X11 11
#define REG_X12 12
#define REG_X13 13
#define REG_X14 14
#define REG_X15 15
#define REG_X16 16 /* IP0 - intra-procedure scratch */
#define REG_X17 17 /* IP1 - intra-procedure scratch */
#define REG_X18 18 /* Platform register (reserved on macOS) */
#define REG_X19 19 /* Callee-saved - voodoo_state_t* */
#define REG_X20 20 /* Callee-saved - voodoo_params_t* */
#define REG_X21 21 /* Callee-saved - real_y */
#define REG_X22 22 /* Callee-saved - logtable ptr */
#define REG_X23 23 /* Callee-saved - alookup ptr */
#define REG_X24 24 /* Callee-saved - aminuslookup ptr */
#define REG_X25 25 /* Callee-saved - bilinear_lookup ptr */
#define REG_X26 26 /* Callee-saved */
#define REG_X27 27 /* Callee-saved */
#define REG_X28 28 /* Callee-saved */
#define REG_X29 29 /* Frame pointer (FP) */
#define REG_X30 30 /* Link register (LR) */
#define REG_XZR 31 /* Zero register / SP depending on context */
#define REG_SP  31

/* W registers (32-bit views) - same encoding as X registers */
#define REG_W0  0
#define REG_W1  1
#define REG_W16 16
#define REG_W19 19

/* NEON/FP register definitions */
#define REG_V0  0  /* Scratch */
#define REG_V1  1  /* Scratch */
#define REG_V2  2  /* Scratch */
#define REG_V3  3  /* Scratch */
#define REG_V4  4  /* Scratch */
#define REG_V5  5  /* Scratch */
#define REG_V6  6  /* Scratch */
#define REG_V7  7  /* Scratch */
#define REG_V8  8  /* Callee-saved - constant: 0x0001 (01_w) */
#define REG_V9  9  /* Callee-saved - constant: 0x00ff (ff_w) */
#define REG_V10 10 /* Callee-saved - constant: 0x00ffffff (ff_b) */
#define REG_V11 11 /* Callee-saved - constant: minus_254 */
#define REG_V12 12 /* Callee-saved */
#define REG_V13 13 /* Callee-saved */
#define REG_V14 14 /* Callee-saved */
#define REG_V15 15 /* Callee-saved */
#define REG_V16 16 /* Scratch */

/*
 * Register field encoding macros
 */
#define Rd(x)  ((x) & 0x1f)         /* Destination: bits 0-4 */
#define Rn(x)  (((x) & 0x1f) << 5)  /* First source: bits 5-9 */
#define Rm(x)  (((x) & 0x1f) << 16) /* Second source: bits 16-20 */
#define Rt(x)  ((x) & 0x1f)         /* Transfer register */
#define Rt2(x) (((x) & 0x1f) << 10) /* Second transfer register */

/* Immediate field macros */
#define IMM12(imm)   (((imm) & 0xfff) << 10) /* 12-bit immediate */
#define IMM16(imm)   (((imm) & 0xffff) << 5) /* 16-bit immediate */
#define SHIFT_12(sh) (((sh) & 1) << 22)      /* 0=none, 1=LSL#12 */
#define HW(hw)       (((hw) & 3) << 21)      /* Halfword for MOVK */

/* Branch offset encoding */
#define OFFSET19(off) ((((off) >> 2) & 0x7ffff) << 5) /* Conditional branch */
#define OFFSET26(off) (((off) >> 2) & 0x03ffffff)     /* Unconditional branch */

/* Condition codes */
#define COND_EQ 0x0
#define COND_NE 0x1
#define COND_CS 0x2 /* Unsigned >= */
#define COND_CC 0x3 /* Unsigned < */
#define COND_MI 0x4
#define COND_PL 0x5
#define COND_VS 0x6
#define COND_VC 0x7
#define COND_HI 0x8 /* Unsigned > */
#define COND_LS 0x9 /* Unsigned <= */
#define COND_GE 0xa /* Signed >= */
#define COND_LT 0xb /* Signed < */
#define COND_GT 0xc /* Signed > */
#define COND_LE 0xd /* Signed <= */
#define COND_AL 0xe /* Always */

/*
 * ARM64 Instruction Opcode Constants
 */

/* Data Processing - Immediate */
#define ARM64_ADD_IMM_W 0x11000000 /* ADD Wd, Wn, #imm */
#define ARM64_ADD_IMM_X 0x91000000 /* ADD Xd, Xn, #imm */
#define ARM64_SUB_IMM_W 0x51000000 /* SUB Wd, Wn, #imm */
#define ARM64_SUB_IMM_X 0xd1000000 /* SUB Xd, Xn, #imm */
#define ARM64_CMP_IMM_W 0x71000000 /* CMP Wn, #imm (alias: SUBS WZR) */
#define ARM64_CMP_IMM_X 0xf1000000 /* CMP Xn, #imm */

/* Move Wide */
#define ARM64_MOVZ_W 0x52800000 /* MOVZ Wd, #imm16, LSL #hw */
#define ARM64_MOVZ_X 0xd2800000 /* MOVZ Xd, #imm16, LSL #hw */
#define ARM64_MOVK_W 0x72800000 /* MOVK Wd, #imm16, LSL #hw */
#define ARM64_MOVK_X 0xf2800000 /* MOVK Xd, #imm16, LSL #hw */
#define ARM64_MOVN_X 0x92800000 /* MOVN Xd, #imm16, LSL #hw */

/* Data Processing - Register */
#define ARM64_ADD_REG_W 0x0b000000 /* ADD Wd, Wn, Wm */
#define ARM64_ADD_REG_X 0x8b000000 /* ADD Xd, Xn, Xm */
#define ARM64_SUB_REG_W 0x4b000000 /* SUB Wd, Wn, Wm */
#define ARM64_SUB_REG_X 0xcb000000 /* SUB Xd, Xn, Xm */
#define ARM64_ORR_REG_W 0x2a000000 /* ORR Wd, Wn, Wm */
#define ARM64_ORR_REG_X 0xaa000000 /* ORR Xd, Xn, Xm (MOV alias when Wn=WZR) */
#define ARM64_AND_REG_W 0x0a000000 /* AND Wd, Wn, Wm */
#define ARM64_AND_REG_X 0x8a000000 /* AND Xd, Xn, Xm */
#define ARM64_MUL_W     0x1b007c00 /* MUL Wd, Wn, Wm */
#define ARM64_SDIV_W    0x1ac00c00 /* SDIV Wd, Wn, Wm */
#define ARM64_UDIV_W    0x1ac00800 /* UDIV Wd, Wn, Wm */

/* Load/Store - Unsigned Offset */
#define ARM64_LDR_IMM_W 0xb9400000 /* LDR Wt, [Xn, #imm] */
#define ARM64_LDR_IMM_X 0xf9400000 /* LDR Xt, [Xn, #imm] */
#define ARM64_LDRB_IMM  0x39400000 /* LDRB Wt, [Xn, #imm] */
#define ARM64_LDRH_IMM  0x79400000 /* LDRH Wt, [Xn, #imm] */
#define ARM64_STR_IMM_W 0xb9000000 /* STR Wt, [Xn, #imm] */
#define ARM64_STR_IMM_X 0xf9000000 /* STR Xt, [Xn, #imm] */
#define ARM64_STRB_IMM  0x39000000 /* STRB Wt, [Xn, #imm] */
#define ARM64_STRH_IMM  0x79000000 /* STRH Wt, [Xn, #imm] */

/* Load/Store - Register Offset */
#define ARM64_LDR_REG_W 0xb8606800 /* LDR Wt, [Xn, Xm, SXTX] */
#define ARM64_LDR_REG_X 0xf8606800 /* LDR Xt, [Xn, Xm, SXTX] */

/* Load/Store Pair */
#define ARM64_STP_PRE_X  0xa9800000 /* STP Xt1, Xt2, [Xn, #imm]! */
#define ARM64_STP_OFF_X  0xa9000000 /* STP Xt1, Xt2, [Xn, #imm] */
#define ARM64_LDP_POST_X 0xa8c00000 /* LDP Xt1, Xt2, [Xn], #imm */
#define ARM64_LDP_OFF_X  0xa9400000 /* LDP Xt1, Xt2, [Xn, #imm] */
#define ARM64_STP_OFF_D  0x6d000000 /* STP Dt1, Dt2, [Xn, #imm] */
#define ARM64_LDP_OFF_D  0x6d400000 /* LDP Dt1, Dt2, [Xn, #imm] */

/* Branch Instructions */
#define ARM64_B      0x14000000 /* B label */
#define ARM64_BL     0x94000000 /* BL label */
#define ARM64_BR     0xd61f0000 /* BR Xn */
#define ARM64_BLR    0xd63f0000 /* BLR Xn */
#define ARM64_RET    0xd65f03c0 /* RET {X30} */
#define ARM64_B_COND 0x54000000 /* B.cond label */
#define ARM64_CBZ_W  0x34000000 /* CBZ Wt, label */
#define ARM64_CBZ_X  0xb4000000 /* CBZ Xt, label */
#define ARM64_CBNZ_W 0x35000000 /* CBNZ Wt, label */
#define ARM64_CBNZ_X 0xb5000000 /* CBNZ Xt, label */

/* NEON/SIMD Instructions */

/* Vector Data Movement */
#define ARM64_FMOV_S_W 0x1e270000 /* FMOV Sd, Wn */
#define ARM64_FMOV_W_S 0x1e260000 /* FMOV Wd, Sn */
#define ARM64_INS_D_X  0x4e080400 /* INS Vd.D[idx], Xn - base, needs idx encoding */
#define ARM64_UMOV_W_S 0x0e043c00 /* UMOV Wd, Vn.S[idx] - base, needs idx encoding */
#define ARM64_DUP_S    0x0e040400 /* DUP Vd.4S, Vn.S[0] - base, needs arrangement */

/* Vector Loads/Stores */
#define ARM64_LDR_Q 0x3dc00000 /* LDR Qt, [Xn, #imm] (128-bit) */
#define ARM64_LDR_D 0xfd400000 /* LDR Dt, [Xn, #imm] (64-bit) */
#define ARM64_LDR_S 0xbd400000 /* LDR St, [Xn, #imm] (32-bit) */
#define ARM64_STR_Q 0x3d800000 /* STR Qt, [Xn, #imm] */
#define ARM64_STR_D 0xfd000000 /* STR Dt, [Xn, #imm] */
#define ARM64_STR_S 0xbd000000 /* STR St, [Xn, #imm] */

/* Vector Arithmetic - Various Arrangements */
#define ARM64_ADD_8B 0x0e208400 /* ADD Vd.8B, Vn.8B, Vm.8B */
#define ARM64_SUB_8B 0x2e208400 /* SUB Vd.8B, Vn.8B, Vm.8B */
#define ARM64_ADD_4H 0x0e608400 /* ADD Vd.4H, Vn.4H, Vm.4H */
#define ARM64_SUB_4H 0x2e608400 /* SUB Vd.4H, Vn.4H, Vm.4H */
#define ARM64_MUL_4H 0x0e609c00 /* MUL Vd.4H, Vn.4H, Vm.4H */
#define ARM64_ADD_2S 0x0ea08400 /* ADD Vd.2S, Vn.2S, Vm.2S */
#define ARM64_SUB_2S 0x2ea08400 /* SUB Vd.2S, Vn.2S, Vm.2S */

/* Saturating Arithmetic */
#define ARM64_UQADD_4H 0x2e600c00 /* UQADD Vd.4H, Vn.4H, Vm.4H */
#define ARM64_UQSUB_4H 0x2e602c00 /* UQSUB Vd.4H, Vn.4H, Vm.4H */
#define ARM64_SQADD_4H 0x0e600c00 /* SQADD Vd.4H, Vn.4H, Vm.4H */
#define ARM64_SQSUB_4H 0x0e602c00 /* SQSUB Vd.4H, Vn.4H, Vm.4H */

/* Vector Widening Multiply */
#define ARM64_SMULL_4S_4H 0x0e60c000 /* SMULL Vd.4S, Vn.4H, Vm.4H */
#define ARM64_UMULL_4S_4H 0x2e60c000 /* UMULL Vd.4S, Vn.4H, Vm.4H */

/* Vector Logical */
#define ARM64_AND_V 0x0e201c00 /* AND Vd.8B, Vn.8B, Vm.8B */
#define ARM64_ORR_V 0x0ea01c00 /* ORR Vd.8B, Vn.8B, Vm.8B */
#define ARM64_EOR_V 0x2e201c00 /* EOR Vd.8B, Vn.8B, Vm.8B */
#define ARM64_BIC_V 0x0e601c00 /* BIC Vd.8B, Vn.8B, Vm.8B */

/* Vector Shifts - Immediate (shift amount in immh:immb) */
#define ARM64_USHR_4H 0x2f100400 /* USHR Vd.4H, Vn.4H, #shift (base) */
#define ARM64_SSHR_4H 0x0f100400 /* SSHR Vd.4H, Vn.4H, #shift (base) */
#define ARM64_SHL_4H  0x0f105400 /* SHL Vd.4H, Vn.4H, #shift (base) */

/* Vector Extend/Narrow */
#define ARM64_UXTL_8H   0x2f08a400 /* UXTL Vd.8H, Vn.8B (USHLL with shift=0) */
#define ARM64_UXTL_4S   0x2f10a400 /* UXTL Vd.4S, Vn.4H */
#define ARM64_SXTL_8H   0x0f08a400 /* SXTL Vd.8H, Vn.8B */
#define ARM64_XTN_8B    0x0e212800 /* XTN Vd.8B, Vn.8H */
#define ARM64_XTN_4H    0x0e612800 /* XTN Vd.4H, Vn.4S */
#define ARM64_SQXTN_8B  0x0e214800 /* SQXTN Vd.8B, Vn.8H (saturating) */
#define ARM64_SQXTUN_8B 0x2e212800 /* SQXTUN Vd.8B, Vn.8H (signed->unsigned) */
#define ARM64_UQXTN_8B  0x2e214800 /* UQXTN Vd.8B, Vn.8H (unsigned saturating) */

/* Vector Interleave/Permute */
#define ARM64_ZIP1_8B 0x0e003800 /* ZIP1 Vd.8B, Vn.8B, Vm.8B */
#define ARM64_ZIP1_4H 0x0e403800 /* ZIP1 Vd.4H, Vn.4H, Vm.4H */
#define ARM64_ZIP2_8B 0x0e007800 /* ZIP2 Vd.8B, Vn.8B, Vm.8B */
#define ARM64_ZIP2_4H 0x0e407800 /* ZIP2 Vd.4H, Vn.4H, Vm.4H */
#define ARM64_EXT_8B  0x2e000000 /* EXT Vd.8B, Vn.8B, Vm.8B, #idx */

/* Vector Compare */
#define ARM64_CMEQ_4H 0x2e608c00 /* CMEQ Vd.4H, Vn.4H, Vm.4H */
#define ARM64_CMGT_4H 0x0e603400 /* CMGT Vd.4H, Vn.4H, Vm.4H (signed) */
#define ARM64_CMHI_4H 0x2e603400 /* CMHI Vd.4H, Vn.4H, Vm.4H (unsigned) */

/*
 * NEON constant lookup tables (aligned for performance)
 */
static int16_t ALIGNED(16) neon_01_w[8]      = { 1, 1, 1, 1, 1, 1, 1, 1 };
static int16_t ALIGNED(16) neon_ff_w[8]      = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static int32_t ALIGNED(16) neon_ff_b[4]      = { 0x00ffffff, 0, 0, 0 };
static int16_t ALIGNED(16) neon_minus_254[8] = { (int16_t) 0xff02, (int16_t) 0xff02, (int16_t) 0xff02, (int16_t) 0xff02,
                                                 (int16_t) 0xff02, (int16_t) 0xff02, (int16_t) 0xff02, (int16_t) 0xff02 };

static int16_t  ALIGNED(16) alookup[257][8];
static int16_t  ALIGNED(16) aminuslookup[256][8];
static int16_t  ALIGNED(16) bilinear_lookup[256 * 2][8];
static int32_t  ALIGNED(16) xmm_00_ff_w[2][4];
static uint32_t i_00_ff_w[2] = { 0, 0xff };

/*
 * Encoder helper functions
 */

/* Load 64-bit immediate using MOVZ + MOVK sequence */
static inline int
emit_mov_imm64(uint8_t *code_block, int block_pos, int reg, uint64_t val)
{
    /* MOVZ Xd, #imm16 (bits 0-15) */
    emit32(ARM64_MOVZ_X | Rd(reg) | IMM16(val & 0xffff) | HW(0));

    if (val & 0xffff0000ull) {
        emit32(ARM64_MOVK_X | Rd(reg) | IMM16((val >> 16) & 0xffff) | HW(1));
    }
    if (val & 0xffff00000000ull) {
        emit32(ARM64_MOVK_X | Rd(reg) | IMM16((val >> 32) & 0xffff) | HW(2));
    }
    if (val & 0xffff000000000000ull) {
        emit32(ARM64_MOVK_X | Rd(reg) | IMM16((val >> 48) & 0xffff) | HW(3));
    }

    return block_pos;
}

/* Emit conditional branch with forward patching support */
static inline int
emit_branch_cond(uint8_t *code_block, int block_pos, int cond)
{
    int patch_pos = block_pos;
    emit32(ARM64_B_COND | cond);
    return patch_pos;
}

/* Patch conditional branch with actual offset */
static inline void
patch_branch(uint8_t *code_block, int patch_pos, int target_pos)
{
    int       offset = target_pos - patch_pos;
    uint32_t *insn   = (uint32_t *) &code_block[patch_pos];
    *insn |= OFFSET19(offset);
}

/* Emit unconditional branch with forward patching support */
static inline int
emit_branch(uint8_t *code_block, int block_pos)
{
    int patch_pos = block_pos;
    emit32(ARM64_B);
    return patch_pos;
}

/* Patch unconditional branch */
static inline void
patch_branch_uncond(uint8_t *code_block, int patch_pos, int target_pos)
{
    int       offset = target_pos - patch_pos;
    uint32_t *insn   = (uint32_t *) &code_block[patch_pos];
    *insn |= OFFSET26(offset);
}

/*
 * W^X compliance for macOS
 *
 * Apple Silicon requires MAP_JIT and toggling write protection
 * before writing vs executing JIT code.
 */
static inline void
jit_enable_write(void)
{
#ifdef __APPLE__
    pthread_jit_write_protect_np(0);
#endif
}

static inline void
jit_enable_execute(uint8_t *code, size_t size)
{
#ifdef __APPLE__
    pthread_jit_write_protect_np(1);
    sys_icache_invalidate(code, size);
#else
    __builtin___clear_cache((char *) code, (char *) code + size);
#endif
}

/*
 * Generate ARM64 code for a scanline
 * This is the main code generation function - STUB for initial infrastructure
 */
static inline void
voodoo_generate_arm64(uint8_t *code_block, voodoo_t *voodoo, voodoo_params_t *params,
                      voodoo_state_t *state, int depthop)
{
    int block_pos = 0;

    /*
     * Prologue: Save callee-saved registers
     *
     * Stack frame layout (224 bytes):
     *   [SP+0]:   X29 (FP), X30 (LR)    - saved with pre-index
     *   [SP+16]:  X19, X20
     *   [SP+32]:  X21, X22
     *   [SP+48]:  X23, X24
     *   [SP+64]:  X25, X26
     *   [SP+80]:  X27, X28
     *   [SP+96]:  D8, D9
     *   [SP+112]: D10, D11
     *   [SP+128]: D12, D13
     *   [SP+144]: D14, D15
     *   [SP+160]: Local variables available
     */

    /* STP X29, X30, [SP, #-224]! */
    emit32(0xa98f7bfd);

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

    /* STP D8, D9, [SP, #96] */
    emit32(0x6d0627e8);

    /* STP D10, D11, [SP, #112] */
    emit32(0x6d072fea);

    /* STP D12, D13, [SP, #128] */
    emit32(0x6d0837ec);

    /* STP D14, D15, [SP, #144] */
    emit32(0x6d093fee);

    /*
     * Move function arguments to callee-saved registers (AAPCS64):
     *   X0 -> X19 (voodoo_state_t* state)
     *   X1 -> X20 (voodoo_params_t* params)
     *   X2 -> X21 (int real_y)
     */

    /* MOV X19, X0 (ORR X19, XZR, X0) */
    emit32(ARM64_ORR_REG_X | Rd(REG_X19) | Rn(REG_XZR) | Rm(REG_X0));

    /* MOV X20, X1 */
    emit32(ARM64_ORR_REG_X | Rd(REG_X20) | Rn(REG_XZR) | Rm(REG_X1));

    /* MOV X21, X2 */
    emit32(ARM64_ORR_REG_X | Rd(REG_X21) | Rn(REG_XZR) | Rm(REG_X2));

    /*
     * Load lookup table pointers into callee-saved registers
     */
    block_pos = emit_mov_imm64(code_block, block_pos, REG_X22, (uintptr_t) &logtable);
    block_pos = emit_mov_imm64(code_block, block_pos, REG_X23, (uintptr_t) &alookup);
    block_pos = emit_mov_imm64(code_block, block_pos, REG_X24, (uintptr_t) &aminuslookup);
    block_pos = emit_mov_imm64(code_block, block_pos, REG_X25, (uintptr_t) &bilinear_lookup);

    /*
     * Load NEON constants into V8-V11
     */

    /* Load neon_01_w into V8 */
    block_pos = emit_mov_imm64(code_block, block_pos, REG_X16, (uintptr_t) &neon_01_w);
    emit32(ARM64_LDR_Q | Rt(REG_V8) | Rn(REG_X16));

    /* Load neon_ff_w into V9 */
    block_pos = emit_mov_imm64(code_block, block_pos, REG_X16, (uintptr_t) &neon_ff_w);
    emit32(ARM64_LDR_Q | Rt(REG_V9) | Rn(REG_X16));

    /* Load neon_ff_b into V10 */
    block_pos = emit_mov_imm64(code_block, block_pos, REG_X16, (uintptr_t) &neon_ff_b);
    emit32(ARM64_LDR_Q | Rt(REG_V10) | Rn(REG_X16));

    /* Load neon_minus_254 into V11 */
    block_pos = emit_mov_imm64(code_block, block_pos, REG_X16, (uintptr_t) &neon_minus_254);
    emit32(ARM64_LDR_Q | Rt(REG_V11) | Rn(REG_X16));

    /*
     * TODO: Main scanline processing loop
     * For now, this is a minimal stub that returns immediately
     */

    /*
     * Epilogue: Restore callee-saved registers and return
     */

    /* LDP D14, D15, [SP, #144] */
    emit32(0x6d493fee);

    /* LDP D12, D13, [SP, #128] */
    emit32(0x6d4837ec);

    /* LDP D10, D11, [SP, #112] */
    emit32(0x6d472fea);

    /* LDP D8, D9, [SP, #96] */
    emit32(0x6d4627e8);

    /* LDP X27, X28, [SP, #80] */
    emit32(0xa9456ffb);

    /* LDP X25, X26, [SP, #64] */
    emit32(0xa94467f9);

    /* LDP X23, X24, [SP, #48] */
    emit32(0xa9435ff7);

    /* LDP X21, X22, [SP, #32] */
    emit32(0xa94257f5);

    /* LDP X19, X20, [SP, #16] */
    emit32(0xa9414ff3);

    /* LDP X29, X30, [SP], #224 */
    emit32(0xa8cf7bfd);

    /* RET */
    emit32(ARM64_RET);
}

/*
 * Recompilation counter (for debugging)
 */
int voodoo_recomp = 0;

/*
 * Look up or generate code block for current render state
 */
static inline void *
voodoo_get_block(voodoo_t *voodoo, voodoo_params_t *params, voodoo_state_t *state, int odd_even)
{
    int                  b                 = last_block[odd_even];
    voodoo_arm64_data_t *voodoo_arm64_data = voodoo->codegen_data;
    voodoo_arm64_data_t *data;
    int                  depth_op = (params->fbzMode >> 5) & 7;

    /* Search cache for existing block */
    for (uint8_t c = 0; c < 8; c++) {
        data = &voodoo_arm64_data[odd_even + c * 4];

        if (state->xdir == data->xdir && params->alphaMode == data->alphaMode && params->fbzMode == data->fbzMode && params->fogMode == data->fogMode && params->fbzColorPath == data->fbzColorPath && (voodoo->trexInit1[0] & (1 << 18)) == data->trexInit1 && params->textureMode[0] == data->textureMode[0] && params->textureMode[1] == data->textureMode[1] && (params->tLOD[0] & LOD_MASK) == data->tLOD[0] && (params->tLOD[1] & LOD_MASK) == data->tLOD[1] && ((params->col_tiled || params->aux_tiled) ? 1 : 0) == data->is_tiled) {
            last_block[odd_even] = b;
            return data->code_block;
        }

        b = (b + 1) & 7;
    }

    /* Cache miss - generate new block */
    voodoo_recomp++;
    data = &voodoo_arm64_data[odd_even + next_block_to_write[odd_even] * 4];

    /* Enable write to JIT memory */
    jit_enable_write();

    /* Generate code */
    voodoo_generate_arm64(data->code_block, voodoo, params, state, depth_op);

    /* Cache state for invalidation check */
    data->xdir           = state->xdir;
    data->alphaMode      = params->alphaMode;
    data->fbzMode        = params->fbzMode;
    data->fogMode        = params->fogMode;
    data->fbzColorPath   = params->fbzColorPath;
    data->trexInit1      = voodoo->trexInit1[0] & (1 << 18);
    data->textureMode[0] = params->textureMode[0];
    data->textureMode[1] = params->textureMode[1];
    data->tLOD[0]        = params->tLOD[0] & LOD_MASK;
    data->tLOD[1]        = params->tLOD[1] & LOD_MASK;
    data->is_tiled       = (params->col_tiled || params->aux_tiled) ? 1 : 0;

    /* Enable execute and flush instruction cache */
    jit_enable_execute(data->code_block, BLOCK_SIZE);

    next_block_to_write[odd_even] = (next_block_to_write[odd_even] + 1) & 7;

    return data->code_block;
}

/*
 * Initialize ARM64 codegen
 * Allocate JIT memory with MAP_JIT for macOS W^X compliance
 */
void
voodoo_codegen_init(voodoo_t *voodoo)
{
    /* Allocate executable memory with MAP_JIT for macOS */
    int prot  = PROT_READ | PROT_WRITE | PROT_EXEC;
    int flags = MAP_ANON | MAP_PRIVATE;

#ifdef __APPLE__
    flags |= MAP_JIT;
#endif

    voodoo->codegen_data = mmap(NULL, sizeof(voodoo_arm64_data_t) * BLOCK_NUM * 4,
                                prot, flags, -1, 0);

    if (voodoo->codegen_data == MAP_FAILED) {
        voodoo->codegen_data   = NULL;
        voodoo->use_recompiler = 0;
        return;
    }

    /* Initialize lookup tables */
    for (uint16_t c = 0; c < 256; c++) {
        int d[4];
        int _ds = c & 0xf;
        int dt  = c >> 4;

        /* Alpha lookup: each element is c repeated */
        for (int i = 0; i < 8; i++) {
            alookup[c][i]      = c;
            aminuslookup[c][i] = 255 - c;
        }

        /* Bilinear interpolation weights */
        d[0] = (16 - _ds) * (16 - dt);
        d[1] = _ds * (16 - dt);
        d[2] = (16 - _ds) * dt;
        d[3] = _ds * dt;

        /* Pack weights for SIMD processing */
        bilinear_lookup[c * 2][0] = d[0];
        bilinear_lookup[c * 2][1] = d[0];
        bilinear_lookup[c * 2][2] = d[1];
        bilinear_lookup[c * 2][3] = d[1];
        bilinear_lookup[c * 2][4] = d[0];
        bilinear_lookup[c * 2][5] = d[0];
        bilinear_lookup[c * 2][6] = d[1];
        bilinear_lookup[c * 2][7] = d[1];

        bilinear_lookup[c * 2 + 1][0] = d[2];
        bilinear_lookup[c * 2 + 1][1] = d[2];
        bilinear_lookup[c * 2 + 1][2] = d[3];
        bilinear_lookup[c * 2 + 1][3] = d[3];
        bilinear_lookup[c * 2 + 1][4] = d[2];
        bilinear_lookup[c * 2 + 1][5] = d[2];
        bilinear_lookup[c * 2 + 1][6] = d[3];
        bilinear_lookup[c * 2 + 1][7] = d[3];
    }

    /* Alpha lookup[256] for clamping */
    for (int i = 0; i < 8; i++) {
        alookup[256][i] = 256;
    }

    /* Zero and 0xff constant arrays */
    xmm_00_ff_w[0][0] = 0;
    xmm_00_ff_w[0][1] = 0;
    xmm_00_ff_w[0][2] = 0;
    xmm_00_ff_w[0][3] = 0;
    xmm_00_ff_w[1][0] = 0x00ff00ff;
    xmm_00_ff_w[1][1] = 0x00ff00ff;
    xmm_00_ff_w[1][2] = 0;
    xmm_00_ff_w[1][3] = 0;
}

/*
 * Close ARM64 codegen and free resources
 */
void
voodoo_codegen_close(voodoo_t *voodoo)
{
    if (voodoo->codegen_data) {
        munmap(voodoo->codegen_data, sizeof(voodoo_arm64_data_t) * BLOCK_NUM * 4);
        voodoo->codegen_data = NULL;
    }
}

#endif /* VIDEO_VOODOO_CODEGEN_ARM64_H */
