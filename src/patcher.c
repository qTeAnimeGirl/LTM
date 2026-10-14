#include "patcher.h"
#include <libkern/OSCacheControl.h>

void patcher_invalidate_cache(void *addr, size_t size) {
    if (!addr || size == 0) return;
    sys_icache_invalidate(addr, size);
    __builtin___clear_cache((char *)addr, (char *)addr + size);
}

size_t patcher_scan_and_patch(void *addr, size_t size) {
    if (!addr || size < 4) return 0;

    uint32_t *code = (uint32_t *)addr;
    size_t count = 0;
    size_t num_instrs = size / 4;

    for (size_t i = 0; i < num_instrs; i++) {
        if (code[i] == ARM64_SVC_0) {
            code[i] = ARM64_BRK_0;
            count++;
        }
    }

    return count;
}

size_t patcher_patch_executable_range(void *addr, size_t size) {
    if (!addr || size == 0) return 0;

    uintptr_t page_start = PAGE_ROUND_DOWN(addr);
    uintptr_t page_end = PAGE_ROUND_UP((uintptr_t)addr + size);
    size_t page_len = page_end - page_start;

    /* Elevate to RW */
    if (mprotect((void *)page_start, page_len, PROT_READ | PROT_WRITE) != 0) {
        ltm_log_error("patcher_patch_executable_range: mprotect RW failed: %s", strerror(errno));
        return 0;
    }

    size_t count = patcher_scan_and_patch(addr, size);

    /* Restore to RX */
    if (mprotect((void *)page_start, page_len, PROT_READ | PROT_EXEC) != 0) {
        ltm_log_error("patcher_patch_executable_range: mprotect RX failed: %s", strerror(errno));
    }

    /* Invalidate caches */
    patcher_invalidate_cache(addr, size);

    ltm_log_debug("Patched %zu svc instructions at [%p, %p)", count, addr, (char *)addr + size);
    return count;
}
