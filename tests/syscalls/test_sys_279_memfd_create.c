#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int mfd = sys_memfd_create("test_memfd", MFD_CLOEXEC);
    TEST_ASSERT(mfd >= 0, "memfd_create failed");
    const char *data = "MEMFD_DATA_STORAGE";
    size_t len = ltm_strlen(data);
    ssize_t w = sys_write(mfd, data, len);
    TEST_ASSERT(w == (ssize_t)len, "memfd write failed");
    sys_lseek(mfd, 0, SEEK_SET);
    char buf[32];
    ltm_memset(buf, 0, sizeof(buf));
    ssize_t r = sys_read(mfd, buf, len);
    TEST_ASSERT(r == (ssize_t)len && ltm_memcmp(buf, data, len) == 0, "memfd read corrupted");
    sys_close(mfd);

    // Case B: Boundary / Edge Cases
    mfd = sys_memfd_create("", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    TEST_ASSERT(mfd >= 0, "memfd_create(empty_name) failed");
    sys_close(mfd);

    // Case C: Error / Errno Handling
    int ret = sys_memfd_create(NULL, 0);
    TEST_ASSERT(ret == -EFAULT, "memfd_create(NULL) must return -EFAULT");
    ret = sys_memfd_create("test", 0xffff0000);
    TEST_ASSERT(ret == -EINVAL, "memfd_create(invalid_flags) must return -EINVAL");

    TEST_PASS("test_sys_279_memfd_create passed!");
    return 0;
}
