#ifndef _CODEGEN_BACKEND_H_
#define _CODEGEN_BACKEND_H_

#ifndef NEW_DYNAREC_BACKEND
#    ifdef USE_NEW_DYNAREC
#        define NEW_DYNAREC_BACKEND
#    endif
#endif

#if defined __amd64__ || defined _M_X64
#    include "codegen_backend_x86-64.h"
#elif defined __aarch64__ || defined _M_ARM64
#    include "codegen_backend_arm64.h"
#else
#    error New dynamic recompiler not implemented on your platform
#endif

typedef enum codegen_backend_kind {
    BACKEND_UNKNOWN = 0,
    BACKEND_X86_64,
    BACKEND_ARM64_GENERIC,
    BACKEND_ARM64_APPLE
} codegen_backend_kind_t;

extern codegen_backend_kind_t dynarec_backend;

static inline int
codegen_backend_is_apple_arm64(void)
{
#if defined(__APPLE__) && defined(__aarch64__) && defined(NEW_DYNAREC_BACKEND)
    return dynarec_backend == BACKEND_ARM64_APPLE;
#else
    return 0;
#endif
}

void codegen_backend_init(void);
void codegen_backend_prologue(codeblock_t *block);
void codegen_backend_epilogue(codeblock_t *block);

struct ir_data_t;
struct uop_t;

struct ir_data_t *codegen_get_ir_data(void);

typedef int (*uOpFn)(codeblock_t *codeblock, struct uop_t *uop);

extern const uOpFn uop_handlers[];

/*Register will not be preserved across function calls*/
#define HOST_REG_FLAG_VOLATILE (1 << 0)

typedef struct host_reg_def_t {
    int reg;
    int flags;
} host_reg_def_t;

extern host_reg_def_t codegen_host_reg_list[CODEGEN_HOST_REGS];
extern host_reg_def_t codegen_host_fp_reg_list[CODEGEN_HOST_FP_REGS];
#if defined __aarch64__ || defined _M_ARM64
extern host_reg_def_t codegen_host_mmx_reg_list[CODEGEN_HOST_MMX_REGS];
#endif

#endif
