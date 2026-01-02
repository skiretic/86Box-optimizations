#pragma once

#include <stdint.h>
#include <time.h>

#if defined(__aarch64__)
#    include <arm_neon.h>
#endif

#include "bench_common.h"

typedef double (*bench_fn_t)(uint64_t iters, impl_kind_t impl);

typedef struct bench_op {
    const char *name;
    bench_fn_t  fn;
} bench_op_t;

static inline uint64_t
bench_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
}

static inline double
bench_mmx_paddb(uint64_t iters, impl_kind_t impl)
{
    uint8_t  a[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t  b[8] = { 8, 7, 6, 5, 4, 3, 2, 1 };
    uint64_t sink = 0;

    uint64_t start = bench_now_ns();
#if defined(__aarch64__)
    if (impl == IMPL_NEON) {
        uint8x8_t va = vld1_u8(a);
        uint8x8_t vb = vld1_u8(b);
        for (uint64_t i = 0; i < iters; ++i) {
            uint8x8_t vc = vadd_u8(va, vb);
            vst1_u8(a, vc);
            va = vc;
            BENCH_CLOBBER();
        }
        sink += a[0];
        return (double) (bench_now_ns() - start + sink);
    }
#endif
    (void) impl;
    for (uint64_t i = 0; i < iters; ++i) {
        for (int j = 0; j < 8; ++j) {
            a[j] = (uint8_t) (a[j] + b[j]);
        }
        BENCH_CLOBBER();
    }
    sink += a[0];
    return (double) (bench_now_ns() - start + sink);
}

static inline double
bench_mmx_psubb(uint64_t iters, impl_kind_t impl)
{
    int8_t   a[8] = { 10, 20, 30, 40, 50, 60, 70, 80 };
    int8_t   b[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint64_t sink = 0;

    uint64_t start = bench_now_ns();
#if defined(__aarch64__)
    if (impl == IMPL_NEON) {
        int8x8_t va = vld1_s8(a);
        int8x8_t vb = vld1_s8(b);
        for (uint64_t i = 0; i < iters; ++i) {
            int8x8_t vc = vsub_s8(va, vb);
            vst1_s8(a, vc);
            va = vc;
            BENCH_CLOBBER();
        }
        sink += (uint64_t) a[0];
        return (double) (bench_now_ns() - start + sink);
    }
#endif
    (void) impl;
    for (uint64_t i = 0; i < iters; ++i) {
        for (int j = 0; j < 8; ++j) {
            a[j] = (int8_t) (a[j] - b[j]);
        }
        BENCH_CLOBBER();
    }
    sink += (uint64_t) a[0];
    return (double) (bench_now_ns() - start + sink);
}

static inline double
bench_mmx_paddusb(uint64_t iters, impl_kind_t impl)
{
    uint8_t  a[8] = { 200, 150, 100, 50, 25, 12, 6, 3 };
    uint8_t  b[8] = { 100, 100, 100, 100, 100, 100, 100, 100 };
    uint64_t sink = 0;

    uint64_t start = bench_now_ns();
#if defined(__aarch64__)
    if (impl == IMPL_NEON) {
        uint8x8_t va = vld1_u8(a);
        uint8x8_t vb = vld1_u8(b);
        for (uint64_t i = 0; i < iters; ++i) {
            uint8x8_t vc = vqadd_u8(va, vb);
            vst1_u8(a, vc);
            va = vc;
            BENCH_CLOBBER();
        }
        sink += a[0];
        return (double) (bench_now_ns() - start + sink);
    }
#endif
    (void) impl;
    for (uint64_t i = 0; i < iters; ++i) {
        for (int j = 0; j < 8; ++j) {
            uint16_t tmp = (uint16_t) a[j] + (uint16_t) b[j];
            if (tmp > 255)
                tmp = 255;
            a[j] = (uint8_t) tmp;
        }
        BENCH_CLOBBER();
    }
    sink += a[0];
    return (double) (bench_now_ns() - start + sink);
}

static inline double
bench_mmx_paddsw(uint64_t iters, impl_kind_t impl)
{
    int16_t  a[4] = { 30000, -30000, 20000, -20000 };
    int16_t  b[4] = { 20000, -20000, 20000, -20000 };
    uint64_t sink = 0;

    uint64_t start = bench_now_ns();
#if defined(__aarch64__)
    if (impl == IMPL_NEON) {
        int16x4_t va = vld1_s16(a);
        int16x4_t vb = vld1_s16(b);
        for (uint64_t i = 0; i < iters; ++i) {
            int16x4_t vc = vqadd_s16(va, vb);
            vst1_s16(a, vc);
            va = vc;
            BENCH_CLOBBER();
        }
        sink += (uint64_t) a[0];
        return (double) (bench_now_ns() - start + sink);
    }
#endif
    (void) impl;
    for (uint64_t i = 0; i < iters; ++i) {
        for (int j = 0; j < 4; ++j) {
            int32_t tmp = (int32_t) a[j] + (int32_t) b[j];
            if (tmp > 32767)
                tmp = 32767;
            else if (tmp < -32768)
                tmp = -32768;
            a[j] = (int16_t) tmp;
        }
        BENCH_CLOBBER();
    }
    sink += (uint64_t) a[0];
    return (double) (bench_now_ns() - start + sink);
}

static inline double
bench_mmx_pmullw(uint64_t iters, impl_kind_t impl)
{
    int16_t  a[4] = { 1000, -2000, 3000, -4000 };
    int16_t  b[4] = { 10, -20, 30, -40 };
    uint64_t sink = 0;

    uint64_t start = bench_now_ns();
#if defined(__aarch64__)
    if (impl == IMPL_NEON) {
        int16x4_t va = vld1_s16(a);
        int16x4_t vb = vld1_s16(b);
        for (uint64_t i = 0; i < iters; ++i) {
            int16x4_t vc = vmul_s16(va, vb);
            vst1_s16(a, vc);
            va = vc;
            BENCH_CLOBBER();
        }
        sink += (uint64_t) a[0];
        return (double) (bench_now_ns() - start + sink);
    }
#endif
    (void) impl;
    for (uint64_t i = 0; i < iters; ++i) {
        for (int j = 0; j < 4; ++j) {
            a[j] = (int16_t) ((int32_t) a[j] * (int32_t) b[j]);
        }
        BENCH_CLOBBER();
    }
    sink += (uint64_t) a[0];
    return (double) (bench_now_ns() - start + sink);
}

static inline double
bench_mmx_pmulh(uint64_t iters, impl_kind_t impl)
{
    int16_t  a[4] = { 123, -321, 456, -654 };
    int16_t  b[4] = { 7, -8, 9, -10 };
    uint64_t sink = 0;

    uint64_t start = bench_now_ns();
#if defined(__aarch64__)
    if (impl == IMPL_NEON) {
        int16x4_t va = vld1_s16(a);
        int16x4_t vb = vld1_s16(b);
        for (uint64_t i = 0; i < iters; ++i) {
            int32x4_t vc = vmull_s16(va, vb);
            int16x4_t vd = vshrn_n_s32(vc, 16);
            vst1_s16(a, vd);
            va = vd;
            BENCH_CLOBBER();
        }
        sink += (uint64_t) a[0];
        return (double) (bench_now_ns() - start + sink);
    }
#endif
    (void) impl;
    for (uint64_t i = 0; i < iters; ++i) {
        for (int j = 0; j < 4; ++j) {
            int32_t tmp = (int32_t) a[j] * (int32_t) b[j];
            a[j]        = (int16_t) (tmp >> 16);
        }
        BENCH_CLOBBER();
    }
    sink += (uint64_t) a[0];
    return (double) (bench_now_ns() - start + sink);
}

static inline double
bench_mmx_paddw(uint64_t iters, impl_kind_t impl)
{
    uint16_t a[4] = { 1, 2, 3, 4 };
    uint16_t b[4] = { 4, 3, 2, 1 };
    uint64_t sink = 0;

    uint64_t start = bench_now_ns();
#if defined(__aarch64__)
    if (impl == IMPL_NEON) {
        uint16x4_t va = vld1_u16(a);
        uint16x4_t vb = vld1_u16(b);
        for (uint64_t i = 0; i < iters; ++i) {
            uint16x4_t vc = vadd_u16(va, vb);
            vst1_u16(a, vc);
            va = vc;
            BENCH_CLOBBER();
        }
        sink += a[0];
        return (double) (bench_now_ns() - start + sink);
    }
#endif
    (void) impl;
    for (uint64_t i = 0; i < iters; ++i) {
        for (int j = 0; j < 4; ++j) {
            a[j] = (uint16_t) (a[j] + b[j]);
        }
        BENCH_CLOBBER();
    }
    sink += a[0];
    return (double) (bench_now_ns() - start + sink);
}

static inline double
bench_mmx_paddd(uint64_t iters, impl_kind_t impl)
{
    uint32_t a[2] = { 1, 2 };
    uint32_t b[2] = { 2, 1 };
    uint64_t sink = 0;

    uint64_t start = bench_now_ns();
#if defined(__aarch64__)
    if (impl == IMPL_NEON) {
        uint32x2_t va = vld1_u32(a);
        uint32x2_t vb = vld1_u32(b);
        for (uint64_t i = 0; i < iters; ++i) {
            uint32x2_t vc = vadd_u32(va, vb);
            vst1_u32(a, vc);
            va = vc;
            BENCH_CLOBBER();
        }
        sink += a[0];
        return (double) (bench_now_ns() - start + sink);
    }
#endif
    (void) impl;
    for (uint64_t i = 0; i < iters; ++i) {
        for (int j = 0; j < 2; ++j) {
            a[j] = (uint32_t) (a[j] + b[j]);
        }
        BENCH_CLOBBER();
    }
    sink += a[0];
    return (double) (bench_now_ns() - start + sink);
}

static inline double
bench_mmx_paddsb(uint64_t iters, impl_kind_t impl)
{
    int8_t   a[8] = { 100, -100, 50, -50, 25, -25, 10, -10 };
    int8_t   b[8] = { 10, -10, 25, -25, 50, -50, 100, -100 };
    uint64_t sink = 0;

    uint64_t start = bench_now_ns();
#if defined(__aarch64__)
    if (impl == IMPL_NEON) {
        int8x8_t va = vld1_s8(a);
        int8x8_t vb = vld1_s8(b);
        for (uint64_t i = 0; i < iters; ++i) {
            int8x8_t vc = vqadd_s8(va, vb);
            vst1_s8(a, vc);
            va = vc;
            BENCH_CLOBBER();
        }
        sink += (uint64_t) a[0];
        return (double) (bench_now_ns() - start + sink);
    }
#endif
    (void) impl;
    for (uint64_t i = 0; i < iters; ++i) {
        for (int j = 0; j < 8; ++j) {
            int16_t tmp = (int16_t) a[j] + (int16_t) b[j];
            if (tmp > 127)
                tmp = 127;
            else if (tmp < -128)
                tmp = -128;
            a[j] = (int8_t) tmp;
        }
        BENCH_CLOBBER();
    }
    sink += (uint64_t) a[0];
    return (double) (bench_now_ns() - start + sink);
}

static inline double
bench_mmx_paddusw(uint64_t iters, impl_kind_t impl)
{
    uint16_t a[4] = { 60000, 50000, 40000, 30000 };
    uint16_t b[4] = { 10000, 20000, 30000, 40000 };
    uint64_t sink = 0;

    uint64_t start = bench_now_ns();
#if defined(__aarch64__)
    if (impl == IMPL_NEON) {
        uint16x4_t va = vld1_u16(a);
        uint16x4_t vb = vld1_u16(b);
        for (uint64_t i = 0; i < iters; ++i) {
            uint16x4_t vc = vqadd_u16(va, vb);
            vst1_u16(a, vc);
            va = vc;
            BENCH_CLOBBER();
        }
        sink += a[0];
        return (double) (bench_now_ns() - start + sink);
    }
#endif
    (void) impl;
    for (uint64_t i = 0; i < iters; ++i) {
        for (int j = 0; j < 4; ++j) {
            uint32_t tmp = (uint32_t) a[j] + (uint32_t) b[j];
            if (tmp > 65535)
                tmp = 65535;
            a[j] = (uint16_t) tmp;
        }
        BENCH_CLOBBER();
    }
    sink += a[0];
    return (double) (bench_now_ns() - start + sink);
}

static inline double
bench_mmx_psubw(uint64_t iters, impl_kind_t impl)
{
    uint16_t a[4] = { 10, 20, 30, 40 };
    uint16_t b[4] = { 1, 2, 3, 4 };
    uint64_t sink = 0;

    uint64_t start = bench_now_ns();
#if defined(__aarch64__)
    if (impl == IMPL_NEON) {
        uint16x4_t va = vld1_u16(a);
        uint16x4_t vb = vld1_u16(b);
        for (uint64_t i = 0; i < iters; ++i) {
            uint16x4_t vc = vsub_u16(va, vb);
            vst1_u16(a, vc);
            va = vc;
            BENCH_CLOBBER();
        }
        sink += a[0];
        return (double) (bench_now_ns() - start + sink);
    }
#endif
    (void) impl;
    for (uint64_t i = 0; i < iters; ++i) {
        for (int j = 0; j < 4; ++j) {
            a[j] = (uint16_t) (a[j] - b[j]);
        }
        BENCH_CLOBBER();
    }
    sink += a[0];
    return (double) (bench_now_ns() - start + sink);
}

static inline double
bench_mmx_psubd(uint64_t iters, impl_kind_t impl)
{
    uint32_t a[2] = { 100, 200 };
    uint32_t b[2] = { 10, 20 };
    uint64_t sink = 0;

    uint64_t start = bench_now_ns();
#if defined(__aarch64__)
    if (impl == IMPL_NEON) {
        uint32x2_t va = vld1_u32(a);
        uint32x2_t vb = vld1_u32(b);
        for (uint64_t i = 0; i < iters; ++i) {
            uint32x2_t vc = vsub_u32(va, vb);
            vst1_u32(a, vc);
            va = vc;
            BENCH_CLOBBER();
        }
        sink += a[0];
        return (double) (bench_now_ns() - start + sink);
    }
#endif
    (void) impl;
    for (uint64_t i = 0; i < iters; ++i) {
        for (int j = 0; j < 2; ++j) {
            a[j] = (uint32_t) (a[j] - b[j]);
        }
        BENCH_CLOBBER();
    }
    sink += a[0];
    return (double) (bench_now_ns() - start + sink);
}

static inline double
bench_mmx_psubsb(uint64_t iters, impl_kind_t impl)
{
    int8_t   a[8] = { 100, -100, 50, -50, 25, -25, 10, -10 };
    int8_t   b[8] = { 10, -10, 5, -5, 2, -2, 1, -1 };
    uint64_t sink = 0;

    uint64_t start = bench_now_ns();
#if defined(__aarch64__)
    if (impl == IMPL_NEON) {
        int8x8_t va = vld1_s8(a);
        int8x8_t vb = vld1_s8(b);
        for (uint64_t i = 0; i < iters; ++i) {
            int8x8_t vc = vqsub_s8(va, vb);
            vst1_s8(a, vc);
            va = vc;
            BENCH_CLOBBER();
        }
        sink += (uint64_t) a[0];
        return (double) (bench_now_ns() - start + sink);
    }
#endif
    (void) impl;
    for (uint64_t i = 0; i < iters; ++i) {
        for (int j = 0; j < 8; ++j) {
            int16_t tmp = (int16_t) a[j] - (int16_t) b[j];
            if (tmp > 127)
                tmp = 127;
            else if (tmp < -128)
                tmp = -128;
            a[j] = (int8_t) tmp;
        }
        BENCH_CLOBBER();
    }
    sink += (uint64_t) a[0];
    return (double) (bench_now_ns() - start + sink);
}

static inline double
bench_mmx_psubsw(uint64_t iters, impl_kind_t impl)
{
    int16_t  a[4] = { 30000, -30000, 20000, -20000 };
    int16_t  b[4] = { 10000, -10000, 5000, -5000 };
    uint64_t sink = 0;

    uint64_t start = bench_now_ns();
#if defined(__aarch64__)
    if (impl == IMPL_NEON) {
        int16x4_t va = vld1_s16(a);
        int16x4_t vb = vld1_s16(b);
        for (uint64_t i = 0; i < iters; ++i) {
            int16x4_t vc = vqsub_s16(va, vb);
            vst1_s16(a, vc);
            va = vc;
            BENCH_CLOBBER();
        }
        sink += (uint64_t) a[0];
        return (double) (bench_now_ns() - start + sink);
    }
#endif
    (void) impl;
    for (uint64_t i = 0; i < iters; ++i) {
        for (int j = 0; j < 4; ++j) {
            int32_t tmp = (int32_t) a[j] - (int32_t) b[j];
            if (tmp > 32767)
                tmp = 32767;
            else if (tmp < -32768)
                tmp = -32768;
            a[j] = (int16_t) tmp;
        }
        BENCH_CLOBBER();
    }
    sink += (uint64_t) a[0];
    return (double) (bench_now_ns() - start + sink);
}

static inline double
bench_mmx_psubusb(uint64_t iters, impl_kind_t impl)
{
    uint8_t  a[8] = { 100, 50, 25, 10, 5, 2, 1, 0 };
    uint8_t  b[8] = { 10, 5, 2, 1, 0, 100, 50, 25 };
    uint64_t sink = 0;

    uint64_t start = bench_now_ns();
#if defined(__aarch64__)
    if (impl == IMPL_NEON) {
        uint8x8_t va = vld1_u8(a);
        uint8x8_t vb = vld1_u8(b);
        for (uint64_t i = 0; i < iters; ++i) {
            uint8x8_t vc = vqsub_u8(va, vb);
            vst1_u8(a, vc);
            va = vc;
            BENCH_CLOBBER();
        }
        sink += a[0];
        return (double) (bench_now_ns() - start + sink);
    }
#endif
    (void) impl;
    for (uint64_t i = 0; i < iters; ++i) {
        for (int j = 0; j < 8; ++j) {
            int16_t tmp = (int16_t) a[j] - (int16_t) b[j];
            if (tmp < 0)
                tmp = 0;
            a[j] = (uint8_t) tmp;
        }
        BENCH_CLOBBER();
    }
    sink += a[0];
    return (double) (bench_now_ns() - start + sink);
}

static inline double
bench_mmx_psubusw(uint64_t iters, impl_kind_t impl)
{
    uint16_t a[4] = { 60000, 50000, 40000, 30000 };
    uint16_t b[4] = { 10000, 5000, 2000, 1000 };
    uint64_t sink = 0;

    uint64_t start = bench_now_ns();
#if defined(__aarch64__)
    if (impl == IMPL_NEON) {
        uint16x4_t va = vld1_u16(a);
        uint16x4_t vb = vld1_u16(b);
        for (uint64_t i = 0; i < iters; ++i) {
            uint16x4_t vc = vqsub_u16(va, vb);
            vst1_u16(a, vc);
            va = vc;
            BENCH_CLOBBER();
        }
        sink += a[0];
        return (double) (bench_now_ns() - start + sink);
    }
#endif
    (void) impl;
    for (uint64_t i = 0; i < iters; ++i) {
        for (int j = 0; j < 4; ++j) {
            int32_t tmp = (int32_t) a[j] - (int32_t) b[j];
            if (tmp < 0)
                tmp = 0;
            a[j] = (uint16_t) tmp;
        }
        BENCH_CLOBBER();
    }
    sink += a[0];
    return (double) (bench_now_ns() - start + sink);
}

static inline double
bench_mmx_pmaddwd(uint64_t iters, impl_kind_t impl)
{
    int16_t  a[4] = { 1000, -2000, 3000, -4000 };
    int16_t  b[4] = { 10, -20, 30, -40 };
    uint64_t sink = 0;

    uint64_t start = bench_now_ns();
#if defined(__aarch64__)
    if (impl == IMPL_NEON) {
        int16x4_t va = vld1_s16(a);
        int16x4_t vb = vld1_s16(b);
        for (uint64_t i = 0; i < iters; ++i) {
            int32x4_t vc = vmull_s16(va, vb);
            int32x2_t vd = vpadd_s32(vget_low_s32(vc), vget_high_s32(vc));
            vst1_s32((int32_t *) a, vd);
            va = vld1_s16(a); // reload since we overwrote
            BENCH_CLOBBER();
        }
        sink += (uint64_t) a[0];
        return (double) (bench_now_ns() - start + sink);
    }
#endif
    (void) impl;
    for (uint64_t i = 0; i < iters; ++i) {
        int32_t tmp[2];
        tmp[0]             = (int32_t) a[0] * (int32_t) b[0] + (int32_t) a[1] * (int32_t) b[1];
        tmp[1]             = (int32_t) a[2] * (int32_t) b[2] + (int32_t) a[3] * (int32_t) b[3];
        ((int32_t *) a)[0] = tmp[0];
        ((int32_t *) a)[1] = tmp[1];
        BENCH_CLOBBER();
    }
    sink += (uint64_t) a[0];
    return (double) (bench_now_ns() - start + sink);
}

static inline double
bench_mmx_packsswb(uint64_t iters, impl_kind_t impl)
{
    int16_t  a[4] = { 30000, -30000, 100, -100 };
    int16_t  b[4] = { 20000, -20000, 50, -50 };
    uint64_t sink = 0;

    uint64_t start = bench_now_ns();
#if defined(__aarch64__)
    if (impl == IMPL_NEON) {
        int16x4_t va = vld1_s16(a);
        int16x4_t vb = vld1_s16(b);
        for (uint64_t i = 0; i < iters; ++i) {
            int16x8_t vc = vcombine_s16(va, vb);
            int8x8_t  vd = vqmovn_s16(vc);
            vst1_s8((int8_t *) a, vd);
            va = vld1_s16(a); // reload
            BENCH_CLOBBER();
        }
        sink += (uint64_t) ((int8_t *) a)[0];
        return (double) (bench_now_ns() - start + sink);
    }
#endif
    (void) impl;
    for (uint64_t i = 0; i < iters; ++i) {
        int8_t result[8];
        for (int j = 0; j < 4; ++j) {
            result[j]     = (int8_t) (a[j] > 127 ? 127 : a[j] < -128 ? -128
                                                                     : a[j]);
            result[j + 4] = (int8_t) (b[j] > 127 ? 127 : b[j] < -128 ? -128
                                                                     : b[j]);
        }
        memcpy(a, result, 8);
        BENCH_CLOBBER();
    }
    sink += (uint64_t) ((int8_t *) a)[0];
    return (double) (bench_now_ns() - start + sink);
}

static inline double
bench_mmx_packuswb(uint64_t iters, impl_kind_t impl)
{
    int16_t  a[4] = { 300, -100, 100, 0 };
    int16_t  b[4] = { 200, -50, 50, 0 };
    uint64_t sink = 0;

    uint64_t start = bench_now_ns();
#if defined(__aarch64__)
    if (impl == IMPL_NEON) {
        int16x4_t va = vld1_s16(a);
        int16x4_t vb = vld1_s16(b);
        for (uint64_t i = 0; i < iters; ++i) {
            int16x8_t vc = vcombine_s16(va, vb);
            uint8x8_t vd = vqmovun_s16(vc);
            vst1_u8((uint8_t *) a, vd);
            va = vld1_s16(a); // reload
            BENCH_CLOBBER();
        }
        sink += (uint64_t) ((uint8_t *) a)[0];
        return (double) (bench_now_ns() - start + sink);
    }
#endif
    (void) impl;
    for (uint64_t i = 0; i < iters; ++i) {
        uint8_t result[8];
        for (int j = 0; j < 4; ++j) {
            result[j]     = (uint8_t) (a[j] > 255 ? 255 : a[j] < 0 ? 0
                                                                   : a[j]);
            result[j + 4] = (uint8_t) (b[j] > 255 ? 255 : b[j] < 0 ? 0
                                                                   : b[j]);
        }
        memcpy(a, result, 8);

        BENCH_CLOBBER();
    }
    sink += (uint64_t) ((uint8_t *) a)[0];
    return (double) (bench_now_ns() - start + sink);
}

static inline double
bench_mmx_pshufb(uint64_t iters, impl_kind_t impl)
{
    uint8_t  a[8] = { 10, 20, 30, 40, 50, 60, 70, 80 };
    uint8_t  b[8] = { 7, 6, 5, 4, 3, 2, 1, 0 }; // indices (reverse)
    uint64_t sink = 0;

    uint64_t start = bench_now_ns();
#if defined(__aarch64__)
    if (impl == IMPL_NEON) {
        uint8x8_t va = vld1_u8(a);
        uint8x8_t vb = vld1_u8(b);
        for (uint64_t i = 0; i < iters; ++i) {
            uint8x8_t vc = vtbl1_u8(va, vb);
            vst1_u8(a, vc);
            va = vc;
            BENCH_CLOBBER();
        }
        sink += a[0];
        return (double) (bench_now_ns() - start + sink);
    }
#endif
    (void) impl;
    for (uint64_t i = 0; i < iters; ++i) {
        uint8_t result[8];
        for (int j = 0; j < 8; ++j) {
            uint8_t idx = b[j];
            result[j]   = (idx & 0x80) ? 0 : a[idx & 7];
        }
        memcpy(a, result, 8);
        BENCH_CLOBBER();
    }
    sink += a[0];
    return (double) (bench_now_ns() - start + sink);
}

/* Shift-immediate benchmarks to test architectural masking */
static inline double
bench_mmx_psrlw(uint64_t iters, impl_kind_t impl)
{
    uint16_t a[4] = { 0x8000, 0x4000, 0x2000, 0x1000 };
    uint64_t sink = 0;
    int shift = 31; /* > 15 to test masking */

    uint64_t start = bench_now_ns();
#if defined(__aarch64__)
    if (impl == IMPL_NEON) {
        uint16x4_t va = vld1_u16(a);
        uint16x4_t vshift = vdup_n_u16(shift & 0x0f);
        for (uint64_t i = 0; i < iters; ++i) {
            uint16x4_t vc = vshl_u16(va, vneg_s16(vreinterpret_s16_u16(vshift)));
            vst1_u16(a, vc);
            va = vc;
            BENCH_CLOBBER();
        }
        sink += a[0];
        return (double) (bench_now_ns() - start + sink);
    }
#endif
    (void) impl;
    for (uint64_t i = 0; i < iters; ++i) {
        for (int j = 0; j < 4; ++j) {
            a[j] = a[j] >> (shift & 0x0f);
        }
        BENCH_CLOBBER();
    }
    sink += a[0];
    return (double) (bench_now_ns() - start + sink);
}

static inline double
bench_mmx_psrld(uint64_t iters, impl_kind_t impl)
{
    uint32_t a[2] = { 0x80000000, 0x40000000 };
    uint64_t sink = 0;
    int shift = 63; /* > 31 to test masking */

    uint64_t start = bench_now_ns();
#if defined(__aarch64__)
    if (impl == IMPL_NEON) {
        uint32x2_t va = vld1_u32(a);
        uint32x2_t vshift = vdup_n_u32(shift & 0x1f);
        for (uint64_t i = 0; i < iters; ++i) {
            uint32x2_t vc = vshl_u32(va, vneg_s32(vreinterpret_s32_u32(vshift)));
            vst1_u32(a, vc);
            va = vc;
            BENCH_CLOBBER();
        }
        sink += a[0];
        return (double) (bench_now_ns() - start + sink);
    }
#endif
    (void) impl;
    for (uint64_t i = 0; i < iters; ++i) {
        for (int j = 0; j < 2; ++j) {
            a[j] = a[j] >> (shift & 0x1f);
        }
        BENCH_CLOBBER();
    }
    sink += a[0];
    return (double) (bench_now_ns() - start + sink);
}

static inline double
bench_mmx_psrlq(uint64_t iters, impl_kind_t impl)
{
    uint64_t a[1] = { 0x8000000000000000ULL };
    uint64_t sink = 0;
    int shift = 127; /* > 63 to test masking */

    uint64_t start = bench_now_ns();
#if defined(__aarch64__)
    if (impl == IMPL_NEON) {
        uint64x1_t va = vld1_u64(a);
        uint64x1_t vshift = vdup_n_u64(shift & 0x3f);
        for (uint64_t i = 0; i < iters; ++i) {
            uint64x1_t vc = vshl_u64(va, vneg_s64(vreinterpret_s64_u64(vshift)));
            vst1_u64(a, vc);
            va = vc;
            BENCH_CLOBBER();
        }
        sink += a[0];
        return (double) (bench_now_ns() - start + sink);
    }
#endif
    (void) impl;
    for (uint64_t i = 0; i < iters; ++i) {
        a[0] = a[0] >> (shift & 0x3f);
        BENCH_CLOBBER();
    }
    sink += a[0];
    return (double) (bench_now_ns() - start + sink);
}

static inline double
bench_mmx_psraw(uint64_t iters, impl_kind_t impl)
{
    int16_t a[4] = { -32768, -16384, 16384, 32767 };
    uint64_t sink = 0;
    int shift = 31; /* > 15 to test masking */

    uint64_t start = bench_now_ns();
#if defined(__aarch64__)
    if (impl == IMPL_NEON) {
        int16x4_t va = vld1_s16(a);
        int16x4_t vshift = vdup_n_s16(shift & 0x0f);
        for (uint64_t i = 0; i < iters; ++i) {
            int16x4_t vc = vshl_s16(va, vneg_s16(vshift));
            vst1_s16(a, vc);
            va = vc;
            BENCH_CLOBBER();
        }
        sink += a[0];
        return (double) (bench_now_ns() - start + sink);
    }
#endif
    (void) impl;
    for (uint64_t i = 0; i < iters; ++i) {
        for (int j = 0; j < 4; ++j) {
            a[j] = a[j] >> (shift & 0x0f);
        }
        BENCH_CLOBBER();
    }
    sink += a[0];
    return (double) (bench_now_ns() - start + sink);
}

static inline double
bench_mmx_psrad(uint64_t iters, impl_kind_t impl)
{
    int32_t a[2] = { -2147483648, 2147483647 };
    uint64_t sink = 0;
    int shift = 63; /* > 31 to test masking */

    uint64_t start = bench_now_ns();
#if defined(__aarch64__)
    if (impl == IMPL_NEON) {
        int32x2_t va = vld1_s32(a);
        int32x2_t vshift = vdup_n_s32(shift & 0x1f);
        for (uint64_t i = 0; i < iters; ++i) {
            int32x2_t vc = vshl_s32(va, vneg_s32(vshift));
            vst1_s32(a, vc);
            va = vc;
            BENCH_CLOBBER();
        }
        sink += a[0];
        return (double) (bench_now_ns() - start + sink);
    }
#endif
    (void) impl;
    for (uint64_t i = 0; i < iters; ++i) {
        for (int j = 0; j < 2; ++j) {
            a[j] = a[j] >> (shift & 0x1f);
        }
        BENCH_CLOBBER();
    }
    sink += a[0];
    return (double) (bench_now_ns() - start + sink);
}

static inline double
bench_mmx_psllw(uint64_t iters, impl_kind_t impl)
{
    uint16_t a[4] = { 1, 2, 3, 4 };
    uint64_t sink = 0;
    int shift = 31; /* > 15 to test masking */

    uint64_t start = bench_now_ns();
#if defined(__aarch64__)
    if (impl == IMPL_NEON) {
        uint16x4_t va = vld1_u16(a);
        uint16x4_t vshift = vdup_n_u16(shift & 0x0f);
        for (uint64_t i = 0; i < iters; ++i) {
            uint16x4_t vc = vshl_u16(va, vreinterpret_s16_u16(vshift));
            vst1_u16(a, vc);
            va = vc;
            BENCH_CLOBBER();
        }
        sink += a[0];
        return (double) (bench_now_ns() - start + sink);
    }
#endif
    (void) impl;
    for (uint64_t i = 0; i < iters; ++i) {
        for (int j = 0; j < 4; ++j) {
            a[j] = a[j] << (shift & 0x0f);
        }
        BENCH_CLOBBER();
    }
    sink += a[0];
    return (double) (bench_now_ns() - start + sink);
}

static inline double
bench_mmx_pslld(uint64_t iters, impl_kind_t impl)
{
    uint32_t a[2] = { 1, 2 };
    uint64_t sink = 0;
    int shift = 63; /* > 31 to test masking */

    uint64_t start = bench_now_ns();
#if defined(__aarch64__)
    if (impl == IMPL_NEON) {
        uint32x2_t va = vld1_u32(a);
        uint32x2_t vshift = vdup_n_u32(shift & 0x1f);
        for (uint64_t i = 0; i < iters; ++i) {
            uint32x2_t vc = vshl_u32(va, vreinterpret_s32_u32(vshift));
            vst1_u32(a, vc);
            va = vc;
            BENCH_CLOBBER();
        }
        sink += a[0];
        return (double) (bench_now_ns() - start + sink);
    }
#endif
    (void) impl;
    for (uint64_t i = 0; i < iters; ++i) {
        for (int j = 0; j < 2; ++j) {
            a[j] = a[j] << (shift & 0x1f);
        }
        BENCH_CLOBBER();
    }
    sink += a[0];
    return (double) (bench_now_ns() - start + sink);
}

static inline double
bench_mmx_psllq(uint64_t iters, impl_kind_t impl)
{
    uint64_t a[1] = { 1 };
    uint64_t sink = 0;
    int shift = 127; /* > 63 to test masking */

    uint64_t start = bench_now_ns();
#if defined(__aarch64__)
    if (impl == IMPL_NEON) {
        uint64x1_t va = vld1_u64(a);
        uint64x1_t vshift = vdup_n_u64(shift & 0x3f);
        for (uint64_t i = 0; i < iters; ++i) {
            uint64x1_t vc = vshl_u64(va, vreinterpret_s64_u64(vshift));
            vst1_u64(a, vc);
            va = vc;
            BENCH_CLOBBER();
        }
        sink += a[0];
        return (double) (bench_now_ns() - start + sink);
    }
#endif
    (void) impl;
    for (uint64_t i = 0; i < iters; ++i) {
        a[0] = a[0] << (shift & 0x3f);
        BENCH_CLOBBER();
    }
    sink += a[0];
    return (double) (bench_now_ns() - start + sink);
}