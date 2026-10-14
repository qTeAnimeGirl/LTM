#ifndef PATCHER_H
#define PATCHER_H

#include "ltm.h"

/* Scans memory buffer [addr, addr + size) for Linux svc #0 (0xd4000001)
 * and rewrites to brk #0 (0xd4200000). Buffer must be writable.
 * Returns number of instructions patched. */
size_t patcher_scan_and_patch(void *addr, size_t size);

/* Patches an already mapped executable range by temporarily elevating
 * permissions to RW, rewriting instructions, restoring protection to RX,
 * and flushing the instruction cache.
 * Returns number of instructions patched. */
size_t patcher_patch_executable_range(void *addr, size_t size);

/* Synchronizes/invalidates the instruction and data caches for the given range */
void patcher_invalidate_cache(void *addr, size_t size);

#endif /* PATCHER_H */
