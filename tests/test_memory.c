#include "include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    ltm_print("[TEST] test_memory: Testing heap (brk) and virtual pages (mmap)...\n");

    // 1. Test brk heap expansion
    ltm_print("  Step 1: Testing sys_brk expansion...\n");
    void *cur_brk = sys_brk(NULL);
    TEST_ASSERT(cur_brk != MAP_FAILED && cur_brk != NULL, "sys_brk(0) failed");
    ltm_print("    Initial brk: ");
    ltm_print_hex((uintptr_t)cur_brk);
    ltm_print("\n");

    uintptr_t expand_size = 64 * 1024; // 64 KB
    void *target_brk = (void *)((uintptr_t)cur_brk + expand_size);
    void *new_brk = sys_brk(target_brk);
    TEST_ASSERT(new_brk >= target_brk, "sys_brk failed to expand heap");
    ltm_print("    Expanded brk: ");
    ltm_print_hex((uintptr_t)new_brk);
    ltm_print("\n");

    // Write data pattern to heap
    ltm_print("    Writing and verifying heap data pattern...\n");
    uint32_t *heap_u32 = (uint32_t *)cur_brk;
    size_t num_words = expand_size / sizeof(uint32_t);
    for (size_t i = 0; i < num_words; i++) {
        heap_u32[i] = (uint32_t)(0xAA550000 ^ i);
    }
    for (size_t i = 0; i < num_words; i++) {
        TEST_ASSERT(heap_u32[i] == (uint32_t)(0xAA550000 ^ i), "Heap pattern corrupted");
    }
    ltm_print("    Heap integrity verified!\n");

    // 2. Test mmap anonymous pages
    ltm_print("  Step 2: Testing sys_mmap anonymous memory allocation...\n");
    size_t mmap_size = 128 * 1024; // 128 KB
    void *map_addr = sys_mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST_ASSERT(map_addr != MAP_FAILED && map_addr != NULL, "sys_mmap failed");
    ltm_print("    mmap mapped region: ");
    ltm_print_hex((uintptr_t)map_addr);
    ltm_print("\n");

    // Touch all 4KB blocks in the mapped range
    ltm_print("    Writing multi-page pattern to mapped region...\n");
    uint8_t *bytes = (uint8_t *)map_addr;
    size_t pages = mmap_size / 4096;
    for (size_t p = 0; p < pages; p++) {
        bytes[p * 4096] = (uint8_t)(p ^ 0x3C);
        bytes[p * 4096 + 2048] = (uint8_t)(p ^ 0xC3);
        bytes[p * 4096 + 4095] = (uint8_t)(p ^ 0x5A);
    }

    // Verify all pattern checkpoints
    for (size_t p = 0; p < pages; p++) {
        TEST_ASSERT(bytes[p * 4096] == (uint8_t)(p ^ 0x3C), "mmap start page byte mismatch");
        TEST_ASSERT(bytes[p * 4096 + 2048] == (uint8_t)(p ^ 0xC3), "mmap mid page byte mismatch");
        TEST_ASSERT(bytes[p * 4096 + 4095] == (uint8_t)(p ^ 0x5A), "mmap end page byte mismatch");
    }
    ltm_print("    mmap page pattern verified!\n");

    // 3. Test munmap
    ltm_print("  Step 3: Testing sys_munmap...\n");
    int unmap_res = sys_munmap(map_addr, mmap_size);
    TEST_ASSERT(unmap_res == 0, "sys_munmap returned error");
    ltm_print("    munmap succeeded!\n");

    TEST_PASS("test_memory: Both brk and mmap memory allocation validated!");
    return 0;
}
