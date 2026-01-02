#ifndef BENCH_MOCKS_H
#define BENCH_MOCKS_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include "x86_ops.h"
#include "codegen.h"
#include "codegen_ir.h"
#include "codegen_reg.h"

/* --- 86BOX CORE MOCK SYMBOLS --- */

/* Global Variables */
cpu_state_t                  cpu_state;
page_t                      *pages;
codeblock_t                 *codeblock;
uint16_t                    *codeblock_hash;
uint8_t                     *block_write_data;
uint16_t                     cpu_cur_status = 0;
uint64_t                     dirty_ir_regs[2];
codegen_cache_metrics_t      codegen_cache_metrics;
codegen_cache_tuning_state_t codegen_cache_tuning;

/* Register Management Mocks (Matching codegen_reg.h) */
reg_version_t reg_version[IREG_COUNT][256];
uint8_t       reg_last_version[IREG_COUNT];
int           max_version_refcount = 0;
uint16_t      reg_dead_list        = 0;

/* Mock Functions */
void
codegen_init(void)
{
}
void
codegen_cache_tuning_init(void)
{
}
void
codegen_cache_tuning_update(void)
{
}

ir_data_t *
codegen_ir_init(void)
{
    ir_data_t *ir   = calloc(1, sizeof(ir_data_t));
    ir->block       = calloc(1, sizeof(codeblock_t));
    ir->block->data = malloc(1024);
    return ir;
}

void
codegen_ir_compile(ir_data_t *ir, codeblock_t *block)
{
    printf("  [MOCK] Compiled IR for block at PC 0x%08X\n", block->pc);
    if (block->data)
        block->data[0] = 0xC3; // RET
}

void
codegen_cache_metrics_reset(void)
{
    memset(&codegen_cache_metrics, 0, sizeof(codegen_cache_metrics));
}

void
codegen_cache_metrics_get(codegen_cache_metrics_t *out_metrics)
{
    if (out_metrics)
        memcpy(out_metrics, &codegen_cache_metrics, sizeof(codegen_cache_metrics));
}

void
codegen_cache_metrics_print_summary(void)
{
    printf("Cache Metrics: Hits=%llu, Misses=%llu, Flushes=%llu\n",
           codegen_cache_metrics.hits, codegen_cache_metrics.misses, codegen_cache_metrics.flushes);
}

void
pclog(const char *fmt, ...)
{
}
void
fatal(const char *fmt, ...)
{
    exit(1);
}
int
pc_init(int argc, char *argv[])
{
    return 0;
}

/* Memory Mocks */
void
mem_read_phys(void *dest, uint32_t addr, int tranfer_size)
{
    if (dest)
        memset(dest, 0, tranfer_size);
}
void
mem_write_phys(void *src, uint32_t addr, int tranfer_size)
{
}
void *
plat_mmap_exec(size_t size)
{
    return malloc(size);
}

// Dynarec global variables
int      cpu_block_end = 0;
uint32_t codegen_endpc = 0;
int      cpu_reps      = 0;
int      cpu_notreps   = 0;

int
reg_is_native_size(ir_reg_t ir_reg)
{
    return 1;
}
#endif /* BENCH_MOCKS_H */
