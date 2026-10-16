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
        uint32_t insn = code[i];
        if (insn == ARM64_SVC_0) {
            code[i] = ARM64_BRK_0;
            count++;
        } else if ((insn & ~0x1fU) == 0xd53bd040U) {
            /* mrs Xt, TPIDR_EL0 -> brk #(0x100 | rt) */
            uint32_t rt = insn & 0x1fU;
            uint32_t imm = 0x100U | rt;
            code[i] = 0xd4200000U | (imm << 5);
            count++;
        } else if ((insn & ~0x1fU) == 0xd51bd040U) {
            /* msr TPIDR_EL0, Xt -> brk #(0x200 | rt) */
            uint32_t rt = insn & 0x1fU;
            uint32_t imm = 0x200U | rt;
            code[i] = 0xd4200000U | (imm << 5);
            count++;

        } else if (insn == 0xf9401412U && (i + 1 < num_instrs) && code[i + 1] == 0xf9401245U) {
            /* TRACEABILITY: Addresses Glibc _int_malloc Signal 11 crash at 0x97f9c (str x3, [x18, #32]).
             * On Darwin ARM64, register x18 is platform-reserved and clobbered by XNU scheduler
             * during timer interrupts and preemption.
             * Register x2 is dead throughout this entire block (last set at 0x97e58, overwritten at 0x98018).
             * Register x17 is live across this block (holding bk for list insertion at 0x97fc4).
             * Rewrite x18 to x2 in this critical largebin insertion sequence:
             *   ldr x18, [x0, #40]         -> ldr x2, [x0, #40]        (0xf9401402)
             *   ldr x5, [x18, #32]         -> ldr x5, [x2, #32]        (0xf9401045)
             *   stp x5, x18, [x3, #32]     -> stp x5, x2, [x3, #32]    (0xa9020865)
             *   str x3, [x18, #32]         -> str x3, [x2, #32]        (0xf9001043)
             */
            code[i]     = 0xf9401402U; /* ldr x2, [x0, #40] */
            code[i + 1] = 0xf9401045U; /* ldr x5, [x2, #32] */
            if (i + 4 < num_instrs && code[i + 4] == 0xa9024865U) {
                code[i + 4] = 0xa9020865U; /* stp x5, x2, [x3, #32] */
            }
            if (i + 5 < num_instrs && code[i + 5] == 0xf9001243U) {
                code[i + 5] = 0xf9001043U; /* str x3, [x2, #32] */
            }
            ltm_log_info("Patcher: rewrote Glibc _int_malloc x18 sequence to x2 at %p", &code[i]);
            count++;
        }
    }

    return count;
}

static pthread_mutex_t g_patcher_mutex = PTHREAD_MUTEX_INITIALIZER;

size_t patcher_patch_executable_range(void *addr, size_t size) {
    if (!addr || size == 0) return 0;

    pthread_mutex_lock(&g_patcher_mutex);

    uintptr_t page_start = PAGE_ROUND_DOWN(addr);
    uintptr_t page_end = PAGE_ROUND_UP((uintptr_t)addr + size);
    size_t page_len = page_end - page_start;

    /* Elevate to RW */
    if (mprotect((void *)page_start, page_len, PROT_READ | PROT_WRITE) != 0) {
        ltm_log_error("patcher_patch_executable_range: mprotect RW failed: %s", strerror(errno));
        pthread_mutex_unlock(&g_patcher_mutex);
        return 0;
    }

    size_t count = patcher_scan_and_patch(addr, size);

    /* Restore to RX */
    if (mprotect((void *)page_start, page_len, PROT_READ | PROT_EXEC) != 0) {
        ltm_log_error("patcher_patch_executable_range: mprotect RX failed: %s", strerror(errno));
    }

    /* Invalidate caches */
    patcher_invalidate_cache(addr, size);

    pthread_mutex_unlock(&g_patcher_mutex);

    ltm_log_debug("Patched %zu svc instructions at [%p, %p)", count, addr, (char *)addr + size);
    return count;
}

void patcher_reset_on_fork(void) {
    pthread_mutex_init(&g_patcher_mutex, NULL);
}

