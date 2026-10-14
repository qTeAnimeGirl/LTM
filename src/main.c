#include "ltm.h"
#include "elf_loader.h"
#include "sandbox.h"
#include "syscalls.h"

#include <termios.h>

ltm_runtime_t g_ltm = {0};

static struct termios g_saved_termios;
static bool g_have_saved_termios = false;

static void ltm_restore_terminal(void) {
    if (g_have_saved_termios) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_termios);
    }
}

static void ltm_terminal_sig_handler(int sig) {
    ltm_restore_terminal();
    signal(sig, SIG_DFL);
    raise(sig);
}

static void print_usage(const char *progname) {
    fprintf(stderr,
        "LTM (Linux Translation Module) for macOS Apple Silicon ARM64\n"
        "Usage: %s [options] <elf-binary> [guest args...]\n\n"
        "Options:\n"
        "  -i, --install-rootfs [dir] Automatically download & install Alpine Linux rootfs (with Fastfetch & Bash)\n"
        "  -r, --rootfs <dir>         Set guest root directory (chroot rootfs, or set LTM_ROOTFS=<dir>)\n"
        "  -v, --verbose              Enable verbose debug logging (or set LTM_DEBUG=1)\n"
        "  -s, --strace               Enable syscall tracing (or set LTM_STRACE=1)\n"
        "  -h, --help                 Display this help message and exit\n\n"
        "Examples:\n"
        "  %s --install-rootfs\n"
        "  %s /usr/bin/fastfetch\n"
        "  %s /bin/bash\n"
        "  %s ./tests/bin/test_hello\n",
        progname, progname, progname, progname, progname);
}

static int run_install_rootfs(const char *target_dir) {
    if (!target_dir || target_dir[0] == '\0') {
        target_dir = "rootfs";
    }

    printf("==============================================================\n");
    printf("   LTM Linux ARM64 Rootfs & Environment Installer\n");
    printf("==============================================================\n");
    printf("[*] Target directory: %s\n\n", target_dir);

    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
        "mkdir -p '%s' && "
        "echo '[+] Downloading Alpine Linux 3.20 aarch64 minirootfs...' && "
        "curl -sSL https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/aarch64/alpine-minirootfs-3.20.0-aarch64.tar.gz | tar -xz -C '%s' && "
        "echo '[+] Downloading Fastfetch, GNU Bash, and dependencies...' && "
        "TMP_PKGS=\"$(mktemp -d /tmp/ltm_pkgs.XXXXXX)\" && "
        "curl -sSL -o \"$TMP_PKGS/fastfetch.apk\" https://dl-cdn.alpinelinux.org/alpine/v3.20/community/aarch64/fastfetch-2.13.1-r0.apk && "
        "curl -sSL -o \"$TMP_PKGS/bash.apk\" https://dl-cdn.alpinelinux.org/alpine/v3.20/main/aarch64/bash-5.2.26-r0.apk && "
        "curl -sSL -o \"$TMP_PKGS/readline.apk\" https://dl-cdn.alpinelinux.org/alpine/v3.20/main/aarch64/readline-8.2.10-r0.apk && "
        "curl -sSL -o \"$TMP_PKGS/libncursesw.apk\" https://dl-cdn.alpinelinux.org/alpine/v3.20/main/aarch64/libncursesw-6.4_p20240420-r2.apk && "
        "curl -sSL -o \"$TMP_PKGS/ncurses-base.apk\" https://dl-cdn.alpinelinux.org/alpine/v3.20/main/aarch64/ncurses-terminfo-base-6.4_p20240420-r2.apk && "
        "tar -xz -C '%s' -f \"$TMP_PKGS/fastfetch.apk\" usr/bin/fastfetch usr/share/fastfetch 2>/dev/null || true && "
        "tar -xz -C '%s' -f \"$TMP_PKGS/bash.apk\" 2>/dev/null || true && "
        "tar -xz -C '%s' -f \"$TMP_PKGS/readline.apk\" 2>/dev/null || true && "
        "tar -xz -C '%s' -f \"$TMP_PKGS/libncursesw.apk\" 2>/dev/null || true && "
        "tar -xz -C '%s' -f \"$TMP_PKGS/ncurses-base.apk\" 2>/dev/null || true && "
        "chmod +x '%s/bin/busybox' '%s/bin/sh' '%s/bin/bash' '%s/usr/bin/fastfetch' 2>/dev/null || true && "
        "rm -rf \"$TMP_PKGS\"",
        target_dir, target_dir, target_dir, target_dir, target_dir, target_dir, target_dir,
        target_dir, target_dir, target_dir, target_dir);

    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "\n[-] Rootfs installation failed with exit code %d\n", ret);
        return 1;
    }

    printf("\n[+] Verification:\n");
    char test_bin[1024];
    snprintf(test_bin, sizeof(test_bin), "%s/bin/busybox", target_dir);
    if (access(test_bin, X_OK) == 0) printf("    [OK] %s exists and is executable\n", test_bin);
    snprintf(test_bin, sizeof(test_bin), "%s/bin/bash", target_dir);
    if (access(test_bin, X_OK) == 0) printf("    [OK] %s exists and is executable\n", test_bin);
    snprintf(test_bin, sizeof(test_bin), "%s/usr/bin/fastfetch", target_dir);
    if (access(test_bin, X_OK) == 0) printf("    [OK] %s exists and is executable\n", test_bin);

    printf("\n==============================================================\n");
    printf("   Alpine Linux ARM64 Rootfs Successfully Installed!\n");
    printf("==============================================================\n");
    printf("   Try running:\n");
    printf("     ./ltm /usr/bin/fastfetch\n");
    printf("     ./ltm /bin/bash\n");
    printf("     ./ltm /bin/sh\n");
    printf("==============================================================\n");
    return 0;
}

int main(int argc, char **argv, char **envp) {
    /* Prevent OpenSSL from executing unsupported ARMv8 crypto instructions during CPUID probes */
    setenv("OPENSSL_armcap", "0", 0);

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    int arg_idx = 1;
    bool debug = getenv("LTM_DEBUG") != NULL;
    bool strace = getenv("LTM_STRACE") != NULL;
    const char *rootfs_arg = getenv("LTM_ROOTFS");

    while (arg_idx < argc && argv[arg_idx][0] == '-') {
        if (strcmp(argv[arg_idx], "-i") == 0 ||
            strcmp(argv[arg_idx], "--install-rootfs") == 0 ||
            strcmp(argv[arg_idx], "--setup-rootfs") == 0) {
            const char *target = (arg_idx + 1 < argc && argv[arg_idx + 1][0] != '-') ? argv[++arg_idx] : "rootfs";
            return run_install_rootfs(target);
        } else if (strcmp(argv[arg_idx], "-v") == 0 || strcmp(argv[arg_idx], "--verbose") == 0) {
            debug = true;
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "-s") == 0 || strcmp(argv[arg_idx], "--strace") == 0) {
            strace = true;
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "-r") == 0 || strcmp(argv[arg_idx], "--root") == 0 ||
                   strcmp(argv[arg_idx], "--rootfs") == 0) {
            arg_idx++;
            if (arg_idx >= argc) {
                fprintf(stderr, "Error: -r / --rootfs requires a directory argument\n");
                return 1;
            }
            rootfs_arg = argv[arg_idx++];
        } else if (strcmp(argv[arg_idx], "-h") == 0 || strcmp(argv[arg_idx], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[arg_idx], "--") == 0) {
            arg_idx++;
            break;
        } else {
            break;
        }
    }

    if (arg_idx >= argc) {
        print_usage(argv[0]);
        return 1;
    }

    g_ltm.debug_enabled = debug;
    g_ltm.trace_syscalls = strace;

    /* Save original terminal attributes for graceful restoration upon exit */
    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &g_saved_termios) == 0) {
        g_have_saved_termios = true;
        atexit(ltm_restore_terminal);
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = ltm_terminal_sig_handler;
        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);
        sigaction(SIGHUP, &sa, NULL);
    }

    const char *binary_arg = argv[arg_idx];
    bool is_container_path = (binary_arg[0] == '/');

    /* Initialize sandbox with chosen chroot rootfs */
    int s_res = sandbox_init(rootfs_arg);
    if (s_res != 0) {
        if (s_res == -2) {
            /* If running a local freestanding test binary on host disk, allow standalone mode */
            if (!is_container_path && access(binary_arg, F_OK) == 0) {
                sandbox_init_standalone();
            } else {
                fprintf(stderr,
                    "\n[LTM:ERROR] No Linux ARM64 rootfs detected!\n"
                    "LTM requires a root filesystem to execute Linux distribution binaries like '%s'.\n\n"
                    "To install an Alpine Linux rootfs (with Fastfetch & GNU Bash) automatically, run:\n"
                    "    %s --install-rootfs\n\n"
                    "Or specify an existing rootfs directory with:\n"
                    "    %s -r /path/to/rootfs <binary>\n\n",
                    binary_arg, argv[0], argv[0]);
                return 1;
            }
        } else {
            ltm_log_error("Failed to initialize sandbox rootfs '%s'", rootfs_arg ? rootfs_arg : "(default)");
            return 1;
        }
    }

    /* If binary_arg is a relative host path or within host_root/host_rootfs, resolve directly */
    if (binary_arg[0] != '/' && access(binary_arg, F_OK) == 0 && realpath(binary_arg, g_ltm.binary_path)) {
        /* Local relative host path (e.g. ./tests/bin/test_hello) */
    } else if ((strncmp(binary_arg, g_ltm.host_root, strlen(g_ltm.host_root)) == 0 ||
                strncmp(binary_arg, g_ltm.host_rootfs, strlen(g_ltm.host_rootfs)) == 0) &&
               access(binary_arg, F_OK) == 0 && realpath(binary_arg, g_ltm.binary_path)) {
        /* Already full path within host project or rootfs */
    } else {
        /* Guest container path (e.g. /bin/sh, /usr/bin/fastfetch) */
        if (sandbox_resolve_path(LINUX_AT_FDCWD, binary_arg, g_ltm.binary_path, sizeof(g_ltm.binary_path), 0) < 0) {
            if (!realpath(binary_arg, g_ltm.binary_path)) {
                strncpy(g_ltm.binary_path, binary_arg, sizeof(g_ltm.binary_path) - 1);
                g_ltm.binary_path[sizeof(g_ltm.binary_path) - 1] = '\0';
            }
        }
    }

    /* Prepare guest argc / argv / envp */
    g_ltm.guest_argc = argc - arg_idx;
    g_ltm.guest_argv = &argv[arg_idx];
    g_ltm.guest_envp = envp;

    /* Initialize syscall trap handling */
    if (syscall_dispatcher_init() != 0) {
        ltm_log_error("Failed to initialize syscall dispatcher");
        return 1;
    }

    /* Load guest ELF binary */
    if (elf_load_binary(g_ltm.binary_path, &g_ltm) != 0) {
        ltm_log_error("Failed to load ELF binary '%s'", g_ltm.binary_path);
        return 1;
    }

    /* Setup guest initial stack ABI */
    if (elf_setup_stack(&g_ltm) != 0) {
        ltm_log_error("Failed to setup guest stack");
        return 1;
    }

    ltm_log_debug("Ready to launch guest ELF!");
    ltm_log_debug("  Entry point: 0x%lx", (unsigned long)g_ltm.elf_entry);
    ltm_log_debug("  Guest SP:    %p", g_ltm.guest_sp);

    /* Jump to guest entry */
    ltm_jump_to_entry(g_ltm.elf_entry, g_ltm.guest_sp);

    /* Unreachable */
    return 0;
}
