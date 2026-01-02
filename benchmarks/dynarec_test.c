#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <86box/86box.h>
#include <86box/cpu.h>
#include <86box/mem.h>

// Include dynarec headers
#include "codegen_new/codegen.h"
#include "codegen_new/codegen_backend.h"
#include "codegen_new/codegen_ir.h"
#include "codegen_new/codegen_reg.h"

// Simple timing function
static double
get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

// Initialize minimal 86Box state for dynarec testing
static void
init_minimal_state(void)
{
    // Initialize memory
    mem_init();

    // Initialize CPU state
    cpu_init();

    // Initialize codegen
    codegen_backend_init();
    codegen_init();

    // Reset cache metrics
    codegen_cache_metrics_reset();
}

// Create a simple dynarec test that performs MMX operations
static void *
create_mmx_test_block(uint64_t iterations)
{
    // Create a new IR data structure
    ir_data_t *ir = codegen_ir_init();

    // Set up basic block info
    ir->code_block.start_pc = 0x1000;  // Fake PC
    ir->code_block._cs = 0;
    ir->code_block.flags = CODEBLOCK_HAS_MMXP | CODEBLOCK_BYTE_MASK;

    // Reserve MMX registers
    int mmx_reg1 = codegen_reg_alloc_readwrite(ir, IREG_MM0, 0);
    int mmx_reg2 = codegen_reg_alloc_readwrite(ir, IREG_MM1, 0);
    int temp_reg = codegen_reg_alloc_readwrite(ir, IREG_temp0, 0);

    // Initialize MMX state
    uop_MMX_ENTER(ir);

    // Load test data into MMX registers
    uop_MOV_IMM(ir, temp_reg, 0x123456789ABCDEF0ULL);  // Test data
    uop_MOVD_R_MM(ir, temp_reg, mmx_reg1);
    uop_MOV_IMM(ir, temp_reg, 0xFEDCBA9876543210ULL);  // Test data
    uop_MOVD_R_MM(ir, temp_reg, mmx_reg2);

    // Create a loop that performs MMX operations
    int loop_start = ir->wr_pos;
    uop_PADDB(ir, mmx_reg1, mmx_reg1, mmx_reg2);  // mmx_reg1 = mmx_reg1 + mmx_reg2 (byte add)
    uop_PSUBB(ir, mmx_reg2, mmx_reg2, mmx_reg1);  // mmx_reg2 = mmx_reg2 - mmx_reg1 (byte sub)

    // Decrement loop counter (simplified - just run fixed iterations)
    // For now, we'll just execute the operations directly

    // Compile the IR to machine code
    codeblock_t *block = codegen_block_init(ir->code_block.start_pc);
    codegen_backend_compile_block(ir, block);

    // Clean up IR
    codegen_ir_cleanup(ir);

    return block->data;
}

static double
run_dynarec_test(uint64_t iterations)
{
    // Create test code block
    void *test_code = create_mmx_test_block(iterations);
    if (!test_code) {
        fprintf(stderr, "Failed to create test code block\n");
        return -1.0;
    }

    // Cast to function pointer
    void (*test_func)(void) = (void (*)(void))test_code;

    // Reset cache metrics before test
    codegen_cache_metrics_reset();

    // Time the execution
    double start_time = get_time_ns();

    // Execute the test function multiple times
    for (uint64_t i = 0; i < iterations; i++) {
        test_func();
    }

    double end_time = get_time_ns();
    double total_ns = end_time - start_time;

    // Print cache metrics
    printf("Cache Metrics after %llu iterations:\n", iterations);
    printf("  Hits: %llu\n", codegen_cache_metrics.hits);
    printf("  Misses: %llu\n", codegen_cache_metrics.misses);
    printf("  Flushes: %llu\n", codegen_cache_metrics.flushes);
    printf("  Recompiles: %llu\n", codegen_cache_metrics.recompiles);
    printf("  Blocks compiled: %llu\n", codegen_cache_metrics.blocks_compiled);
    printf("  Bytes emitted: %llu\n", codegen_cache_metrics.bytes_emitted);
    printf("  Max block bytes: %llu\n", codegen_cache_metrics.max_block_bytes);

    return total_ns / (double)iterations;
}

int
main(int argc, char **argv)
{
    uint64_t iterations = 1000000;

    // Parse command line args
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--iters=", 8) == 0) {
            iterations = strtoull(argv[i] + 8, NULL, 10);
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [--iters=N]\n", argv[0]);
            printf("Run dynarec MMX test with N iterations\n");
            return 0;
        }
    }

    printf("Initializing dynarec test environment...\n");
    init_minimal_state();

    printf("Running dynarec MMX test with %llu iterations...\n", iterations);
    double ns_per_iter = run_dynarec_test(iterations);

    if (ns_per_iter > 0) {
        printf("Average time per iteration: %.2f ns\n", ns_per_iter);
        printf("Total time: %.2f ms\n", (ns_per_iter * iterations) / 1e6);
    } else {
        printf("Test failed\n");
        return 1;
    }

    return 0;
}