#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef enum impl_kind {
    IMPL_SCALAR = 0,
    IMPL_NEON
} impl_kind_t;

#ifdef __GNUC__
#    define BENCH_CLOBBER() __asm__ volatile("" ::: "memory")
#else
#    define BENCH_CLOBBER() \
        do {                \
        } while (0)
#endif

static inline const char *
impl_name(impl_kind_t impl)
{
    return (impl == IMPL_NEON) ? "neon" : "scalar";
}

static inline double
ratio(double primary, double baseline)
{
    if (baseline == 0.0)
        return 0.0;
    return primary / baseline;
}

static inline void
bench_common_print_results(impl_kind_t       impl,
                           uint64_t          iters,
                           const char *const names[],
                           const double     *op_ns,
                           size_t            op_count)
{
    printf("impl=%s iters=%llu\n", impl_name(impl), (unsigned long long) iters);
    for (size_t i = 0; i < op_count; ++i) {
        printf("  %-8s: %.3f ns/iter\n", names[i], op_ns[i] / (double) iters);
    }
}

static inline void
bench_common_print_comparison(const double     *primary,
                              const double     *baseline,
                              const char *const names[],
                              size_t            op_count)
{
    printf("comparison (primary/baseline)\n");
    for (size_t i = 0; i < op_count; ++i) {
        printf("  %-8s: %.2f\n", names[i], ratio(primary[i], baseline[i]));
    }
}
