#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench_mmx_ops.h"

static const bench_op_t dyn_ops[] = {
    { "DYN_PADDB",   bench_mmx_paddb   },
    { "DYN_PSUBB",   bench_mmx_psubb   },
    { "DYN_PADDUSB", bench_mmx_paddusb },
    { "DYN_PADDSW",  bench_mmx_paddsw  },
    { "DYN_PMULLW",  bench_mmx_pmullw  },
    { "DYN_PMULH",   bench_mmx_pmulh   },
    { "DYN_PADDW",   bench_mmx_paddw   },
    { "DYN_PADDD",   bench_mmx_paddd   },
    { "DYN_PADDSB",  bench_mmx_paddsb  },
    { "DYN_PADDUSW", bench_mmx_paddusw },
    { "DYN_PSUBW",   bench_mmx_psubw   },
    { "DYN_PSUBD",   bench_mmx_psubd   },
    { "DYN_PSUBSB",  bench_mmx_psubsb  },
    { "DYN_PSUBSW",  bench_mmx_psubsw  },
    { "DYN_PSUBUSB", bench_mmx_psubusb },
    { "DYN_PSUBUSW", bench_mmx_psubusw },
    { "DYN_PMADDWD", bench_mmx_pmaddwd },
    { "DYN_PSRLW",   bench_mmx_psrlw   },
    { "DYN_PSRLD",   bench_mmx_psrld   },
    { "DYN_PSRLQ",   bench_mmx_psrlq   },
    { "DYN_PSRAW",   bench_mmx_psraw   },
    { "DYN_PSRAD",   bench_mmx_psrad   },
    { "DYN_PSLLW",   bench_mmx_psllw   },
    { "DYN_PSLLD",   bench_mmx_pslld   },
    { "DYN_PSLLQ",   bench_mmx_psllq   }
};
#define DYN_OP_COUNT (sizeof(dyn_ops) / sizeof(dyn_ops[0]))

static const char *const dyn_op_names[] = {
    "DYN_PADDB",
    "DYN_PSUBB",
    "DYN_PADDUSB",
    "DYN_PADDSW",
    "DYN_PMULLW",
    "DYN_PMULH",
    "DYN_PADDW",
    "DYN_PADDD",
    "DYN_PADDSB",
    "DYN_PADDUSW",
    "DYN_PSUBW",
    "DYN_PSUBD",
    "DYN_PSUBSB",
    "DYN_PSUBSW",
    "DYN_PSUBUSB",
    "DYN_PSUBUSW",
    "DYN_PMADDWD",
    "DYN_PSRLW",
    "DYN_PSRLD",
    "DYN_PSRLQ",
    "DYN_PSRAW",
    "DYN_PSRAD",
    "DYN_PSLLW",
    "DYN_PSLLD",
    "DYN_PSLLQ"
};

typedef struct dyn_result {
    impl_kind_t impl;
    uint64_t    iters;
    double      op_ns[DYN_OP_COUNT];
} dyn_result_t;

static dyn_result_t
run_dyn_suite(uint64_t iters, impl_kind_t impl)
{
    dyn_result_t result = {
        .impl  = impl,
        .iters = iters
    };

    for (size_t i = 0; i < DYN_OP_COUNT; ++i)
        result.op_ns[i] = dyn_ops[i].fn(iters, impl);

    return result;
}

int
main(int argc, char **argv)
{
    uint64_t    iters = 30000000ull;
    impl_kind_t impl  = IMPL_NEON;

    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--iters=", 8) == 0) {
            iters = strtoull(argv[i] + 8, NULL, 10);
        } else if (strcmp(argv[i], "--impl=neon") == 0) {
            impl = IMPL_NEON;
        } else if (strcmp(argv[i], "--impl=scalar") == 0) {
            impl = IMPL_SCALAR;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [--iters=N] [--impl=neon|scalar]\n", argv[0]);
            return 0;
        }
    }

#if !defined(__aarch64__)
    impl = IMPL_SCALAR;
#endif

    dyn_result_t primary  = run_dyn_suite(iters, impl);
    dyn_result_t baseline = primary;

#if defined(__aarch64__)
    if (impl == IMPL_NEON)
        baseline = run_dyn_suite(iters, IMPL_SCALAR);
#endif

    bench_common_print_results(primary.impl, primary.iters, dyn_op_names, primary.op_ns, DYN_OP_COUNT);
    if (baseline.impl != primary.impl) {
        bench_common_print_results(baseline.impl, baseline.iters, dyn_op_names, baseline.op_ns, DYN_OP_COUNT);
        bench_common_print_comparison(primary.op_ns, baseline.op_ns, dyn_op_names, DYN_OP_COUNT);
    }

    return 0;
}
