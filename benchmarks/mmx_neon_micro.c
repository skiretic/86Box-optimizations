#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench_mmx_ops.h"

static const bench_op_t bench_ops[] = {
    { "PADDB",    bench_mmx_paddb    },
    { "PSUBB",    bench_mmx_psubb    },
    { "PADDUSB",  bench_mmx_paddusb  },
    { "PADDSW",   bench_mmx_paddsw   },
    { "PMULLW",   bench_mmx_pmullw   },
    { "PMULH",    bench_mmx_pmulh    },
    { "PACKSSWB", bench_mmx_packsswb },
    { "PACKUSWB", bench_mmx_packuswb },
    { "PSHUFB",   bench_mmx_pshufb   }
};
#define BENCH_OP_COUNT (sizeof(bench_ops) / sizeof(bench_ops[0]))

static const char *const bench_names[] = {
    "PADDB",
    "PSUBB",
    "PADDUSB",
    "PADDSW",
    "PMULLW",
    "PMULH",
    "PACKSSWB",
    "PACKUSWB",
    "PSHUFB"
};

typedef struct bench_result {
    impl_kind_t impl;
    uint64_t    iters;
    double      op_ns[BENCH_OP_COUNT];
} bench_result_t;

static bench_result_t
run_suite(uint64_t iters, impl_kind_t impl)
{
    bench_result_t result = {
        .impl  = impl,
        .iters = iters
    };

    for (size_t i = 0; i < BENCH_OP_COUNT; ++i)
        result.op_ns[i] = bench_ops[i].fn(iters, impl);

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

    bench_result_t primary  = run_suite(iters, impl);
    bench_result_t baseline = primary;

#if defined(__aarch64__)
    if (impl == IMPL_NEON)
        baseline = run_suite(iters, IMPL_SCALAR);
#endif

    bench_common_print_results(primary.impl, primary.iters, bench_names, primary.op_ns, BENCH_OP_COUNT);
    if (baseline.impl != primary.impl) {
        bench_common_print_results(baseline.impl, baseline.iters, bench_names, baseline.op_ns, BENCH_OP_COUNT);
        bench_common_print_comparison(primary.op_ns, baseline.op_ns, bench_names, BENCH_OP_COUNT);
    }

    // Print cache metrics
    printf("\nCache Metrics: Not available (microbenchmarks don't run dynarec code)\n");

    return 0;
}
