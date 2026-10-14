#include "ltm.h"
#include "elf_loader.h"
#include "sandbox.h"
#include "syscalls.h"
#include "rootfs_installer.h"

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
        "LTM (Linux Translation Module) for macOS Apple Silicon ARM64 [Kernel: " LTM_KERNEL_VERSION_NAME "]\n"
        "Usage: %s [options] <elf-binary> [guest args...]\n\n"
        "Options:\n"
        "  --arch                     Use Arch Linux ARM rootfs (arch_rootfs, default)\n"
        "  --alpine                   Use Alpine Linux rootfs (rootfs)\n"
        "  -r, --rootfs <dir>         Set custom guest root directory (or set LTM_ROOTFS=<dir>)\n"
        "  -e, --env <KEY=VAL>        Set environment variable in guest (can be specified multiple times)\n"
        "  --preserve-env             Inherit host environment variables (default: isolated clean env)\n"
        "  -i, --install-rootfs [type] Automatically download & install rootfs ('arch' [default] or 'alpine')\n"
        "  -v, --verbose              Enable verbose debug logging (or set LTM_DEBUG=1)\n"
        "  -s, --strace               Enable syscall tracing (or set LTM_STRACE=1)\n"
        "  -h, --help                 Display this help message and exit\n\n"
        "Examples:\n"
        "  %s /bin/bash\n"
        "  %s /usr/bin/pacman -Syu\n"
        "  %s -e FOO=BAR /bin/sh -c 'echo $FOO'\n"
        "  %s --alpine /usr/bin/fastfetch\n"
        "  %s --install-rootfs arch\n"
        "  %s ./tests/bin/test_hello\n",
        progname, progname, progname, progname, progname, progname, progname);
}

static int run_install_rootfs(const char *flavor, const char *target_dir) {
    return rootfs_install(flavor, target_dir);
}

#define MAX_CUSTOM_ENV 256
static char *g_custom_env[MAX_CUSTOM_ENV];
static int g_custom_env_count = 0;

static char **build_guest_env(char **host_envp, bool preserve_env) {
    if (preserve_env) {
        int host_count = 0;
        while (host_envp && host_envp[host_count]) host_count++;

        char **guest_env = malloc((host_count + g_custom_env_count + 16) * sizeof(char *));
        if (!guest_env) return host_envp;

        int n = 0;
        for (int i = 0; i < host_count; i++) {
            guest_env[n++] = strdup(host_envp[i]);
        }

        /* Apply custom environment variables, replacing if key already exists */
        for (int c = 0; c < g_custom_env_count; c++) {
            const char *custom = g_custom_env[c];
            const char *eq = strchr(custom, '=');
            size_t keylen = eq ? (size_t)(eq - custom) : strlen(custom);

            bool replaced = false;
            for (int i = 0; i < n; i++) {
                if (strncmp(guest_env[i], custom, keylen) == 0 && guest_env[i][keylen] == '=') {
                    free(guest_env[i]);
                    guest_env[i] = strdup(custom);
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                guest_env[n++] = strdup(custom);
            }
        }

        bool has_armcap = false;
        for (int i = 0; i < n; i++) {
            if (strncmp(guest_env[i], "OPENSSL_armcap=", 15) == 0) {
                has_armcap = true;
                break;
            }
        }
        if (!has_armcap) {
            guest_env[n++] = strdup("OPENSSL_armcap=0");
        }

        guest_env[n] = NULL;
        return guest_env;
    }

    /* Isolated guest environment (Requirement 1) */
    const char *term_val = getenv("TERM");
    if (!term_val || term_val[0] == '\0') {
        term_val = "xterm-256color";
    }
    char term_buf[256];
    snprintf(term_buf, sizeof(term_buf), "TERM=%s", term_val);

    const char *defaults[] = {
        "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
        "HOME=/root",
        "USER=root",
        "LOGNAME=root",
        "SHELL=/bin/bash",
        term_buf,
        "LANG=C.UTF-8",
        "LC_ALL=C.UTF-8",
        "HOSTNAME=ltm-mac",
        "PWD=/root",
        "OPENSSL_armcap=0",
        NULL
    };

    int num_defaults = 0;
    while (defaults[num_defaults]) num_defaults++;

    char **guest_env = malloc((num_defaults + g_custom_env_count + 16) * sizeof(char *));
    if (!guest_env) return host_envp;

    int n = 0;
    for (int i = 0; i < num_defaults; i++) {
        guest_env[n++] = strdup(defaults[i]);
    }

    /* Apply custom variables: override matching default or append */
    for (int c = 0; c < g_custom_env_count; c++) {
        const char *custom = g_custom_env[c];
        const char *eq = strchr(custom, '=');
        size_t keylen = eq ? (size_t)(eq - custom) : strlen(custom);

        bool replaced = false;
        for (int i = 0; i < n; i++) {
            if (strncmp(guest_env[i], custom, keylen) == 0 && guest_env[i][keylen] == '=') {
                free(guest_env[i]);
                guest_env[i] = strdup(custom);
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            guest_env[n++] = strdup(custom);
        }
    }

    guest_env[n] = NULL;
    return guest_env;
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
    bool preserve_env = false;
    const char *rootfs_arg = getenv("LTM_ROOTFS");
    bool rootfs_explicit = (rootfs_arg != NULL);

    while (arg_idx < argc && argv[arg_idx][0] == '-') {
        if (strcmp(argv[arg_idx], "-i") == 0 ||
            strcmp(argv[arg_idx], "--install-rootfs") == 0 ||
            strcmp(argv[arg_idx], "--setup-rootfs") == 0) {
            arg_idx++;
            const char *flavor = "arch";
            const char *target = NULL;
            if (arg_idx < argc && argv[arg_idx][0] != '-') {
                if (strcmp(argv[arg_idx], "alpine") == 0 || strcmp(argv[arg_idx], "rootfs") == 0) {
                    flavor = "alpine";
                    target = "rootfs";
                    arg_idx++;
                } else if (strcmp(argv[arg_idx], "arch") == 0 || strcmp(argv[arg_idx], "arch_rootfs") == 0) {
                    flavor = "arch";
                    target = "arch_rootfs";
                    arg_idx++;
                } else {
                    target = argv[arg_idx++];
                }
            }
            return run_install_rootfs(flavor, target);
        } else if (strcmp(argv[arg_idx], "--arch") == 0) {
            rootfs_arg = "arch_rootfs";
            rootfs_explicit = true;
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "--alpine") == 0) {
            rootfs_arg = "rootfs";
            rootfs_explicit = true;
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "-e") == 0 || strcmp(argv[arg_idx], "--env") == 0) {
            arg_idx++;
            if (arg_idx >= argc) {
                fprintf(stderr, "Error: -e / --env requires a KEY=VAL argument\n");
                return 1;
            }
            if (g_custom_env_count < MAX_CUSTOM_ENV) {
                g_custom_env[g_custom_env_count++] = argv[arg_idx++];
            }
        } else if (strcmp(argv[arg_idx], "--preserve-env") == 0) {
            preserve_env = true;
            arg_idx++;
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
            rootfs_explicit = true;
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

    const char *binary_arg = argv[arg_idx];
    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) cwd[0] = '\0';
    bool is_container_path = (binary_arg[0] == '/' && (cwd[0] == '\0' || strncmp(binary_arg, cwd, strlen(cwd)) != 0));

    /* Default to Arch Linux ARM rootfs; auto-fallback to Alpine only if binary exists in rootfs but not arch_rootfs */
    if (!rootfs_explicit) {
        if (is_container_path) {
            char check_path[1024];
            snprintf(check_path, sizeof(check_path), "arch_rootfs%s", binary_arg);
            if (access(check_path, F_OK) == 0) {
                rootfs_arg = "arch_rootfs";
            } else {
                snprintf(check_path, sizeof(check_path), "rootfs%s", binary_arg);
                if (access(check_path, F_OK) == 0) {
                    rootfs_arg = "rootfs";
                } else {
                    rootfs_arg = "arch_rootfs";
                }
            }
        } else if (access(binary_arg, F_OK) == 0) {
            /* Standalone binary on host disk (e.g. ./tests/bin/test_hello) */
            rootfs_arg = NULL;
        } else {
            rootfs_arg = "arch_rootfs";
        }
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

    bool rootfs_missing = false;
    if (rootfs_arg) {
        char sh_check[1024], bash_check[1024], bb_check[1024];
        snprintf(sh_check, sizeof(sh_check), "%s/bin/sh", rootfs_arg);
        snprintf(bash_check, sizeof(bash_check), "%s/bin/bash", rootfs_arg);
        snprintf(bb_check, sizeof(bb_check), "%s/bin/busybox", rootfs_arg);

        struct stat rst, lst;
        if (stat(rootfs_arg, &rst) != 0 || !S_ISDIR(rst.st_mode)) {
            rootfs_missing = true;
        } else if (lstat(sh_check, &lst) != 0 && lstat(bash_check, &lst) != 0 && lstat(bb_check, &lst) != 0) {
            rootfs_missing = true;
        }
    }

    if (rootfs_missing) {
        /* If user is running a standalone test binary on host disk (e.g. ./tests/bin/test_hello)
         * and did not explicitly specify a rootfs, allow standalone mode without downloading. */
        if (!is_container_path && !rootfs_explicit && access(binary_arg, F_OK) == 0) {
            sandbox_init_standalone();
        } else {
            const char *flavor = (strcmp(rootfs_arg, "rootfs") == 0) ? "alpine" : "arch";
            printf("[*] Linux ARM64 rootfs '%s' missing or incomplete (/bin/sh not found).\n", rootfs_arg);
            printf("[*] Automatically downloading and configuring %s rootfs...\n",
                   (strcmp(flavor, "alpine") == 0) ? "Alpine Linux" : "Arch Linux ARM");
            if (run_install_rootfs(flavor, rootfs_arg) != 0) {
                fprintf(stderr, "[-] Failed to automatically setup rootfs '%s'\n", rootfs_arg);
                return 1;
            }
        }
    }

    /* Initialize sandbox with chosen chroot rootfs if not already in standalone mode */
    if (g_ltm.host_rootfs[0] == '\0') {
        int s_res = sandbox_init(rootfs_arg);
        if (s_res != 0) {
            if (s_res == -2 && !is_container_path && access(binary_arg, F_OK) == 0) {
                sandbox_init_standalone();
            } else {
                ltm_log_error("Failed to initialize sandbox rootfs '%s'", rootfs_arg);
                return 1;
            }
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

    /* If running inside guest container rootfs, ensure working directory defaults to /root */
    if (is_container_path) {
        char cur_cwd[1024];
        if (getcwd(cur_cwd, sizeof(cur_cwd))) {
            size_t rootfs_len = strlen(g_ltm.host_rootfs);
            if (strncmp(cur_cwd, g_ltm.host_rootfs, rootfs_len) != 0) {
                char root_home[1024];
                snprintf(root_home, sizeof(root_home), "%s/root", g_ltm.host_rootfs);
                struct stat st;
                if (stat(root_home, &st) == 0 && S_ISDIR(st.st_mode)) {
                    chdir(root_home);
                } else {
                    chdir(g_ltm.host_rootfs);
                }
            }
        }
    }

    /* Prepare guest argc / argv / envp */
    g_ltm.guest_argc = argc - arg_idx;
    g_ltm.guest_argv = &argv[arg_idx];
    g_ltm.guest_envp = build_guest_env(envp, preserve_env);

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
