#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bench_mocks.h"

void
test_ir_generation()
{
    printf("--- Testing IR Generation ---\n");

    ir_data_t *ir = codegen_ir_init();
    if (!ir) {
        printf("FAILURE: Could not initialize IR context.\n");
        return;
    }
    printf("SUCCESS: IR context initialized.\n");

    // Mock block setup
    codeblock_t *block = calloc(1, sizeof(codeblock_t));
    block->pc          = 0x1234;
    ir->block          = block;

    printf("Verifying IR data structure integrity...\n");
    if (ir->wr_pos == 0) {
        printf("SUCCESS: Initial uOP count is 0.\n");
    } else {
        printf("WARNING: Unexpected initial uOP count: %d\n", ir->wr_pos);
    }

    printf("Generating a dummy uOP (PADDB)...\n");
    // Verify we can use the actual uOP macros from codegen_ir_defs.h
    uop_gen_reg_dst_src2(UOP_PADDB, ir, IREG_MM0, IREG_MM1, IREG_MM2);

    if (ir->wr_pos > 0) {
        uop_t *uop = &ir->uops[ir->wr_pos - 1];
        if (uop->type == UOP_PADDB) {
            printf("SUCCESS: PADDB uOP generated.\n");
        } else {
            printf("FAILURE: uOP type mismatch (got 0x%X).\n", uop->type);
        }
    } else {
        printf("FAILURE: uOP generation failed to update wr_pos.\n");
    }

    // Clean up
    free(block);
    // Note: codegen_ir_init doesn't have a public 'free' in codegen_ir.h,
    // but we're in a mock environment.
    free(ir);
}

void
test_mmx_enter_optimization()
{
    printf("\n--- Testing MMX_ENTER Optimization ---\n");

    ir_data_t *ir = codegen_ir_init();
    codeblock_t *block = calloc(1, sizeof(codeblock_t));
    ir->block = block;

    // Reset global state for MMX tracking
    extern int codegen_mmx_entered;
    codegen_mmx_entered = 0;

    printf("Generating MMX_ENTER...\n");
    uop_MMX_ENTER(ir);

    if (ir->wr_pos > 0) {
        // Find the CALL uop. It might not be the first one (MOV_IMM etc might precede it)
        int found = 0;
        for (int i = 0; i < ir->wr_pos; ++i) {
            uop_t *uop = &ir->uops[i];
            // Check for UOP_CALL_FUNC_RESULT (0x16) and UOP_TYPE_ORDER_BARRIER (1<<27)
            // UOP_MASK is 0xffff. 0x16 is the op.
            if ((uop->type & UOP_MASK) == 0x16) {
                found = 1;
                if (uop->type & UOP_TYPE_ORDER_BARRIER) {
                    printf("SUCCESS: MMX_ENTER uses ORDER_BARRIER (Registers Preserved).\n");
                } else if (uop->type & UOP_TYPE_BARRIER) {
                    printf("FAILURE: MMX_ENTER uses BARRIER (Registers Flushed/Invalidated).\n");
                } else {
                    printf("FAILURE: MMX_ENTER uses unknown barrier type: %08X\n", uop->type);
                }
            }
        }
        if (!found) {
             printf("WARNING: MMX_ENTER did not emit a CALL uOP (maybe already entered?)\n");
        }
    } else {
        printf("FAILURE: MMX_ENTER generated no uOPs.\n");
    }

    free(block);
    free(ir);
}

void
test_cache_metrics()
{
    printf("\n--- Testing Cache Metrics Infrastructure ---\n");

    codegen_cache_metrics_reset();
    if (codegen_cache_metrics.hits == 0 && codegen_cache_metrics.misses == 0) {
        printf("SUCCESS: Cache metrics reset to zero.\n");
    } else {
        printf("FAILURE: Cache metrics reset failed.\n");
    }

    codegen_cache_metrics.hits = 42;
    codegen_cache_metrics_t out;
    codegen_cache_metrics_get(&out);

    if (out.hits == 42) {
        printf("SUCCESS: Cache metrics retrieval works.\n");
    } else {
        printf("FAILURE: Cache metrics retrieval failed (got %llu).\n", out.hits);
    }
}

int
main(int argc, char **argv)
{
    printf("=== 86Box Dynarec Sanity Tool ===\n");
    printf("Platform: Apple Silicon (ARM64) Mock Mode\n\n");

    test_ir_generation();
    test_mmx_enter_optimization();
    test_cache_metrics();

    printf("\nSanity checks complete.\n");
    return 0;
}
