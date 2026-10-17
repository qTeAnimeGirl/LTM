#ifndef PATCHER_H
#define PATCHER_H

#include "ltm.h"

#define ARM64_BRK_X18 (0xd4200000U | (0x1800U << 5))

/* Scans memory buffer [addr, addr + size) for Linux svc #0 (0xd4000001)
 * and rewrites to brk #0 (0xd4200000). Buffer must be writable.
 * Returns number of instructions patched. */
size_t patcher_scan_and_patch(void *addr, size_t size);
size_t patcher_scan_and_patch_ex(void *addr, size_t size, bool is_interp);

/* Patches an already mapped executable range by temporarily elevating
 * permissions to RW, rewriting instructions, restoring protection to RX,
 * and flushing the instruction cache.
 * Returns number of instructions patched. */
size_t patcher_patch_executable_range(void *addr, size_t size);

/* Synchronizes/invalidates the instruction and data caches for the given range */
void patcher_invalidate_cache(void *addr, size_t size);

/* Reinitializes patcher mutex in child process after fork */
void patcher_reset_on_fork(void);

/* Universal x18 Virtualization Engine */
void x18_register_patch(uintptr_t pc, uint32_t orig_insn);
uint32_t x18_lookup_patch(uintptr_t pc);
bool aarch64_insn_uses_x18(uint32_t insn);
int patcher_patch_elf_mapped_file(int fd, void *mapped_addr, size_t mapped_len, off_t map_offset);

#endif /* PATCHER_H */
