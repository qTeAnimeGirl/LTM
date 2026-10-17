#include "patcher.h"
#include "elf_loader.h"
#include <libkern/OSCacheControl.h>
#include <sys/mman.h>

#define ARM64_SVC_0    0xd4000001U
#define ARM64_BRK_0    0xd4200000U
#define ARM64_BRK_X18  (0xd4200000U | (0x1800U << 5))

/* Universal x18 Virtualization Hash Table */
typedef struct {
    uintptr_t pc;
    uint32_t orig_insn;
} x18_patch_entry_t;

#define X18_HASH_SIZE 65536
#define X18_HASH_MASK (X18_HASH_SIZE - 1)

static x18_patch_entry_t g_x18_patches[X18_HASH_SIZE];
static pthread_mutex_t g_x18_patch_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_patcher_mutex = PTHREAD_MUTEX_INITIALIZER;

void patcher_invalidate_cache(void *addr, size_t size) {
    if (!addr || size == 0) return;
    sys_icache_invalidate(addr, size);
    __builtin___clear_cache((char *)addr, (char *)addr + size);
}

void x18_register_patch(uintptr_t pc, uint32_t orig_insn) {
    pthread_mutex_lock(&g_x18_patch_mutex);
    uint32_t idx = (uint32_t)(((pc >> 2) ^ (pc >> 16)) & X18_HASH_MASK);
    for (size_t i = 0; i < X18_HASH_SIZE; i++) {
        uint32_t cur = (idx + i) & X18_HASH_MASK;
        if (g_x18_patches[cur].pc == 0 || g_x18_patches[cur].pc == pc) {
            g_x18_patches[cur].pc = pc;
            g_x18_patches[cur].orig_insn = orig_insn;
            pthread_mutex_unlock(&g_x18_patch_mutex);
            return;
        }
    }
    pthread_mutex_unlock(&g_x18_patch_mutex);
    ltm_log_error("x18 patch table capacity exceeded!");
}

uint32_t x18_lookup_patch(uintptr_t pc) {
    uint32_t idx = (uint32_t)(((pc >> 2) ^ (pc >> 16)) & X18_HASH_MASK);
    for (size_t i = 0; i < X18_HASH_SIZE; i++) {
        uint32_t cur = (idx + i) & X18_HASH_MASK;
        if (g_x18_patches[cur].pc == pc) {
            return g_x18_patches[cur].orig_insn;
        }
        if (g_x18_patches[cur].pc == 0) {
            break;
        }
    }
    return 0;
}

bool aarch64_insn_uses_x18(uint32_t insn) {
    uint32_t rd  = insn & 0x1f;
    uint32_t rn  = (insn >> 5) & 0x1f;
    uint32_t rt2 = (insn >> 10) & 0x1f;
    uint32_t rm  = (insn >> 16) & 0x1f;

    /* Group 1: Data Processing - Immediate ((insn & 0x1c000000) == 0x10000000) */
    if ((insn & 0x1c000000) == 0x10000000) {
        uint32_t op0 = (insn >> 23) & 0x7;
        if (op0 == 0 || op0 == 1) {
            /* ADR, ADRP */
            return (rd == 18);
        }
        if (op0 == 2 || op0 == 3) {
            /* Add/Sub immediate */
            return (rd == 18 || rn == 18);
        }
        if (op0 == 4) {
            /* Logical immediate */
            return (rd == 18 || rn == 18);
        }
        if (op0 == 5) {
            /* Move wide immediate (movz, movn, movk) */
            return (rd == 18);
        }
        if (op0 == 6) {
            /* Bitfield (sbfm, bfm, ubfm) */
            return (rd == 18 || rn == 18);
        }
        if (op0 == 7) {
            /* Extract (extr) */
            return (rd == 18 || rn == 18 || rm == 18);
        }
        return false;
    }

    /* Group 2: Data Processing - Register ((insn & 0x0e000000) == 0x0a000000) */
    if ((insn & 0x0e000000) == 0x0a000000) {
        bool is_3source = ((insn & 0x1f000000) == 0x1b000000);
        if (is_3source) {
            return (rd == 18 || rn == 18 || rm == 18 || rt2 == 18);
        }
        return (rd == 18 || rn == 18 || rm == 18);
    }

    /* Group 3: Branches, Exceptions, System ((insn & 0x1c000000) == 0x14000000) */
    if ((insn & 0x1c000000) == 0x14000000) {
        /* cbz / cbnz: bits [30:25] == 0b011010 */
        if ((insn & 0x7e000000) == 0x34000000) {
            return (rd == 18);
        }
        /* tbz / tbnz: bits [30:25] == 0b011011 */
        if ((insn & 0x7e000000) == 0x36000000) {
            return (rd == 18);
        }
        /* Unconditional branch register (br, blr, ret) */
        if ((insn & 0xfe1ffc1f) == 0xd61f0000) {
            return (rn == 18);
        }
        /* System instructions (msr, mrs) */
        if ((insn & 0xffd00000) == 0xd5100000) {
            return (rd == 18);
        }
        return false;
    }

    /* Group 4: Loads and Stores ((insn & 0x0a000000) == 0x08000000) */
    if ((insn & 0x0a000000) == 0x08000000) {
        /* Base register Rn is always GPR */
        if (rn == 18) return true;

        bool is_vector = (insn & (1 << 26)) != 0;
        bool is_pair = ((insn & 0x3a000000) == 0x28000000);
        bool is_reg_offset = ((insn & 0x3b200c00) == 0x38200800);

        if (is_reg_offset && rm == 18) {
            return true;
        }

        if (!is_vector) {
            if (rd == 18) return true;
            if (is_pair && rt2 == 18) return true;
        }
        return false;
    }

    /* Group 5: FP / SIMD conversions ((insn & 0x7f200000) == 0x1e200000) */
    if ((insn & 0x7f200000) == 0x1e200000) {
        if (rd == 18 || rn == 18) return true;
    }

    return false;
}

size_t patcher_scan_and_patch(void *addr, size_t size) {
    return patcher_scan_and_patch_ex(addr, size, false);
}

size_t patcher_scan_and_patch_ex(void *addr, size_t size, bool is_interp) {
    (void)is_interp;
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
        } else if (aarch64_insn_uses_x18(insn)) {
            uintptr_t pc = (uintptr_t)&code[i];
            x18_register_patch(pc, insn);
            code[i] = ARM64_BRK_X18;
            count++;
        }
    }

    return count;
}

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

    ltm_log_debug("Patched %zu instructions at [%p, %p)", count, addr, (char *)addr + size);
    return count;
}

void patcher_reset_on_fork(void) {
    pthread_mutex_init(&g_patcher_mutex, NULL);
    pthread_mutex_init(&g_x18_patch_mutex, NULL);
}

int patcher_patch_elf_mapped_file(int fd, void *mapped_addr, size_t mapped_len, off_t map_offset) {
    if (fd < 0 || !mapped_addr || mapped_len == 0) return -1;

    Elf64_Ehdr ehdr;
    if (pread(fd, &ehdr, sizeof(ehdr), 0) != sizeof(ehdr)) {
        return -1;
    }
    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0 || ehdr.e_ident[EI_CLASS] != ELFCLASS64) {
        return -1;
    }
    if (ehdr.e_shoff == 0 || ehdr.e_shnum == 0 || ehdr.e_shentsize != sizeof(Elf64_Shdr)) {
        return -1;
    }

    size_t sh_table_size = ehdr.e_shnum * sizeof(Elf64_Shdr);
    Elf64_Shdr *shdrs = malloc(sh_table_size);
    if (!shdrs) return -1;

    if (pread(fd, shdrs, sh_table_size, ehdr.e_shoff) != (ssize_t)sh_table_size) {
        free(shdrs);
        return -1;
    }

    int patched_sections = 0;
    for (int s = 0; s < ehdr.e_shnum; s++) {
        if ((shdrs[s].sh_flags & 0x4 /* SHF_EXECINSTR */) && shdrs[s].sh_size > 0) {
            uint64_t sec_off = shdrs[s].sh_offset;
            uint64_t sec_sz = shdrs[s].sh_size;

            /* Check if section overlaps with this mmap range [map_offset, map_offset + mapped_len) */
            if (sec_off >= (uint64_t)map_offset && sec_off < (uint64_t)map_offset + mapped_len) {
                uint64_t rel_off = sec_off - (uint64_t)map_offset;
                size_t patch_sz = sec_sz;
                if (rel_off + patch_sz > mapped_len) {
                    patch_sz = mapped_len - rel_off;
                }
                void *code_ptr = (void *)((uintptr_t)mapped_addr + rel_off);
                patcher_patch_executable_range(code_ptr, patch_sz);
                patched_sections++;
            }
        }
    }

    free(shdrs);
    return patched_sections;
}
