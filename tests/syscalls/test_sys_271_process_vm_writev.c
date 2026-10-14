#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    char src[] = "Hello process_vm_writev!";
    char dst[32];
    ltm_memset(dst, 0, sizeof(dst));

    struct iovec local_iov = { .iov_base = src, .iov_len = sizeof(src) };
    struct iovec remote_iov = { .iov_base = dst, .iov_len = sizeof(dst) };

    // Case A: Happy Path (Write memory to self process)
    int ret = sys_process_vm_writev(sys_getpid(), &local_iov, 1, &remote_iov, 1, 0);
    TEST_ASSERT(ret == sizeof(src), "process_vm_writev failed");
    TEST_ASSERT(ltm_strcmp(src, dst) == 0, "process_vm_writev copied data mismatch");

    // Case B: Boundary / Edge Cases (Write with pid 0 -> self)
    ltm_memset(dst, 0, sizeof(dst));
    ret = sys_process_vm_writev(0, &local_iov, 1, &remote_iov, 1, 0);
    TEST_ASSERT(ret == sizeof(src), "process_vm_writev(0) failed");

    // Case C: Error / Errno Handling (flags != 0 -> -EINVAL, negative pid -> -ESRCH, NULL iov -> -EFAULT)
    ret = sys_process_vm_writev(sys_getpid(), &local_iov, 1, &remote_iov, 1, 1 /* flags != 0 */);
    TEST_ASSERT(ret == -EINVAL, "process_vm_writev(flags != 0) must return -EINVAL");

    ret = sys_process_vm_writev(-1, &local_iov, 1, &remote_iov, 1, 0);
    TEST_ASSERT(ret == -ESRCH, "process_vm_writev(-1 pid) must return -ESRCH");

    ret = sys_process_vm_writev(sys_getpid(), &local_iov, 1, NULL, 1, 0);
    TEST_ASSERT(ret == -EFAULT, "process_vm_writev(NULL remote) must return -EFAULT");

    TEST_PASS("test_sys_271_process_vm_writev passed!");
    return 0;
}
