#include "sandbox.h"
#include <mach/mach_host.h>

static int create_mem_fd(const void *data, size_t len) {
    char tmppath[] = "/tmp/ltm_vfs_XXXXXX";
    int fd = mkstemp(tmppath);
    if (fd < 0) {
        return -LINUX_EIO;
    }
    /* Immediately unlink so it will disappear on close or termination */
    unlink(tmppath);
    if (len > 0) {
        ssize_t written = write(fd, data, len);
        if (written < 0 || (size_t)written != len) {
            close(fd);
            return -LINUX_EIO;
        }
        lseek(fd, 0, SEEK_SET);
    }
    return fd;
}

int sandbox_init(const char *rootfs_dir) {
    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) return -1;
    strncpy(g_ltm.host_root, cwd, sizeof(g_ltm.host_root) - 1);
    g_ltm.host_root[sizeof(g_ltm.host_root) - 1] = '\0';

    const char *target_rootfs = rootfs_dir;
    if (!target_rootfs || target_rootfs[0] == '\0') {
        struct stat st;
        if (stat("rootfs", &st) == 0 && S_ISDIR(st.st_mode)) {
            target_rootfs = "rootfs";
        } else {
            return -2; /* Rootfs directory not found */
        }
    }

    struct stat st;
    if (stat(target_rootfs, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return -2;
    }

    if (!realpath(target_rootfs, g_ltm.host_rootfs)) {
        strncpy(g_ltm.host_rootfs, target_rootfs, sizeof(g_ltm.host_rootfs) - 1);
        g_ltm.host_rootfs[sizeof(g_ltm.host_rootfs) - 1] = '\0';
    }

    /* Strip trailing slashes */
    size_t len = strlen(g_ltm.host_rootfs);
    while (len > 1 && g_ltm.host_rootfs[len - 1] == '/') {
        g_ltm.host_rootfs[len - 1] = '\0';
        len--;
    }

    char proc_path[1024];
    snprintf(proc_path, sizeof(proc_path), "%s/proc", g_ltm.host_rootfs);
    mkdir(proc_path, 0755);

    ltm_log_info("Sandbox chroot rootfs initialized: %s (invocation dir: %s)", g_ltm.host_rootfs, g_ltm.host_root);
    return 0;
}

int sandbox_init_standalone(void) {
    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) return -1;
    strncpy(g_ltm.host_root, cwd, sizeof(g_ltm.host_root) - 1);
    g_ltm.host_root[sizeof(g_ltm.host_root) - 1] = '\0';
    strncpy(g_ltm.host_rootfs, cwd, sizeof(g_ltm.host_rootfs) - 1);
    g_ltm.host_rootfs[sizeof(g_ltm.host_rootfs) - 1] = '\0';

    char proc_path[1024];
    snprintf(proc_path, sizeof(proc_path), "%s/proc", g_ltm.host_rootfs);
    mkdir(proc_path, 0755);

    ltm_log_info("Sandbox standalone mode initialized (jail: %s)", g_ltm.host_rootfs);
    return 0;
}

static bool parse_proc_pid_path(const char *path, int *out_pid, const char **out_subfile) {
    if (!path || strncmp(path, "/proc/", 6) != 0) return false;
    const char *p = path + 6;
    if (strncmp(p, "self/", 5) == 0) {
        if (out_pid) *out_pid = getpid();
        if (out_subfile) *out_subfile = p + 5;
        return true;
    }
    if (strcmp(p, "self") == 0) {
        if (out_pid) *out_pid = getpid();
        if (out_subfile) *out_subfile = "";
        return true;
    }
    if (*p < '0' || *p > '9') return false;
    int pid = 0;
    while (*p >= '0' && *p <= '9') {
        pid = pid * 10 + (*p - '0');
        p++;
    }
    if (*p == '\0') {
        if (out_pid) *out_pid = pid;
        if (out_subfile) *out_subfile = "";
        return true;
    }
    if (*p == '/') {
        p++;
        if (out_pid) *out_pid = pid;
        if (out_subfile) *out_subfile = p;
        return true;
    }
    return false;
}

bool sandbox_is_virtual_path(const char *path) {
    if (!path) return false;

    if (strcmp(path, "/proc/cpuinfo") == 0 ||
        strcmp(path, "/proc/version") == 0 ||
        strcmp(path, "/proc/uptime") == 0 ||
        strcmp(path, "/proc/loadavg") == 0 ||
        strcmp(path, "/proc/meminfo") == 0 ||
        strcmp(path, "/proc/mounts") == 0 ||
        strcmp(path, "/proc/stat") == 0 ||
        strcmp(path, "/proc/filesystems") == 0 ||
        strcmp(path, "/proc/sys/kernel/osrelease") == 0 ||
        strcmp(path, "/proc/self") == 0 ||
        strcmp(path, "/sys/devices/system/cpu/online") == 0 ||
        strcmp(path, "/sys/devices/system/cpu/present") == 0 ||
        strcmp(path, "/sys/devices/system/cpu/possible") == 0 ||
        strcmp(path, "/sys/class/dmi/id/product_name") == 0 ||
        strcmp(path, "/sys/class/dmi/id/sys_vendor") == 0 ||
        strcmp(path, "/dev/null") == 0 ||
        strcmp(path, "/dev/zero") == 0 ||
        strcmp(path, "/dev/urandom") == 0 ||
        strcmp(path, "/dev/random") == 0 ||
        strcmp(path, "/dev/tty") == 0 ||
        strcmp(path, "/dev/console") == 0 ||
        strcmp(path, "/dev/ptmx") == 0 ||
        strcmp(path, "/dev/stdin") == 0 ||
        strcmp(path, "/dev/stdout") == 0 ||
        strcmp(path, "/dev/stderr") == 0 ||
        strncmp(path, "/dev/fd/", 8) == 0) {
        return true;
    }

    int pid = 0;
    const char *subfile = NULL;
    if (parse_proc_pid_path(path, &pid, &subfile)) {
        return true;
    }

    return false;
}

ssize_t sandbox_virtual_readlink(const char *path, char *buf, size_t bufsiz) {
    if (!path || !buf) return -LINUX_EFAULT;
    if (bufsiz == 0) return -LINUX_EINVAL;

    if (strcmp(path, "/proc/self") == 0) {
        char pid_str[16];
        int plen = snprintf(pid_str, sizeof(pid_str), "%d", getpid());
        size_t copy_len = (size_t)plen < bufsiz ? (size_t)plen : bufsiz;
        memcpy(buf, pid_str, copy_len);
        return (ssize_t)copy_len;
    }

    if (strcmp(path, "/proc/self/exe") == 0) {
        size_t len = strlen(g_ltm.binary_path);
        size_t copy_len = len < bufsiz ? len : bufsiz;
        memcpy(buf, g_ltm.binary_path, copy_len);
        return (ssize_t)copy_len;
    }

    int pid = 0;
    const char *subfile = NULL;
    if (parse_proc_pid_path(path, &pid, &subfile) && strcmp(subfile, "exe") == 0) {
        const char *target = (pid == 1) ? "/bin/sh" : g_ltm.binary_path;
        size_t len = strlen(target);
        size_t copy_len = len < bufsiz ? len : bufsiz;
        memcpy(buf, target, copy_len);
        return (ssize_t)copy_len;
    }

    return -LINUX_ENOENT;
}

int sandbox_virtual_stat(const char *path, struct stat *st) {
    if (!path || !st) return -LINUX_EFAULT;
    memset(st, 0, sizeof(*st));

    st->st_uid = 0;
    st->st_gid = 0;
    st->st_nlink = 1;
    st->st_blksize = 4096;

    if (strcmp(path, "/proc/self") == 0) {
        st->st_mode = S_IFLNK | 0777;
        st->st_size = 8;
        return 0;
    }

    int pid = 0;
    const char *subfile = NULL;
    if (parse_proc_pid_path(path, &pid, &subfile)) {
        if (subfile == NULL || *subfile == '\0') {
            st->st_mode = S_IFDIR | 0555;
            st->st_nlink = 2;
            return 0;
        }
        if (strcmp(subfile, "exe") == 0) {
            st->st_mode = S_IFLNK | 0777;
            st->st_size = strlen((pid == 1) ? "/bin/sh" : g_ltm.binary_path);
            return 0;
        }
        if (strncmp(subfile, "fd/", 3) == 0) {
            st->st_mode = S_IFLNK | 0777;
            st->st_size = 16;
            return 0;
        }
        st->st_mode = S_IFREG | 0444;
        st->st_size = 1024;
        return 0;
    }

    if (strcmp(path, "/proc/self/exe") == 0) {
        st->st_mode = S_IFLNK | 0777;
        st->st_size = strlen(g_ltm.binary_path);
        return 0;
    }

    if (strncmp(path, "/dev/fd/", 8) == 0 || strncmp(path, "/proc/self/fd/", 14) == 0) {
        st->st_mode = S_IFLNK | 0777;
        st->st_size = 16;
        return 0;
    }

    if (strcmp(path, "/dev/stdin") == 0 || strcmp(path, "/dev/stdout") == 0 || strcmp(path, "/dev/stderr") == 0) {
        st->st_mode = S_IFCHR | 0666;
        st->st_rdev = 0x0103;
        return 0;
    }

    if (strncmp(path, "/dev/", 5) == 0) {
        st->st_mode = S_IFCHR | 0666;
        st->st_rdev = 0x0103; /* standard dummy char dev */
        return 0;
    }

    if (strncmp(path, "/proc", 5) == 0 || strncmp(path, "/sys", 4) == 0) {
        st->st_mode = S_IFREG | 0444;
        st->st_size = 4096;
        return 0;
    }

    return -LINUX_ENOENT;
}

static int open_proc_cpuinfo(void) {
    char buf[16384];
    char brand[256] = "Apple Silicon Processor";
    size_t b_len = sizeof(brand);
    sysctlbyname("machdep.cpu.brand_string", brand, &b_len, NULL, 0);

    int ncpus = 8;
    size_t size = sizeof(ncpus);
    if (sysctlbyname("hw.ncpu", &ncpus, &size, NULL, 0) != 0 || ncpus <= 0) {
        ncpus = 8;
    }

    size_t offset = 0;
    for (int i = 0; i < ncpus && offset < sizeof(buf) - 512; i++) {
        int written = snprintf(buf + offset, sizeof(buf) - offset,
            "processor\t: %d\n"
            "BogoMIPS\t: 48.00\n"
            "Features\t: fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp cpuid asimdrdm jscvt fcma lrcpc dcpop sha3 sm3 sm4 asimddp sha512 sve asimdfhm dit uscat ilrcpc flagm ssbs sb paca pacg dcpodp flagm2 frint\n"
            "CPU implementer\t: 0x61\n"
            "CPU architecture: 8\n"
            "CPU variant\t: 0x0\n"
            "CPU part\t: 0x000\n"
            "CPU revision\t: 1\n"
            "model name\t: %s\n\n",
            i, brand);
        if (written > 0) offset += written;
    }

    return create_mem_fd(buf, offset);
}

static int open_proc_version(void) {
    const char *ver = LTM_KERNEL_FULL_VERSION;
    return create_mem_fd(ver, strlen(ver));
}

static int open_proc_uptime(void) {
    struct timeval boottime;
    size_t btsize = sizeof(boottime);
    double uptime = 3600.0;
    if (sysctlbyname("kern.boottime", &boottime, &btsize, NULL, 0) == 0) {
        struct timeval now;
        gettimeofday(&now, NULL);
        uptime = (double)(now.tv_sec - boottime.tv_sec) + (double)(now.tv_usec - boottime.tv_usec) / 1000000.0;
    }
    char buf[128];
    int len = snprintf(buf, sizeof(buf), "%.2f %.2f\n", uptime, uptime * 0.7);
    if (len < 0) return -LINUX_EIO;
    return create_mem_fd(buf, len);
}

static int open_proc_loadavg(void) {
    struct loadavg load;
    size_t lsize = sizeof(load);
    double a1 = 0.0, a5 = 0.0, a15 = 0.0;
    if (sysctlbyname("vm.loadavg", &load, &lsize, NULL, 0) == 0) {
        a1 = (double)load.ldavg[0] / (double)load.fscale;
        a5 = (double)load.ldavg[1] / (double)load.fscale;
        a15 = (double)load.ldavg[2] / (double)load.fscale;
    }
    char buf[128];
    int len = snprintf(buf, sizeof(buf), "%.2f %.2f %.2f 1/128 %d\n", a1, a5, a15, getpid());
    if (len < 0) return -LINUX_EIO;
    return create_mem_fd(buf, len);
}

static int open_sys_cpu_range(void) {
    int ncpus = 8;
    size_t size = sizeof(ncpus);
    if (sysctlbyname("hw.ncpu", &ncpus, &size, NULL, 0) != 0 || ncpus <= 0) {
        ncpus = 8;
    }
    char buf[64];
    int len = snprintf(buf, sizeof(buf), "0-%d\n", ncpus - 1);
    if (len < 0) return -LINUX_EIO;
    return create_mem_fd(buf, len);
}

static int open_sys_product_name(void) {
    char model[128] = "Mac";
    size_t size = sizeof(model);
    sysctlbyname("hw.model", model, &size, NULL, 0);
    char buf[256];
    int len = snprintf(buf, sizeof(buf), "%s\n", model);
    if (len < 0) return -LINUX_EIO;
    return create_mem_fd(buf, len);
}

static int open_proc_cmdline(void) {
    char buf[4096];
    size_t offset = 0;
    for (int i = 0; i < g_ltm.guest_argc && offset < sizeof(buf) - 1; i++) {
        const char *arg = g_ltm.guest_argv[i];
        size_t len = strlen(arg) + 1;
        if (offset + len >= sizeof(buf)) break;
        memcpy(buf + offset, arg, len);
        offset += len;
    }
    return create_mem_fd(buf, offset);
}

static int open_proc_meminfo(void) {
    vm_size_t page_size = 16384;
    mach_port_t host_port = mach_host_self();
    host_page_size(host_port, &page_size);

    vm_statistics64_data_t vm_stat;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    uint64_t memsize = 0;
    size_t size = sizeof(memsize);
    sysctlbyname("hw.memsize", &memsize, &size, NULL, 0);
    if (memsize == 0) memsize = 16ULL * 1024 * 1024 * 1024;

    uint64_t mem_total_kb = memsize / 1024;
    uint64_t free_kb = mem_total_kb / 4;
    uint64_t active_kb = mem_total_kb / 2;
    uint64_t inactive_kb = mem_total_kb / 4;
    uint64_t cached_kb = 0;

    if (host_statistics64(host_port, HOST_VM_INFO64, (host_info64_t)&vm_stat, &count) == KERN_SUCCESS) {
        free_kb = ((uint64_t)vm_stat.free_count * page_size) / 1024;
        active_kb = ((uint64_t)vm_stat.active_count * page_size) / 1024;
        inactive_kb = ((uint64_t)vm_stat.inactive_count * page_size) / 1024;
        cached_kb = ((uint64_t)(vm_stat.purgeable_count + vm_stat.external_page_count) * page_size) / 1024;
    }

    uint64_t avail_kb = free_kb + inactive_kb;

    struct xsw_usage swap;
    size_t swap_size = sizeof(swap);
    uint64_t swap_total_kb = 0;
    uint64_t swap_free_kb = 0;
    if (sysctlbyname("vm.swapusage", &swap, &swap_size, NULL, 0) == 0) {
        swap_total_kb = swap.xsu_total / 1024;
        swap_free_kb = swap.xsu_avail / 1024;
    }

    char buf[1024];
    int len = snprintf(buf, sizeof(buf),
        "MemTotal:       %llu kB\n"
        "MemFree:        %llu kB\n"
        "MemAvailable:   %llu kB\n"
        "Buffers:               0 kB\n"
        "Cached:          %llu kB\n"
        "Active:          %llu kB\n"
        "Inactive:        %llu kB\n"
        "SwapTotal:      %llu kB\n"
        "SwapFree:       %llu kB\n",
        (unsigned long long)mem_total_kb,
        (unsigned long long)free_kb,
        (unsigned long long)avail_kb,
        (unsigned long long)(cached_kb > 0 ? cached_kb : inactive_kb),
        (unsigned long long)active_kb,
        (unsigned long long)inactive_kb,
        (unsigned long long)swap_total_kb,
        (unsigned long long)swap_free_kb);
    if (len < 0) return -LINUX_EIO;
    return create_mem_fd(buf, len);
}

static int open_proc_maps(void) {
    char buf[4096];
    size_t offset = 0;

    int written = snprintf(buf + offset, sizeof(buf) - offset,
        "%016lx-%016lx r-xp 00000000 00:00 0 %s\n"
        "%016lx-%016lx rw-p 00000000 00:00 0 [heap]\n"
        "%016lx-%016lx rw-p 00000000 00:00 0 [stack]\n",
        (unsigned long)g_ltm.elf_base,
        (unsigned long)(g_ltm.elf_base + 0x10000),
        g_ltm.binary_path,
        (unsigned long)g_ltm.brk_start,
        (unsigned long)(g_ltm.brk_current > g_ltm.brk_start ? g_ltm.brk_current : g_ltm.brk_start + 0x10000),
        (unsigned long)g_ltm.guest_stack_base,
        (unsigned long)((char *)g_ltm.guest_stack_base + g_ltm.guest_stack_size));
    if (written > 0) offset += written;

    return create_mem_fd(buf, offset);
}

static int open_proc_mounts(void) {
    const char *mounts = 
        "rootfs / rootfs rw 0 0\n"
        "proc /proc proc rw,nosuid,nodev,noexec,relatime 0 0\n"
        "sysfs /sys sysfs rw,nosuid,nodev,noexec,relatime 0 0\n"
        "devtmpfs /dev devtmpfs rw,nosuid,size=4096k,nr_inodes=100000,mode=755 0 0\n"
        "devpts /dev/pts devpts rw,nosuid,noexec,relatime,mode=600,ptmxmode=000 0 0\n"
        "shm /dev/shm tmpfs rw,nosuid,nodev,noexec,relatime 0 0\n";
    return create_mem_fd(mounts, strlen(mounts));
}

static int open_proc_stat(void) {
    char buf[1024];
    int ncpus = 8;
    size_t size = sizeof(ncpus);
    sysctlbyname("hw.ncpu", &ncpus, &size, NULL, 0);
    if (ncpus <= 0) ncpus = 8;

    struct timeval boottime;
    size_t btsize = sizeof(boottime);
    long btime_sec = 1700000000;
    if (sysctlbyname("kern.boottime", &boottime, &btsize, NULL, 0) == 0 && boottime.tv_sec > 0) {
        btime_sec = (long)boottime.tv_sec;
    }

    int len = snprintf(buf, sizeof(buf),
        "cpu  10000 0 5000 500000 100 0 50 0 0 0\n");
    for (int i = 0; i < ncpus && len < (int)sizeof(buf) - 64; i++) {
        len += snprintf(buf + len, sizeof(buf) - len,
            "cpu%d 1250 0 625 62500 12 0 6 0 0 0\n", i);
    }
    len += snprintf(buf + len, sizeof(buf) - len,
        "intr 1000000\n"
        "ctxt 2000000\n"
        "btime %ld\n"
        "processes 5000\n"
        "procs_running 1\n"
        "procs_blocked 0\n",
        btime_sec);
    return create_mem_fd(buf, len);
}

static int open_proc_filesystems(void) {
    const char *fs = 
        "nodev\tsysfs\n"
        "nodev\trootfs\n"
        "nodev\tramfs\n"
        "nodev\tbdev\n"
        "nodev\tproc\n"
        "nodev\tcpuset\n"
        "nodev\tcgroup\n"
        "nodev\ttmpfs\n"
        "nodev\tdevpts\n"
        "\text4\n"
        "\tvfat\n";
    return create_mem_fd(fs, strlen(fs));
}

static int open_proc_pid_file(int pid, const char *subfile) {
    const char *slash = strrchr(g_ltm.binary_path, '/');
    const char *guest_comm = slash ? slash + 1 : g_ltm.binary_path;
    if (*guest_comm == '\0') guest_comm = "guest";

    const char *comm = (pid == 1) ? "sh" : guest_comm;
    char state = (pid == 1) ? 'S' : 'R';
    const char *state_str = (pid == 1) ? "S (sleeping)" : "R (running)";
    int ppid = (pid == 1) ? 0 : (getppid() > 0 ? getppid() : 1);

    if (strcmp(subfile, "stat") == 0) {
        char buf[512];
        int len = snprintf(buf, sizeof(buf),
            "%d (%s) %c %d 1 1 0 0 0 0 0 0 0 0 0 20 0 1 0 100 10000000 1000 18446744073709551615 0 0 0 0 0 0 0 0 0 0 0 0 17 0\n",
            pid, comm, state, ppid);
        return create_mem_fd(buf, len);
    }

    if (strcmp(subfile, "status") == 0) {
        char buf[1024];
        int len = snprintf(buf, sizeof(buf),
            "Name:\t%s\n"
            "State:\t%s\n"
            "Tgid:\t%d\n"
            "Pid:\t%d\n"
            "PPid:\t%d\n"
            "Uid:\t0\t0\t0\t0\n"
            "Gid:\t0\t0\t0\t0\n"
            "FDSize:\t256\n"
            "Threads:\t1\n",
            comm, state_str, pid, pid, ppid);
        return create_mem_fd(buf, len);
    }

    if (strcmp(subfile, "comm") == 0) {
        char buf[64];
        int len = snprintf(buf, sizeof(buf), "%s\n", comm);
        return create_mem_fd(buf, len);
    }

    if (strcmp(subfile, "cmdline") == 0) {
        if (pid == 1) {
            const char *cmd = "/bin/sh";
            return create_mem_fd(cmd, strlen(cmd) + 1);
        } else {
            return open_proc_cmdline();
        }
    }

    if (strcmp(subfile, "maps") == 0) {
        return open_proc_maps();
    }

    if (strcmp(subfile, "exe") == 0) {
        if (pid == 1) {
            char sh_path[1024];
            snprintf(sh_path, sizeof(sh_path), "%s/bin/sh", g_ltm.host_rootfs);
            int fd = open(sh_path, O_RDONLY);
            if (fd < 0) fd = open("/bin/sh", O_RDONLY);
            return fd >= 0 ? fd : -LINUX_ENOENT;
        } else {
            int fd = open(g_ltm.binary_path, O_RDONLY);
            return fd >= 0 ? fd : -LINUX_ENOENT;
        }
    }

    if (strcmp(subfile, "mounts") == 0) {
        return open_proc_mounts();
    }

    return -LINUX_ENOENT;
}

int sandbox_open_virtual(const char *path, int flags, mode_t mode) {
    (void)flags;
    (void)mode;
    if (!path) return -LINUX_EFAULT;

    if (strcmp(path, "/dev/null") == 0) return open("/dev/null", flags, mode);
    if (strcmp(path, "/dev/zero") == 0) return open("/dev/zero", flags, mode);
    if (strcmp(path, "/dev/urandom") == 0) return open("/dev/urandom", flags, mode);
    if (strcmp(path, "/dev/random") == 0) return open("/dev/random", flags, mode);
    if (strcmp(path, "/dev/tty") == 0) return open("/dev/tty", flags, mode);
    if (strcmp(path, "/dev/console") == 0) return open("/dev/console", flags, mode);
    if (strcmp(path, "/dev/ptmx") == 0) return open("/dev/ptmx", flags, mode);
    if (strcmp(path, "/dev/stdin") == 0) return dup(0);
    if (strcmp(path, "/dev/stdout") == 0) return dup(1);
    if (strcmp(path, "/dev/stderr") == 0) return dup(2);
    if (strncmp(path, "/dev/fd/", 8) == 0) {
        int target_fd = atoi(path + 8);
        return dup(target_fd);
    }
    if (strncmp(path, "/proc/self/fd/", 14) == 0) {
        int target_fd = atoi(path + 14);
        return dup(target_fd);
    }

    int pid = 0;
    const char *subfile = NULL;
    if (parse_proc_pid_path(path, &pid, &subfile)) {
        if (strncmp(subfile, "fd/", 3) == 0) {
            int target_fd = atoi(subfile + 3);
            return dup(target_fd);
        }
        if (pid != 1 && pid != getpid()) {
            return -LINUX_ENOENT;
        }
        return open_proc_pid_file(pid, subfile);
    }

    if (strcmp(path, "/proc/cpuinfo") == 0) return open_proc_cpuinfo();
    if (strcmp(path, "/proc/version") == 0) return open_proc_version();
    if (strcmp(path, "/proc/uptime") == 0) return open_proc_uptime();
    if (strcmp(path, "/proc/loadavg") == 0) return open_proc_loadavg();
    if (strcmp(path, "/proc/meminfo") == 0) return open_proc_meminfo();
    if (strcmp(path, "/proc/mounts") == 0 || strcmp(path, "/proc/self/mounts") == 0) return open_proc_mounts();
    if (strcmp(path, "/proc/stat") == 0) return open_proc_stat();
    if (strcmp(path, "/proc/filesystems") == 0) return open_proc_filesystems();
    if (strcmp(path, "/proc/sys/kernel/osrelease") == 0) {
        const char *rel = LTM_KERNEL_VERSION_NAME "\n";
        return create_mem_fd(rel, strlen(rel));
    }
    if (strcmp(path, "/proc/self/cmdline") == 0) return open_proc_cmdline();
    if (strcmp(path, "/proc/self/maps") == 0) return open_proc_maps();
    if (strcmp(path, "/proc/self/exe") == 0) {
        return open(g_ltm.binary_path, O_RDONLY);
    }

    if (strcmp(path, "/sys/devices/system/cpu/online") == 0 ||
        strcmp(path, "/sys/devices/system/cpu/present") == 0 ||
        strcmp(path, "/sys/devices/system/cpu/possible") == 0) {
        return open_sys_cpu_range();
    }
    if (strcmp(path, "/sys/class/dmi/id/product_name") == 0) {
        return open_sys_product_name();
    }
    if (strcmp(path, "/sys/class/dmi/id/sys_vendor") == 0) {
        const char *vendor = "Apple Inc.\n";
        return create_mem_fd(vendor, strlen(vendor));
    }

    return -LINUX_ENOENT;
}

static int sandbox_resolve_path_lexical(int dirfd, const char *guest_path, char *host_path_buf, size_t buf_sz) {
    if (!guest_path) {
        return -LINUX_EFAULT;
    }
    if (!host_path_buf || buf_sz == 0) {
        return -LINUX_EINVAL;
    }

    /* Virtual paths handled separately */
    if (sandbox_is_virtual_path(guest_path)) {
        return 1;
    }

    const char *src = guest_path;

    /* Handle home and root virtual aliases if not present in rootfs */
    char test_target[1024];
    if (strncmp(src, "/home/user", 10) == 0) {
        snprintf(test_target, sizeof(test_target), "%s/home/user", g_ltm.host_rootfs);
        if (access(test_target, F_OK) != 0) {
            /* If /home/user doesn't exist in rootfs, alias to rootfs root */
            src += 10;
            if (*src == '\0') src = "/";
        }
    } else if (strncmp(src, "/root", 5) == 0) {
        snprintf(test_target, sizeof(test_target), "%s/root", g_ltm.host_rootfs);
        if (access(test_target, F_OK) != 0) {
            src += 5;
            if (*src == '\0') src = "/";
        }
    }

    /* Canonicalize path components */
    char full_virtual[1024];
    if (src[0] == '/') {
        /* Absolute virtual path: rooted at chroot rootfs */
        snprintf(full_virtual, sizeof(full_virtual), "%s", src);
    } else {
        /* Relative path */
        if (dirfd == LINUX_AT_FDCWD || dirfd == AT_FDCWD) {
            char host_cwd[1024];
            if (getcwd(host_cwd, sizeof(host_cwd))) {
                size_t root_len = strlen(g_ltm.host_rootfs);
                if (strncmp(host_cwd, g_ltm.host_rootfs, root_len) == 0) {
                    const char *rel_base = host_cwd + root_len;
                    if (*rel_base == '\0') rel_base = "/";
                    snprintf(full_virtual, sizeof(full_virtual), "%s/%s", rel_base, src);
                } else {
                    snprintf(full_virtual, sizeof(full_virtual), "/%s", src);
                }
            } else {
                snprintf(full_virtual, sizeof(full_virtual), "/%s", src);
            }
        } else {
            char dir_path[1024];
            if (fcntl(dirfd, F_GETPATH, dir_path) == 0) {
                if (strncmp(dir_path, g_ltm.host_rootfs, strlen(g_ltm.host_rootfs)) == 0) {
                    const char *rel_base = dir_path + strlen(g_ltm.host_rootfs);
                    if (*rel_base == '\0') rel_base = "/";
                    snprintf(full_virtual, sizeof(full_virtual), "%s/%s", rel_base, src);
                } else {
                    return -LINUX_EACCES;
                }
            } else {
                return -LINUX_EBADF;
            }
        }
    }

    /* Lexically normalize path components (resolving . and ..) */
    char *components[64];
    int depth = 0;
    char temp[1024];
    strncpy(temp, full_virtual, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    char *token = strtok(temp, "/");
    while (token != NULL) {
        if (strcmp(token, ".") == 0) {
            /* ignore */
        } else if (strcmp(token, "..") == 0) {
            if (depth > 0) {
                depth--;
            } else if (strcmp(src, "..") == 0 || strcmp(src, "/..") == 0 ||
                       strcmp(src, "/../") == 0 || strcmp(src, "./..") == 0 ||
                       strcmp(src, "../") == 0 || strcmp(src, ".") == 0) {
                /* In POSIX, '..' at rootfs root refers to root itself (e.g. ls -la /) */
                depth = 0;
            } else {
                /* Attempted directory traversal escape above chroot rootfs */
                ltm_log_debug("Sandbox BLOCKED directory traversal escape: %s", guest_path);
                return -LINUX_EACCES;
            }
        } else {
            if (depth < 63) {
                components[depth++] = token;
            }
        }
        token = strtok(NULL, "/");
    }

    char norm_guest[1024];
    norm_guest[0] = '\0';
    size_t norm_len = 0;
    for (int i = 0; i < depth; i++) {
        norm_len += snprintf(norm_guest + norm_len, sizeof(norm_guest) - norm_len, "/%s", components[i]);
    }
    if (norm_len == 0) {
        strcpy(norm_guest, "");
    }

    /* Construct host path within chroot rootfs */
    int written = snprintf(host_path_buf, buf_sz, "%s%s", g_ltm.host_rootfs, norm_guest);
    if (written < 0 || (size_t)written >= buf_sz) {
        return -LINUX_ENAMETOOLONG;
    }

    /* Verify existing path does not symlink escape the chroot rootfs */
    char resolved_real[1024];
    if (realpath(host_path_buf, resolved_real)) {
        size_t root_len = strlen(g_ltm.host_rootfs);
        if (strncmp(resolved_real, g_ltm.host_rootfs, root_len) != 0 ||
            (resolved_real[root_len] != '/' && resolved_real[root_len] != '\0')) {
            ltm_log_debug("Sandbox BLOCKED symlink escape: %s -> %s", host_path_buf, resolved_real);
            return -LINUX_EACCES;
        }
    }

    return 0;
}

int sandbox_resolve_path(int dirfd, const char *guest_path, char *host_path_buf, size_t buf_sz, int flags) {
    if (!guest_path) return -LINUX_EFAULT;
    if (!host_path_buf || buf_sz == 0) return -LINUX_EINVAL;
    if (sandbox_is_virtual_path(guest_path)) {
        strncpy(host_path_buf, guest_path, buf_sz - 1);
        host_path_buf[buf_sz - 1] = '\0';
        return 1;
    }

    char cur_path[1024];
    strncpy(cur_path, guest_path, sizeof(cur_path) - 1);
    cur_path[sizeof(cur_path) - 1] = '\0';

    int cur_dirfd = dirfd;
    int loop_count = 0;

    while (loop_count++ < 32) {
        int r = sandbox_resolve_path_lexical(cur_dirfd, cur_path, host_path_buf, buf_sz);
        if (r != 0) return r;

        /* Check if the resolved host path corresponds to a virtual path inside rootfs */
        size_t root_len = strlen(g_ltm.host_rootfs);
        if (strncmp(host_path_buf, g_ltm.host_rootfs, root_len) == 0) {
            const char *virt_cand = host_path_buf + root_len;
            if (sandbox_is_virtual_path(virt_cand)) {
                memmove(host_path_buf, virt_cand, strlen(virt_cand) + 1);
                return 1;
            }
        }

        /* If caller requested NOFOLLOW on final target, stop here */
        if (flags & (AT_SYMLINK_NOFOLLOW | LINUX_O_NOFOLLOW)) {
            return 0;
        }

        struct stat st;
        if (lstat(host_path_buf, &st) != 0) {
            /* File doesn't exist yet (e.g. O_CREAT) */
            return 0;
        }

        if (!S_ISLNK(st.st_mode)) {
            /* Target is a regular file/dir, fully resolved! */
            return 0;
        }

        /* Target is a symlink: read target and resolve inside rootfs container */
        char link_target[1024];
        ssize_t llen = readlink(host_path_buf, link_target, sizeof(link_target) - 1);
        if (llen < 0) {
            return darwin_to_linux_errno(errno);
        }
        link_target[llen] = '\0';

        if (link_target[0] == '/') {
            /* Absolute symlink: resolve relative to container rootfs */
            strncpy(cur_path, link_target, sizeof(cur_path) - 1);
            cur_path[sizeof(cur_path) - 1] = '\0';
            cur_dirfd = AT_FDCWD;
        } else {
            /* Relative symlink: resolve relative to directory of cur_path */
            char dir_part[1024];
            strncpy(dir_part, cur_path, sizeof(dir_part) - 1);
            dir_part[sizeof(dir_part) - 1] = '\0';
            char *last_slash = strrchr(dir_part, '/');
            if (last_slash) {
                *last_slash = '\0';
                snprintf(cur_path, sizeof(cur_path), "%s/%s", dir_part, link_target);
            } else {
                strncpy(cur_path, link_target, sizeof(cur_path) - 1);
                cur_path[sizeof(cur_path) - 1] = '\0';
            }
        }
    }

    return -LINUX_ELOOP;
}
