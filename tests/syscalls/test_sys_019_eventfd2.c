#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int fd = sys_eventfd2(0, 0);
    TEST_ASSERT(fd >= 0, "eventfd2(0, 0) failed");
    uint64_t wval = 42;
    ssize_t nw = sys_write(fd, &wval, sizeof(wval));
    TEST_ASSERT(nw == sizeof(wval), "write to eventfd failed");
    uint64_t rval = 0;
    ssize_t nr = sys_read(fd, &rval, sizeof(rval));
    TEST_ASSERT(nr == sizeof(rval), "read from eventfd failed");
    TEST_ASSERT(rval == 42, "eventfd value mismatch");
    sys_close(fd);

    // Case B: Boundary / Edge Cases (initial value + nonblock)
    fd = sys_eventfd2(10, EFD_NONBLOCK);
    TEST_ASSERT(fd >= 0, "eventfd2 with initial value failed");
    rval = 0;
    nr = sys_read(fd, &rval, sizeof(rval));
    TEST_ASSERT(nr == sizeof(rval), "read initial val failed");
    TEST_ASSERT(rval == 10, "initial val mismatch");
    sys_close(fd);

    // Case C: Error / Errno Translation
    int err = sys_eventfd2(0, 0x7fffffff);
    TEST_ASSERT(err == -EINVAL, "eventfd2 invalid flags must return -EINVAL");

    TEST_PASS("test_sys_019_eventfd2 passed!");
    return 0;
}
