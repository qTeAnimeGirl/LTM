#include "ltm.h"
#include "elf_loader.h"
#include "sandbox.h"
#include "syscalls.h"

ltm_runtime_t g_ltm = {0};

static void print_usage(const char *progname) {
    fprintf(stderr,
        "LTM (Linux Translation Module) for macOS Apple Silicon ARM64\n"
        "Usage: %s [options] <elf-binary> [guest args...]\n\n"
        "Options:\n"
        "  -r, --rootfs <dir> Set guest root directory (chroot rootfs, or set LTM_ROOTFS=<dir>)\n"
        "  -v, --verbose      Enable verbose debug logging (or set LTM_DEBUG=1)\n"
        "  -s, --strace       Enable syscall tracing (or set LTM_STRACE=1)\n"
        "  -h, --help         Display this help message and exit\n\n"
        "Examples:\n"
        "  %s ./tests/bin/test_hello\n"
        "  %s -r ./alpine_rootfs /bin/busybox ls -la /\n",
        progname, progname, progname);
}

int main(int argc, char **argv, char **envp) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    int arg_idx = 1;
    bool debug = getenv("LTM_DEBUG") != NULL;
    bool strace = getenv("LTM_STRACE") != NULL;
    const char *rootfs_arg = getenv("LTM_ROOTFS");

    while (arg_idx < argc && argv[arg_idx][0] == '-') {
        if (strcmp(argv[arg_idx], "-v") == 0 || strcmp(argv[arg_idx], "--verbose") == 0) {
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

    /* Initialize sandbox with chosen chroot rootfs */
    if (sandbox_init(rootfs_arg) != 0) {
        ltm_log_error("Failed to initialize sandbox rootfs '%s'", rootfs_arg ? rootfs_arg : "(default)");
        return 1;
    }

    const char *binary_arg = argv[arg_idx];
    char chroot_cand[1024];

    /* If binary is an absolute path within chroot (e.g. /bin/busybox) */
    if (binary_arg[0] == '/' && g_ltm.host_rootfs[0] != '\0') {
        snprintf(chroot_cand, sizeof(chroot_cand), "%s%s", g_ltm.host_rootfs, binary_arg);
        if (access(chroot_cand, F_OK) == 0) {
            strncpy(g_ltm.binary_path, chroot_cand, sizeof(g_ltm.binary_path) - 1);
            g_ltm.binary_path[sizeof(g_ltm.binary_path) - 1] = '\0';
        } else if (!realpath(binary_arg, g_ltm.binary_path)) {
            strncpy(g_ltm.binary_path, binary_arg, sizeof(g_ltm.binary_path) - 1);
            g_ltm.binary_path[sizeof(g_ltm.binary_path) - 1] = '\0';
        }
    } else {
        if (!realpath(binary_arg, g_ltm.binary_path)) {
            strncpy(g_ltm.binary_path, binary_arg, sizeof(g_ltm.binary_path) - 1);
            g_ltm.binary_path[sizeof(g_ltm.binary_path) - 1] = '\0';
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
