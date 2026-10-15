#include "elf_loader.h"
#include "patcher.h"
#include "sandbox.h"

static int elf_load_image(int fd, const char *filepath, uintptr_t pref_base,
                          uintptr_t *out_base, uintptr_t *out_entry,
                          uintptr_t *out_ph_addr, uint16_t *out_ph_num, uint16_t *out_ph_ent,
                          uintptr_t *out_max_vaddr, char *out_interp, size_t interp_buf_sz) {
    if (lseek(fd, 0, SEEK_SET) < 0) {
        ltm_log_error("Failed to rewind '%s'", filepath);
        return -1;
    }

    Elf64_Ehdr ehdr;
    if (read(fd, &ehdr, sizeof(ehdr)) != sizeof(ehdr)) {
        ltm_log_error("Failed to read ELF header from '%s'", filepath);
        return -1;
    }

    /* Validate ELF magic */
    if (ehdr.e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr.e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr.e_ident[EI_MAG3] != ELFMAG3) {
        ltm_log_error("Not an ELF binary: '%s'", filepath);
        return -1;
    }

    /* Validate architecture and endianness */
    if (ehdr.e_ident[EI_CLASS] != ELFCLASS64) {
        ltm_log_error("Not a 64-bit ELF binary: class %d", ehdr.e_ident[EI_CLASS]);
        return -1;
    }

    if (ehdr.e_ident[EI_DATA] != ELFDATA2LSB) {
        ltm_log_error("Not a little-endian ELF binary");
        return -1;
    }

    if (ehdr.e_machine != EM_AARCH64) {
        ltm_log_error("Not an ARM64 (AArch64) ELF binary (machine %d)", ehdr.e_machine);
        return -1;
    }

    if (ehdr.e_type != ET_EXEC && ehdr.e_type != ET_DYN) {
        ltm_log_error("Unsupported ELF type: %d (expected ET_EXEC or ET_DYN)", ehdr.e_type);
        return -1;
    }

    /* Read program headers */
    if (ehdr.e_phnum == 0 || ehdr.e_phentsize != sizeof(Elf64_Phdr)) {
        ltm_log_error("Invalid program header count or entry size (%d * %d)", ehdr.e_phnum, ehdr.e_phentsize);
        return -1;
    }

    size_t ph_table_bytes = ehdr.e_phnum * sizeof(Elf64_Phdr);
    Elf64_Phdr *phdrs = malloc(ph_table_bytes);
    if (!phdrs) {
        ltm_log_error("Out of memory allocating program headers");
        return -1;
    }

    if (lseek(fd, ehdr.e_phoff, SEEK_SET) < 0 ||
        (size_t)read(fd, phdrs, ph_table_bytes) != ph_table_bytes) {
        ltm_log_error("Failed to read program headers");
        free(phdrs);
        return -1;
    }

    /* Check for PT_INTERP */
    if (out_interp && interp_buf_sz > 0) {
        out_interp[0] = '\0';
        for (int i = 0; i < ehdr.e_phnum; i++) {
            if (phdrs[i].p_type == PT_INTERP) {
                if (lseek(fd, phdrs[i].p_offset, SEEK_SET) >= 0) {
                    size_t isz = phdrs[i].p_filesz < interp_buf_sz - 1 ? phdrs[i].p_filesz : interp_buf_sz - 1;
                    ssize_t r = read(fd, out_interp, isz);
                    if (r > 0) {
                        out_interp[r] = '\0';
                        while (r > 0 && (out_interp[r - 1] == '\0' || out_interp[r - 1] == '\n')) {
                            out_interp[--r] = '\0';
                        }
                    }
                }
                break;
            }
        }
    }

    /* Find virtual address bounds across all PT_LOAD segments */
    uint64_t min_vaddr = UINT64_MAX;
    uint64_t max_vaddr = 0;
    int load_segments = 0;

    for (int i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD) {
            load_segments++;
            if (phdrs[i].p_vaddr < min_vaddr) {
                min_vaddr = phdrs[i].p_vaddr;
            }
            if (phdrs[i].p_vaddr + phdrs[i].p_memsz > max_vaddr) {
                max_vaddr = phdrs[i].p_vaddr + phdrs[i].p_memsz;
            }
        }
    }

    if (load_segments == 0) {
        ltm_log_error("No PT_LOAD segments found in ELF '%s'", filepath);
        free(phdrs);
        return -1;
    }

    uintptr_t min_page = PAGE_ROUND_DOWN(min_vaddr);
    uintptr_t max_page = PAGE_ROUND_UP(max_vaddr);
    size_t total_span = max_page - min_page;

    uintptr_t load_base = 0;
    if (ehdr.e_type == ET_DYN) {
        void *reserved = mmap((void *)pref_base, total_span, PROT_NONE, MAP_ANON | MAP_PRIVATE, -1, 0);
        if (reserved == MAP_FAILED) {
            reserved = mmap(NULL, total_span, PROT_NONE, MAP_ANON | MAP_PRIVATE, -1, 0);
        }
        if (reserved == MAP_FAILED) {
            ltm_log_error("Failed to reserve memory for ET_DYN ELF '%s': %s", filepath, strerror(errno));
            free(phdrs);
            return -1;
        }
        load_base = (uintptr_t)reserved - min_page;
    } else {
        if (min_vaddr < 0x100000000ULL) {
            ltm_log_error("ELF binary '%s' is static non-PIE linked below 4GB (0x%llx). macOS Apple Silicon enforces a 4GB PAGEZERO.",
                          filepath, (unsigned long long)min_vaddr);
            free(phdrs);
            return -1;
        }
        load_base = 0;
    }

    ltm_log_debug("Loading '%s': base=0x%lx, entry=0x%lx, span=0x%zx",
                  filepath, (unsigned long)load_base, (unsigned long)(load_base + ehdr.e_entry), total_span);

    /* Allocate the contiguous span with RW */
    void *span_ptr = mmap((void *)(load_base + min_page), total_span,
                          PROT_READ | PROT_WRITE,
                          MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
    if (span_ptr == MAP_FAILED) {
        ltm_log_error("Failed to map segment memory for '%s': %s", filepath, strerror(errno));
        free(phdrs);
        return -1;
    }
    memset(span_ptr, 0, total_span);

    /* Load each PT_LOAD segment data into memory and patch svc #0 / TLS instructions */
    for (int i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type != PT_LOAD) continue;

        uintptr_t seg_start = load_base + phdrs[i].p_vaddr;
        if (phdrs[i].p_filesz > 0) {
            if (lseek(fd, phdrs[i].p_offset, SEEK_SET) < 0) {
                ltm_log_error("lseek failed for segment %d of '%s'", i, filepath);
                free(phdrs);
                return -1;
            }
            ssize_t read_bytes = read(fd, (void *)seg_start, phdrs[i].p_filesz);
            if (read_bytes < 0 || (size_t)read_bytes != phdrs[i].p_filesz) {
                ltm_log_error("read failed for segment %d of '%s'", i, filepath);
                free(phdrs);
                return -1;
            }
        }

        if (phdrs[i].p_flags & PF_X) {
            size_t patched = patcher_scan_and_patch((void *)seg_start, phdrs[i].p_filesz);
            ltm_log_debug("Segment %d (RX) of '%s': patched %zu instructions", i, filepath, patched);
        }
    }

    /* Apply dynamic relocations if present */
    for (int i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type == PT_DYNAMIC) {
            Elf64_Dyn *dyn = (Elf64_Dyn *)(load_base + phdrs[i].p_vaddr);
            uintptr_t rela_addr = 0;
            size_t rela_sz = 0;
            size_t rela_ent = sizeof(Elf64_Rela);

            for (Elf64_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
                if (d->d_tag == DT_RELA) {
                    rela_addr = (uintptr_t)d->d_un.d_ptr;
                } else if (d->d_tag == DT_RELASZ) {
                    rela_sz = d->d_un.d_val;
                } else if (d->d_tag == DT_RELAENT) {
                    rela_ent = d->d_un.d_val;
                }
            }

            if (rela_addr != 0 && rela_sz > 0) {
                Elf64_Rela *rela = (Elf64_Rela *)(load_base + rela_addr);
                size_t num_relas = rela_sz / (rela_ent > 0 ? rela_ent : sizeof(Elf64_Rela));
                size_t applied = 0;

                for (size_t r = 0; r < num_relas; r++) {
                    uint32_t type = rela[r].r_info & 0xffffffff;
                    if (type == R_AARCH64_RELATIVE) {
                        uint64_t *target = (uint64_t *)(load_base + rela[r].r_offset);
                        *target = load_base + rela[r].r_addend;
                        applied++;
                    }
                }
                ltm_log_debug("Applied %zu dynamic relocations to '%s'", applied, filepath);
            }
            break;
        }
    }

    /* Apply page protections per 16KB hardware page */
    size_t num_pages = total_span / PAGE_SIZE_16K;
    int *page_prots = calloc(num_pages, sizeof(int));
    if (!page_prots) {
        ltm_log_error("calloc page_prots failed");
        free(phdrs);
        return -1;
    }

    for (int i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type != PT_LOAD) continue;

        int seg_prot = 0;
        if (phdrs[i].p_flags & PF_R) seg_prot |= PROT_READ;
        if (phdrs[i].p_flags & PF_W) seg_prot |= PROT_WRITE;
        if (phdrs[i].p_flags & PF_X) seg_prot |= PROT_EXEC;

        uintptr_t seg_pstart = PAGE_ROUND_DOWN(load_base + phdrs[i].p_vaddr);
        uintptr_t seg_pend = PAGE_ROUND_UP(load_base + phdrs[i].p_vaddr + phdrs[i].p_memsz);

        size_t p_start_idx = (seg_pstart - (load_base + min_page)) / PAGE_SIZE_16K;
        size_t p_end_idx = (seg_pend - (load_base + min_page)) / PAGE_SIZE_16K;

        for (size_t p = p_start_idx; p < p_end_idx && p < num_pages; p++) {
            page_prots[p] |= seg_prot;
        }
    }

    size_t range_start_idx = 0;
    while (range_start_idx < num_pages) {
        int prot = page_prots[range_start_idx];
        if (prot == 0) prot = PROT_READ | PROT_WRITE;

        size_t range_end_idx = range_start_idx + 1;
        while (range_end_idx < num_pages) {
            int next_prot = page_prots[range_end_idx];
            if (next_prot == 0) next_prot = PROT_READ | PROT_WRITE;
            if (next_prot != prot) break;
            range_end_idx++;
        }

        uintptr_t range_addr = (load_base + min_page) + (range_start_idx * PAGE_SIZE_16K);
        size_t range_len = (range_end_idx - range_start_idx) * PAGE_SIZE_16K;

        if (mprotect((void *)range_addr, range_len, prot) != 0) {
            ltm_log_error("mprotect failed for range [0x%lx, 0x%lx), prot=0x%x: %s",
                          (unsigned long)range_addr, (unsigned long)(range_addr + range_len), prot, strerror(errno));
        }

        if (prot & PROT_EXEC) {
            patcher_invalidate_cache((void *)range_addr, range_len);
        }

        range_start_idx = range_end_idx;
    }
    free(page_prots);

    /* Locate program headers for auxv */
    uintptr_t ph_addr = 0;
    for (int i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type == PT_PHDR) {
            ph_addr = load_base + phdrs[i].p_vaddr;
            break;
        }
    }
    if (ph_addr == 0) {
        ph_addr = load_base + min_vaddr + ehdr.e_phoff;
    }

    if (out_base) *out_base = load_base;
    if (out_entry) *out_entry = load_base + ehdr.e_entry;
    if (out_ph_addr) *out_ph_addr = ph_addr;
    if (out_ph_num) *out_ph_num = ehdr.e_phnum;
    if (out_ph_ent) *out_ph_ent = ehdr.e_phentsize;
    if (out_max_vaddr) *out_max_vaddr = load_base + max_vaddr;

    free(phdrs);
    return 0;
}

int elf_load_binary(const char *filepath, ltm_runtime_t *rt) {
    if (!filepath || !rt) return -1;

    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        ltm_log_error("Failed to open binary '%s': %s", filepath, strerror(errno));
        return -1;
    }

    char interp_path[1024] = {0};
    uintptr_t main_base = 0, main_entry = 0, main_ph_addr = 0, main_max_vaddr = 0;
    uint16_t main_ph_num = 0, main_ph_ent = 0;

    int ret = elf_load_image(fd, filepath, 0x400000000ULL,
                             &main_base, &main_entry,
                             &main_ph_addr, &main_ph_num, &main_ph_ent,
                             &main_max_vaddr, interp_path, sizeof(interp_path));
    close(fd);
    if (ret != 0) return -1;

    rt->elf_base = main_base;
    rt->main_entry = main_entry;
    rt->elf_ph_addr = main_ph_addr;
    rt->elf_ph_num = main_ph_num;
    rt->elf_ph_ent = main_ph_ent;
    rt->brk_start = PAGE_ROUND_UP(main_max_vaddr);
    rt->brk_current = rt->brk_start;

    if (interp_path[0] != '\0') {
        ltm_log_debug("Binary requested dynamic interpreter: %s", interp_path);
        char host_interp[1024];
        int sres = sandbox_resolve_path(LINUX_AT_FDCWD, interp_path, host_interp, sizeof(host_interp), 0);
        if (sres < 0) {
            ltm_log_error("Cannot resolve PT_INTERP path '%s' in rootfs: error %d", interp_path, sres);
            return -1;
        }

        int ifd = open(host_interp, O_RDONLY);
        if (ifd < 0) {
            ltm_log_error("Cannot open interpreter '%s' (%s): %s", interp_path, host_interp, strerror(errno));
            return -1;
        }

        uintptr_t interp_base = 0, interp_entry = 0, interp_ph_addr = 0, interp_max_vaddr = 0;
        uint16_t interp_ph_num = 0, interp_ph_ent = 0;

        ret = elf_load_image(ifd, host_interp, 0x500000000ULL,
                             &interp_base, &interp_entry,
                             &interp_ph_addr, &interp_ph_num, &interp_ph_ent,
                             &interp_max_vaddr, NULL, 0);
        close(ifd);
        if (ret != 0) {
            ltm_log_error("Failed to load dynamic interpreter '%s'", host_interp);
            return -1;
        }

        rt->interp_base = interp_base;
        rt->elf_entry = interp_entry;
        ltm_log_debug("Loaded dynamic interpreter at 0x%lx, entry=0x%lx",
                      (unsigned long)interp_base, (unsigned long)interp_entry);
    } else {
        rt->interp_base = 0;
        rt->elf_entry = main_entry;
    }

    return 0;
}

int elf_setup_stack(ltm_runtime_t *rt) {
    if (!rt) return -1;

    size_t stack_size = 8 * 1024 * 1024; /* 8MB */
    void *stack_base = mmap(NULL, stack_size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (stack_base == MAP_FAILED) {
        ltm_log_error("Failed to allocate guest stack: %s", strerror(errno));
        return -1;
    }

    uintptr_t sp = (uintptr_t)stack_base + stack_size;

    /* Push random 16 bytes for AT_RANDOM */
    sp -= 16;
    arc4random_buf((void *)sp, 16);
    uintptr_t random_ptr = sp;

    /* Push platform string */
    sp -= 8;
    memcpy((void *)sp, "aarch64", 8);
    uintptr_t platform_ptr = sp;

    /* Push binary path string for AT_EXECFN */
    size_t fn_len = strlen(rt->binary_path) + 1;
    sp -= (fn_len + 7) & ~7ULL;
    memcpy((void *)sp, rt->binary_path, fn_len);
    uintptr_t execfn_ptr = sp;

    /* Count envp */
    int envc = 0;
    while (rt->guest_envp && rt->guest_envp[envc]) {
        envc++;
    }

    /* Push envp strings */
    uintptr_t *envp_ptrs = malloc((envc + 1) * sizeof(uintptr_t));
    for (int i = envc - 1; i >= 0; i--) {
        size_t len = strlen(rt->guest_envp[i]) + 1;
        sp -= (len + 7) & ~7ULL;
        memcpy((void *)sp, rt->guest_envp[i], len);
        envp_ptrs[i] = sp;
    }
    envp_ptrs[envc] = 0;

    /* Push argv strings */
    int argc = rt->guest_argc;
    uintptr_t *argv_ptrs = malloc((argc + 1) * sizeof(uintptr_t));
    for (int i = argc - 1; i >= 0; i--) {
        const char *arg_str = (rt->guest_argv && rt->guest_argv[i]) ? rt->guest_argv[i] : "";
        size_t len = strlen(arg_str) + 1;
        sp -= (len + 7) & ~7ULL;
        memcpy((void *)sp, arg_str, len);
        argv_ptrs[i] = sp;
    }
    argv_ptrs[argc] = 0;

    /* Auxiliary vector */
    struct {
        uint64_t type;
        uint64_t val;
    } auxv[] = {
        { LINUX_AT_PHDR,     rt->elf_ph_addr },
        { LINUX_AT_PHENT,    rt->elf_ph_ent },
        { LINUX_AT_PHNUM,    rt->elf_ph_num },
        { LINUX_AT_PAGESZ,   PAGE_SIZE_16K },
        { LINUX_AT_BASE,     rt->interp_base },
        { LINUX_AT_FLAGS,    0 },
        { LINUX_AT_ENTRY,    rt->main_entry ? rt->main_entry : rt->elf_entry },
        { LINUX_AT_NOTELF,   0 },
        { LINUX_AT_UID,      0 },
        { LINUX_AT_EUID,     0 },
        { LINUX_AT_GID,      0 },
        { LINUX_AT_EGID,     0 },
        { LINUX_AT_CLKTCK,   100 },
        { LINUX_AT_PLATFORM, platform_ptr },
        { LINUX_AT_BASE_PLATFORM, platform_ptr },
        { LINUX_AT_HWCAP,    LINUX_DEFAULT_HWCAP_ARM64 },
        { LINUX_AT_HWCAP2,   0 },
        { LINUX_AT_RANDOM,   random_ptr },
        { LINUX_AT_SECURE,   0 },
        { LINUX_AT_EXECFN,   execfn_ptr },
        { LINUX_AT_MINSIGSTKSZ, 4096 },
        { LINUX_AT_NULL,     0 }
    };
    int auxv_count = sizeof(auxv) / sizeof(auxv[0]);

    /* Total words to push on stack:
     * argc (1) + argv pointers (argc + 1) + envp pointers (envc + 1) + auxv (auxv_count * 2)
     */
    size_t total_words = 1 + (argc + 1) + (envc + 1) + (auxv_count * 2);

    /* Align sp so that sp is 16-byte aligned */
    sp -= total_words * sizeof(uint64_t);
    sp &= ~0xfULL;

    uint64_t *stack = (uint64_t *)sp;
    size_t idx = 0;

    /* argc */
    stack[idx++] = argc;

    /* argv pointers */
    for (int i = 0; i < argc; i++) {
        stack[idx++] = argv_ptrs[i];
    }
    stack[idx++] = 0; /* NULL terminator */

    /* envp pointers */
    for (int i = 0; i < envc; i++) {
        stack[idx++] = envp_ptrs[i];
    }
    stack[idx++] = 0; /* NULL terminator */

    /* auxv pairs */
    for (int i = 0; i < auxv_count; i++) {
        stack[idx++] = auxv[i].type;
        stack[idx++] = auxv[i].val;
    }

    free(argv_ptrs);
    free(envp_ptrs);

    rt->guest_sp = (void *)sp;
    rt->guest_stack_base = stack_base;
    rt->guest_stack_size = stack_size;

    ltm_log_debug("Guest stack initialized: sp=%p, base=%p", rt->guest_sp, rt->guest_stack_base);
    return 0;
}
