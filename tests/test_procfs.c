#include "include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    char buf[1024];
    ssize_t nread;
    int fd;

    ltm_print("[TEST] test_procfs: Verifying virtual procfs emulation...\n");

    // 1. Inspect /proc/version
    ltm_print("  Step 1: Reading /proc/version...\n");
    fd = sys_openat(AT_FDCWD, "/proc/version", O_RDONLY, 0);
    TEST_ASSERT(fd >= 0, "openat(/proc/version) failed");

    ltm_memset(buf, 0, sizeof(buf));
    nread = sys_read(fd, buf, sizeof(buf) - 1);
    TEST_ASSERT(nread > 0, "read(/proc/version) returned empty or error");
    buf[nread] = '\0';
    sys_close(fd);

    ltm_print("    /proc/version: ");
    ltm_print(buf);
    if (buf[nread - 1] != '\n') ltm_print("\n");

    TEST_ASSERT(ltm_strstr(buf, "Linux") != NULL, "/proc/version must mention 'Linux'");

    // 2. Inspect /proc/cpuinfo
    ltm_print("  Step 2: Reading /proc/cpuinfo...\n");
    fd = sys_openat(AT_FDCWD, "/proc/cpuinfo", O_RDONLY, 0);
    TEST_ASSERT(fd >= 0, "openat(/proc/cpuinfo) failed");

    ltm_memset(buf, 0, sizeof(buf));
    nread = sys_read(fd, buf, sizeof(buf) - 1);
    TEST_ASSERT(nread > 0, "read(/proc/cpuinfo) returned empty or error");
    buf[nread] = '\0';
    sys_close(fd);

    ltm_print("    /proc/cpuinfo (sample):\n");
    // Print first 200 chars or until second newline
    size_t preview_len = nread < 200 ? nread : 200;
    for (size_t i = 0; i < preview_len; i++) {
        char c[2] = { buf[i], 0 };
        ltm_print(c);
    }
    ltm_print("...\n");

    TEST_ASSERT(ltm_strstr(buf, "processor") != NULL ||
                ltm_strstr(buf, "CPU") != NULL ||
                ltm_strstr(buf, "model") != NULL ||
                ltm_strstr(buf, "ARM") != NULL ||
                ltm_strstr(buf, "aarch64") != NULL,
                "/proc/cpuinfo missing processor/architecture identifiers");

    // 3. Inspect /proc/self/cmdline
    ltm_print("  Step 3: Reading /proc/self/cmdline...\n");
    fd = sys_openat(AT_FDCWD, "/proc/self/cmdline", O_RDONLY, 0);
    TEST_ASSERT(fd >= 0, "openat(/proc/self/cmdline) failed");

    ltm_memset(buf, 0, sizeof(buf));
    nread = sys_read(fd, buf, sizeof(buf) - 1);
    TEST_ASSERT(nread > 0, "read(/proc/self/cmdline) returned empty or error");
    buf[nread] = '\0';
    sys_close(fd);

    ltm_print("    /proc/self/cmdline: ");
    ltm_print(buf);
    ltm_print("\n");

    TEST_PASS("test_procfs: Virtual procfs successfully inspected and validated!");
    return 0;
}
