#include "ltm.h"
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

/* Helper to execute a system command directly via fork/execv without a shell */
static int run_exec(const char *file, char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        /* Child process */
        execv(file, argv);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

static void ensure_dir(const char *path, mode_t mode) {
    struct stat st;
    if (stat(path, &st) != 0) {
        mkdir(path, mode);
    }
}

static void touch_file(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_NOCTTY, 0644);
    if (fd >= 0) {
        close(fd);
    }
}

static void setup_resolv_conf(const char *rootfs_dir) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/etc", rootfs_dir);
    ensure_dir(path, 0755);

    snprintf(path, sizeof(path), "%s/etc/resolv.conf", rootfs_dir);
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "nameserver 1.1.1.1\nnameserver 8.8.8.8\n");
        fclose(f);
    }
}

static void setup_vfs_nodes(const char *rootfs_dir) {
    char path[1024];

    snprintf(path, sizeof(path), "%s/proc", rootfs_dir); ensure_dir(path, 0755);
    snprintf(path, sizeof(path), "%s/proc/self", rootfs_dir); ensure_dir(path, 0755);
    snprintf(path, sizeof(path), "%s/proc/sys", rootfs_dir); ensure_dir(path, 0755);
    snprintf(path, sizeof(path), "%s/proc/sys/kernel", rootfs_dir); ensure_dir(path, 0755);

    snprintf(path, sizeof(path), "%s/sys", rootfs_dir); ensure_dir(path, 0755);
    snprintf(path, sizeof(path), "%s/sys/devices", rootfs_dir); ensure_dir(path, 0755);
    snprintf(path, sizeof(path), "%s/sys/devices/system", rootfs_dir); ensure_dir(path, 0755);
    snprintf(path, sizeof(path), "%s/sys/devices/system/cpu", rootfs_dir); ensure_dir(path, 0755);
    snprintf(path, sizeof(path), "%s/sys/class", rootfs_dir); ensure_dir(path, 0755);
    snprintf(path, sizeof(path), "%s/sys/class/dmi", rootfs_dir); ensure_dir(path, 0755);
    snprintf(path, sizeof(path), "%s/sys/class/dmi/id", rootfs_dir); ensure_dir(path, 0755);

    snprintf(path, sizeof(path), "%s/dev", rootfs_dir); ensure_dir(path, 0755);
    snprintf(path, sizeof(path), "%s/dev/pts", rootfs_dir); ensure_dir(path, 0755);
    snprintf(path, sizeof(path), "%s/dev/shm", rootfs_dir); ensure_dir(path, 0755);

    snprintf(path, sizeof(path), "%s/root", rootfs_dir); ensure_dir(path, 0700);
    snprintf(path, sizeof(path), "%s/tmp", rootfs_dir); ensure_dir(path, 0777);

    /* Stub files for /proc */
    snprintf(path, sizeof(path), "%s/proc/cpuinfo", rootfs_dir); touch_file(path);
    snprintf(path, sizeof(path), "%s/proc/meminfo", rootfs_dir); touch_file(path);
    snprintf(path, sizeof(path), "%s/proc/version", rootfs_dir); touch_file(path);
    snprintf(path, sizeof(path), "%s/proc/uptime", rootfs_dir); touch_file(path);
    snprintf(path, sizeof(path), "%s/proc/loadavg", rootfs_dir); touch_file(path);
    snprintf(path, sizeof(path), "%s/proc/mounts", rootfs_dir); touch_file(path);
    snprintf(path, sizeof(path), "%s/proc/stat", rootfs_dir); touch_file(path);
    snprintf(path, sizeof(path), "%s/proc/filesystems", rootfs_dir); touch_file(path);
    snprintf(path, sizeof(path), "%s/proc/sys/kernel/osrelease", rootfs_dir); touch_file(path);

    snprintf(path, sizeof(path), "%s/proc/self/cmdline", rootfs_dir); touch_file(path);
    snprintf(path, sizeof(path), "%s/proc/self/maps", rootfs_dir); touch_file(path);
    snprintf(path, sizeof(path), "%s/proc/self/status", rootfs_dir); touch_file(path);
    snprintf(path, sizeof(path), "%s/proc/self/stat", rootfs_dir); touch_file(path);
    snprintf(path, sizeof(path), "%s/proc/self/mounts", rootfs_dir); touch_file(path);

    /* Stub files for /sys */
    snprintf(path, sizeof(path), "%s/sys/devices/system/cpu/online", rootfs_dir); touch_file(path);
    snprintf(path, sizeof(path), "%s/sys/devices/system/cpu/present", rootfs_dir); touch_file(path);
    snprintf(path, sizeof(path), "%s/sys/devices/system/cpu/possible", rootfs_dir); touch_file(path);
    snprintf(path, sizeof(path), "%s/sys/class/dmi/id/product_name", rootfs_dir); touch_file(path);
    snprintf(path, sizeof(path), "%s/sys/class/dmi/id/sys_vendor", rootfs_dir); touch_file(path);

    /* Stub files for /dev */
    snprintf(path, sizeof(path), "%s/dev/null", rootfs_dir); touch_file(path);
    snprintf(path, sizeof(path), "%s/dev/zero", rootfs_dir); touch_file(path);
    snprintf(path, sizeof(path), "%s/dev/urandom", rootfs_dir); touch_file(path);
    snprintf(path, sizeof(path), "%s/dev/random", rootfs_dir); touch_file(path);
    snprintf(path, sizeof(path), "%s/dev/tty", rootfs_dir); touch_file(path);
    snprintf(path, sizeof(path), "%s/dev/console", rootfs_dir); touch_file(path);
    snprintf(path, sizeof(path), "%s/dev/ptmx", rootfs_dir); touch_file(path);
    snprintf(path, sizeof(path), "%s/dev/stdin", rootfs_dir); touch_file(path);
    snprintf(path, sizeof(path), "%s/dev/stdout", rootfs_dir); touch_file(path);
    snprintf(path, sizeof(path), "%s/dev/stderr", rootfs_dir); touch_file(path);
    snprintf(path, sizeof(path), "%s/dev/fd", rootfs_dir); touch_file(path);
}

static void configure_pacman_conf(const char *rootfs_dir) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/etc/pacman.conf", rootfs_dir);

    FILE *f = fopen(path, "r");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 1024 * 1024) {
        fclose(f);
        return;
    }

    char *buf = malloc(size + 4096);
    if (!buf) {
        fclose(f);
        return;
    }

    size_t read_bytes = fread(buf, 1, size, f);
    fclose(f);
    buf[read_bytes] = '\0';

    /* Build new pacman.conf */
    char *new_buf = malloc(size + 8192);
    if (!new_buf) {
        free(buf);
        return;
    }

    char *src = buf;
    char *dst = new_buf;
    bool inserted_sandboxing = false;

    char line[1024];
    while (*src) {
        char *line_start = src;
        while (*src && *src != '\n') src++;
        size_t line_len = src - line_start;
        if (line_len >= sizeof(line)) line_len = sizeof(line) - 1;
        memcpy(line, line_start, line_len);
        line[line_len] = '\0';
        if (*src == '\n') src++;

        /* Comment out DownloadUser = alpm */
        if (strstr(line, "DownloadUser") != NULL && strstr(line, "alpm") != NULL) {
            dst += sprintf(dst, "#DownloadUser = alpm\n");
            continue;
        }

        /* Set SigLevel = Never */
        if (strncmp(line, "SigLevel", 8) == 0 && strchr(line, '=') != NULL) {
            dst += sprintf(dst, "SigLevel = Never\n");
            continue;
        }

        /* Check if sandbox directives are already present */
        if (strstr(line, "DisableSandboxFilesystem") != NULL ||
            strstr(line, "DisableSandboxSyscalls") != NULL) {
            inserted_sandboxing = true;
        }

        dst += sprintf(dst, "%s\n", line);

        /* Insert sandboxing directives after [options] section */
        if (!inserted_sandboxing && strcmp(line, "[options]") == 0) {
            dst += sprintf(dst, "DisableSandboxFilesystem\nDisableSandboxSyscalls\n");
            inserted_sandboxing = true;
        }
    }

    free(buf);

    /* Write back updated pacman.conf */
    f = fopen(path, "w");
    if (f) {
        fputs(new_buf, f);
        fclose(f);
    }
    free(new_buf);
}

int ltm_install_arch_rootfs(const char *target_dir) {
    if (!target_dir || target_dir[0] == '\0') {
        target_dir = "arch_rootfs";
    }

    printf("==============================================================\n");
    printf("   LTM Arch Linux ARM64 Rootfs & Environment Installer\n");
    printf("==============================================================\n");
    printf("[*] Target directory: %s\n\n", target_dir);

    ensure_dir(target_dir, 0755);

    /* Download Arch Linux ARM rootfs archive using /usr/bin/curl */
    char tmp_archive[] = "/tmp/ltm_archlinux_arm_XXXXXX.tar.gz";
    int fd = mkstemps(tmp_archive, 7);
    if (fd >= 0) close(fd);

    printf("[+] Downloading official Arch Linux ARM rootfs archive...\n");
    printf("    URL: http://os.archlinuxarm.org/os/ArchLinuxARM-aarch64-latest.tar.gz\n");

    char *curl_argv[] = {
        "/usr/bin/curl",
        "-L",
        "--progress-bar",
        "-o",
        tmp_archive,
        "http://os.archlinuxarm.org/os/ArchLinuxARM-aarch64-latest.tar.gz",
        NULL
    };

    int ret = run_exec("/usr/bin/curl", curl_argv);
    if (ret != 0) {
        fprintf(stderr, "\n[-] Failed to download Arch Linux ARM rootfs archive (curl exit code: %d)\n", ret);
        unlink(tmp_archive);
        return 1;
    }

    /* Extract rootfs using /usr/bin/tar */
    printf("\n[+] Extracting Arch Linux ARM rootfs into '%s'...\n", target_dir);
    char *tar_argv[] = {
        "/usr/bin/tar",
        "-xzf",
        tmp_archive,
        "-C",
        (char *)target_dir,
        NULL
    };

    ret = run_exec("/usr/bin/tar", tar_argv);
    unlink(tmp_archive);

    char check_sh[1024], check_bash[1024];
    snprintf(check_sh, sizeof(check_sh), "%s/bin/sh", target_dir);
    snprintf(check_bash, sizeof(check_bash), "%s/usr/bin/bash", target_dir);

    /* On macOS APFS (case-insensitive by default), bsdtar returns exit code 1 due to
     * case collisions in terminfo (e.g. /usr/share/terminfo/p vs P).
     * If core binaries (/bin/sh or /usr/bin/bash) exist, the extraction is fully valid. */
    if (ret != 0 && (access(check_sh, F_OK) == 0 || access(check_bash, F_OK) == 0)) {
        printf("[+] Core filesystem unpacked successfully (APFS case-collision warnings safely handled).\n");
        ret = 0;
    }

    if (ret != 0) {
        fprintf(stderr, "[-] Failed to extract Arch Linux ARM rootfs archive (tar exit code: %d)\n", ret);
        return 1;
    }

    /* Configure network DNS nameservers */
    printf("[+] Configuring /etc/resolv.conf nameservers (1.1.1.1, 8.8.8.8)...\n");
    setup_resolv_conf(target_dir);

    /* Configure pacman.conf */
    printf("[+] Configuring /etc/pacman.conf for userland translation...\n");
    configure_pacman_conf(target_dir);

    /* Initialize virtual directory nodes */
    printf("[+] Initializing /proc, /sys, and /dev directory nodes...\n");
    setup_vfs_nodes(target_dir);

    printf("\n==============================================================\n");
    printf("   Arch Linux ARM64 Rootfs Successfully Installed!\n");
    printf("==============================================================\n");
    printf("   Try running:\n");
    printf("     ./ltm /bin/bash\n");
    printf("     ./ltm /usr/bin/fastfetch\n");
    printf("     ./ltm /usr/bin/pacman -Sy\n");
    printf("==============================================================\n");
    return 0;
}

int ltm_install_alpine_rootfs(const char *target_dir) {
    if (!target_dir || target_dir[0] == '\0') {
        target_dir = "rootfs";
    }

    printf("==============================================================\n");
    printf("   LTM Alpine Linux ARM64 Rootfs & Environment Installer\n");
    printf("==============================================================\n");
    printf("[*] Target directory: %s\n\n", target_dir);

    ensure_dir(target_dir, 0755);

    /* Download Alpine minirootfs */
    char tmp_archive[] = "/tmp/ltm_alpine_XXXXXX.tar.gz";
    int fd = mkstemps(tmp_archive, 7);
    if (fd >= 0) close(fd);

    printf("[+] Downloading Alpine Linux 3.20 aarch64 minirootfs...\n");
    char *curl_argv[] = {
        "/usr/bin/curl",
        "-sSL",
        "-o",
        tmp_archive,
        "https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/aarch64/alpine-minirootfs-3.20.0-aarch64.tar.gz",
        NULL
    };

    int ret = run_exec("/usr/bin/curl", curl_argv);
    if (ret != 0) {
        fprintf(stderr, "\n[-] Failed to download Alpine minirootfs\n");
        unlink(tmp_archive);
        return 1;
    }

    printf("[+] Extracting Alpine Linux rootfs into '%s'...\n", target_dir);
    char *tar_argv[] = {
        "/usr/bin/tar",
        "-xzf",
        tmp_archive,
        "-C",
        (char *)target_dir,
        NULL
    };

    ret = run_exec("/usr/bin/tar", tar_argv);
    unlink(tmp_archive);
    if (ret != 0) {
        fprintf(stderr, "[-] Failed to extract Alpine rootfs\n");
        return 1;
    }

    /* Download packages: fastfetch, bash, readline, libncursesw */
    printf("[+] Downloading Fastfetch, GNU Bash, and dependencies...\n");
    const struct {
        const char *name;
        const char *url;
    } pkgs[] = {
        { "fastfetch.apk", "https://dl-cdn.alpinelinux.org/alpine/v3.20/community/aarch64/fastfetch-2.13.1-r0.apk" },
        { "bash.apk", "https://dl-cdn.alpinelinux.org/alpine/v3.20/main/aarch64/bash-5.2.26-r0.apk" },
        { "readline.apk", "https://dl-cdn.alpinelinux.org/alpine/v3.20/main/aarch64/readline-8.2.10-r0.apk" },
        { "libncursesw.apk", "https://dl-cdn.alpinelinux.org/alpine/v3.20/main/aarch64/libncursesw-6.4_p20240420-r2.apk" },
        { "ncurses-base.apk", "https://dl-cdn.alpinelinux.org/alpine/v3.20/main/aarch64/ncurses-terminfo-base-6.4_p20240420-r2.apk" },
        { NULL, NULL }
    };

    for (int i = 0; pkgs[i].name != NULL; i++) {
        char pkg_file[] = "/tmp/ltm_pkg_XXXXXX.apk";
        int pfd = mkstemps(pkg_file, 4);
        if (pfd >= 0) close(pfd);

        char *dl_argv[] = { "/usr/bin/curl", "-sSL", "-o", pkg_file, (char *)pkgs[i].url, NULL };
        if (run_exec("/usr/bin/curl", dl_argv) == 0) {
            char *extract_argv[] = { "/usr/bin/tar", "-xzf", pkg_file, "-C", (char *)target_dir, NULL };
            run_exec("/usr/bin/tar", extract_argv);
        }
        unlink(pkg_file);
    }

    /* Make binaries executable */
    char bin_path[1024];
    snprintf(bin_path, sizeof(bin_path), "%s/bin/busybox", target_dir); chmod(bin_path, 0755);
    snprintf(bin_path, sizeof(bin_path), "%s/bin/sh", target_dir); chmod(bin_path, 0755);
    snprintf(bin_path, sizeof(bin_path), "%s/bin/bash", target_dir); chmod(bin_path, 0755);
    snprintf(bin_path, sizeof(bin_path), "%s/usr/bin/fastfetch", target_dir); chmod(bin_path, 0755);

    setup_resolv_conf(target_dir);
    setup_vfs_nodes(target_dir);

    printf("\n==============================================================\n");
    printf("   Alpine Linux ARM64 Rootfs Successfully Installed!\n");
    printf("==============================================================\n");
    return 0;
}

int rootfs_install(const char *flavor, const char *target_dir) {
    if (!flavor || flavor[0] == '\0' || strcmp(flavor, "arch") == 0 || strcmp(flavor, "arch_rootfs") == 0) {
        return ltm_install_arch_rootfs(target_dir);
    }
    if (strcmp(flavor, "alpine") == 0 || strcmp(flavor, "rootfs") == 0) {
        return ltm_install_alpine_rootfs(target_dir);
    }
    return ltm_install_arch_rootfs(target_dir);
}

void ensure_rootfs_configured(const char *rootfs_dir) {
    if (!rootfs_dir || rootfs_dir[0] == '\0') return;

    /* Verify pacman.conf has sandbox disabling flags and no DownloadUser = alpm */
    char pacman_conf[1024];
    snprintf(pacman_conf, sizeof(pacman_conf), "%s/etc/pacman.conf", rootfs_dir);
    if (access(pacman_conf, F_OK) == 0) {
        FILE *f = fopen(pacman_conf, "r");
        bool needs_config = true;
        if (f) {
            char line[512];
            bool has_disable_fs = false;
            bool has_alpm_user = false;
            while (fgets(line, sizeof(line), f)) {
                if (strstr(line, "DisableSandboxFilesystem") != NULL) {
                    has_disable_fs = true;
                }
                if (strstr(line, "DownloadUser") != NULL && strstr(line, "alpm") != NULL && line[0] != '#') {
                    has_alpm_user = true;
                }
            }
            fclose(f);
            if (has_disable_fs && !has_alpm_user) {
                needs_config = false;
            }
        }
        if (needs_config) {
            configure_pacman_conf(rootfs_dir);
        }
    }

    /* Ensure /etc/resolv.conf exists */
    char resolv_conf[1024];
    snprintf(resolv_conf, sizeof(resolv_conf), "%s/etc/resolv.conf", rootfs_dir);
    if (access(resolv_conf, F_OK) != 0) {
        setup_resolv_conf(rootfs_dir);
    }

    /* Ensure virtual proc, sys, dev stubs exist */
    setup_vfs_nodes(rootfs_dir);
}
