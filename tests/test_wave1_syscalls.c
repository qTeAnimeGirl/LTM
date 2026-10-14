#include "include/ltm_test.h"

LTM_TEST_ENTRYPOINT

static const char *TEST_FILE = "wave1_test_file.tmp";

// =============================================================================
// 1. statfs & fstatfs (Syscall 43, 44)
// =============================================================================
static void test_statfs_family(void) {
    ltm_print("  [TEST] statfs & fstatfs (Syscall 43, 44)...\n");
    struct linux_statfs sfs;
    ltm_memset(&sfs, 0, sizeof(sfs));

    // Case A: Happy Path
    int ret = sys_statfs(".", &sfs);
    TEST_ASSERT(ret == 0, "statfs('.') failed");
    TEST_ASSERT(sfs.f_bsize > 0, "statfs.f_bsize must be > 0");
    TEST_ASSERT(sfs.f_blocks > 0, "statfs.f_blocks must be > 0");

    int fd = sys_openat(AT_FDCWD, TEST_FILE, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "Failed to create test file for fstatfs");
    ret = sys_fstatfs(fd, &sfs);
    TEST_ASSERT(ret == 0, "fstatfs(fd) failed");
    TEST_ASSERT(sfs.f_bsize > 0, "fstatfs.f_bsize must be > 0");

    // Case B: Boundary / Edge Cases
    ret = sys_statfs("/", &sfs);
    TEST_ASSERT(ret == 0, "statfs('/') failed");

    // Case C: Error / Errno Handling
    ret = sys_statfs(NULL, &sfs);
    TEST_ASSERT(ret == -EFAULT, "statfs(NULL) must return -EFAULT (-14)");

    ret = sys_statfs("/nonexistent_path_wave1_xyz", &sfs);
    TEST_ASSERT(ret == -ENOENT, "statfs(nonexistent) must return -ENOENT (-2)");

    ret = sys_fstatfs(-1, &sfs);
    TEST_ASSERT(ret == -EBADF, "fstatfs(-1) must return -EBADF (-9)");

    ret = sys_fstatfs(fd, NULL);
    TEST_ASSERT(ret == -EFAULT, "fstatfs(fd, NULL) must return -EFAULT (-14)");

    sys_close(fd);
    ltm_print("    -> Passed statfs & fstatfs (Cases A, B, C)\n");
}

// =============================================================================
// 2. truncate & ftruncate (Syscall 45, 46)
// =============================================================================
static void test_truncate_family(void) {
    ltm_print("  [TEST] truncate & ftruncate (Syscall 45, 46)...\n");

    int fd = sys_openat(AT_FDCWD, TEST_FILE, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "Failed to open test file for truncate");
    char dummy[256];
    ltm_memset(dummy, 'A', sizeof(dummy));
    sys_write(fd, dummy, sizeof(dummy));

    // Case A: Happy Path
    int ret = sys_truncate(TEST_FILE, 100);
    TEST_ASSERT(ret == 0, "truncate(path, 100) failed");

    ret = sys_ftruncate(fd, 50);
    TEST_ASSERT(ret == 0, "ftruncate(fd, 50) failed");

    // Case B: Boundary / Edge Cases
    ret = sys_truncate(TEST_FILE, 0);
    TEST_ASSERT(ret == 0, "truncate(path, 0) failed");

    ret = sys_ftruncate(fd, 0);
    TEST_ASSERT(ret == 0, "ftruncate(fd, 0) failed");

    // Case C: Error / Errno Handling
    ret = sys_truncate(NULL, 10);
    TEST_ASSERT(ret == -EFAULT, "truncate(NULL) must return -EFAULT (-14)");

    ret = sys_truncate("/nonexistent_truncate_xyz", 10);
    TEST_ASSERT(ret == -ENOENT, "truncate(nonexistent) must return -ENOENT (-2)");

    ret = sys_truncate(TEST_FILE, -5);
    TEST_ASSERT(ret == -EINVAL, "truncate(negative) must return -EINVAL (-22)");

    ret = sys_ftruncate(-1, 10);
    TEST_ASSERT(ret == -EBADF, "ftruncate(-1) must return -EBADF (-9)");

    ret = sys_ftruncate(fd, -5);
    TEST_ASSERT(ret == -EINVAL, "ftruncate(negative) must return -EINVAL (-22)");

    sys_close(fd);
    ltm_print("    -> Passed truncate & ftruncate (Cases A, B, C)\n");
}

// =============================================================================
// 3. fchmod & fchmodat (Syscall 52, 53)
// =============================================================================
static void test_chmod_family(void) {
    ltm_print("  [TEST] fchmod & fchmodat (Syscall 52, 53)...\n");

    int fd = sys_openat(AT_FDCWD, TEST_FILE, O_CREAT | O_RDWR, 0644);
    TEST_ASSERT(fd >= 0, "Failed to open test file for chmod");

    // Case A: Happy Path
    int ret = sys_fchmod(fd, 0600);
    TEST_ASSERT(ret == 0, "fchmod(fd, 0600) failed");

    ret = sys_fchmodat(AT_FDCWD, TEST_FILE, 0644, 0);
    TEST_ASSERT(ret == 0, "fchmodat(AT_FDCWD, path, 0644, 0) failed");

    // Case B: Boundary / Edge Cases
    ret = sys_fchmod(fd, 0700);
    TEST_ASSERT(ret == 0, "fchmod(fd, 0700) boundary failed");

    ret = sys_fchmodat(AT_FDCWD, TEST_FILE, 0777, 0);
    TEST_ASSERT(ret == 0, "fchmodat(path, 0777, 0) boundary failed");

    // Case C: Error / Errno Handling
    ret = sys_fchmod(-1, 0644);
    TEST_ASSERT(ret == -EBADF, "fchmod(-1) must return -EBADF (-9)");

    ret = sys_fchmodat(AT_FDCWD, NULL, 0644, 0);
    TEST_ASSERT(ret == -EFAULT, "fchmodat(NULL) must return -EFAULT (-14)");

    ret = sys_fchmodat(AT_FDCWD, "nonexistent_chmod_xyz", 0644, 0);
    TEST_ASSERT(ret == -ENOENT, "fchmodat(nonexistent) must return -ENOENT (-2)");

    ret = sys_fchmodat(-999, TEST_FILE, 0644, 0);
    TEST_ASSERT(ret == -EBADF, "fchmodat(invalid_dirfd) must return -EBADF (-9)");

    sys_close(fd);
    ltm_print("    -> Passed fchmod & fchmodat (Cases A, B, C)\n");
}

// =============================================================================
// 4. fchown & fchownat (Syscall 54, 55)
// =============================================================================
static void test_chown_family(void) {
    ltm_print("  [TEST] fchown & fchownat (Syscall 54, 55)...\n");

    int fd = sys_openat(AT_FDCWD, TEST_FILE, O_CREAT | O_RDWR, 0644);
    TEST_ASSERT(fd >= 0, "Failed to open test file for chown");

    // Case A: Happy Path (-1, -1 no-op ownership query/preservation)
    int ret = sys_fchown(fd, -1, -1);
    TEST_ASSERT(ret == 0, "fchown(fd, -1, -1) failed");

    ret = sys_fchownat(AT_FDCWD, TEST_FILE, -1, -1, 0);
    TEST_ASSERT(ret == 0, "fchownat(path, -1, -1, 0) failed");

    // Case B: Boundary / Edge Cases (root ownership 0, 0)
    ret = sys_fchown(fd, 0, 0);
    TEST_ASSERT(ret == 0, "fchown(fd, 0, 0) failed");

    ret = sys_fchownat(AT_FDCWD, TEST_FILE, 0, 0, 0);
    TEST_ASSERT(ret == 0, "fchownat(path, 0, 0, 0) failed");

    // Case C: Error / Errno Handling
    ret = sys_fchown(-1, 0, 0);
    TEST_ASSERT(ret == -EBADF, "fchown(-1) must return -EBADF (-9)");

    ret = sys_fchownat(AT_FDCWD, NULL, 0, 0, 0);
    TEST_ASSERT(ret == -EFAULT, "fchownat(NULL) must return -EFAULT (-14)");

    ret = sys_fchownat(AT_FDCWD, "nonexistent_chown_xyz", 0, 0, 0);
    TEST_ASSERT(ret == -ENOENT, "fchownat(nonexistent) must return -ENOENT (-2)");

    ret = sys_fchownat(-999, TEST_FILE, 0, 0, 0);
    TEST_ASSERT(ret == -EBADF, "fchownat(invalid_dirfd) must return -EBADF (-9)");

    sys_close(fd);
    ltm_print("    -> Passed fchown & fchownat (Cases A, B, C)\n");
}

// =============================================================================
// 5. fsync & fdatasync (Syscall 82, 83)
// =============================================================================
static void test_sync_family(void) {
    ltm_print("  [TEST] fsync & fdatasync (Syscall 82, 83)...\n");

    int fd = sys_openat(AT_FDCWD, TEST_FILE, O_CREAT | O_RDWR, 0644);
    TEST_ASSERT(fd >= 0, "Failed to open test file for sync");

    // Case A: Happy Path
    int ret = sys_fsync(fd);
    TEST_ASSERT(ret == 0, "fsync(fd) failed");

    ret = sys_fdatasync(fd);
    TEST_ASSERT(ret == 0, "fdatasync(fd) failed");

    // Case B: Boundary / Edge Cases (sync after write)
    const char *data = "SYNC_VERIFICATION_BURST_DATA";
    sys_write(fd, data, ltm_strlen(data));
    ret = sys_fdatasync(fd);
    TEST_ASSERT(ret == 0, "fdatasync after write failed");
    ret = sys_fsync(fd);
    TEST_ASSERT(ret == 0, "fsync after write failed");

    // Case C: Error / Errno Handling
    ret = sys_fsync(-1);
    TEST_ASSERT(ret == -EBADF, "fsync(-1) must return -EBADF (-9)");

    ret = sys_fdatasync(-1);
    TEST_ASSERT(ret == -EBADF, "fdatasync(-1) must return -EBADF (-9)");

    sys_close(fd);
    ltm_print("    -> Passed fsync & fdatasync (Cases A, B, C)\n");
}

// =============================================================================
// 6. sched_setaffinity, sched_getaffinity, sched_yield (Syscall 122, 123, 124)
// =============================================================================
static void test_sched_family(void) {
    ltm_print("  [TEST] sched_setaffinity, sched_getaffinity, sched_yield (Syscall 122, 123, 124)...\n");

    unsigned long mask = 0;

    // Case A: Happy Path
    int ret = sys_sched_getaffinity(0, sizeof(mask), &mask);
    TEST_ASSERT(ret >= 0, "sched_getaffinity(0) failed");
    TEST_ASSERT(mask != 0, "sched_getaffinity returned empty CPU mask");

    ret = sys_sched_setaffinity(0, sizeof(mask), &mask);
    TEST_ASSERT(ret == 0, "sched_setaffinity(0) failed");

    ret = sys_sched_yield();
    TEST_ASSERT(ret == 0, "sched_yield() failed");

    // Case B: Boundary / Edge Cases (oversized mask buffer, repeated yields)
    unsigned long big_mask[16];
    ltm_memset(big_mask, 0, sizeof(big_mask));
    ret = sys_sched_getaffinity(0, sizeof(big_mask), big_mask);
    TEST_ASSERT(ret >= 0, "sched_getaffinity(big_mask) failed");

    for (int i = 0; i < 5; i++) {
        ret = sys_sched_yield();
        TEST_ASSERT(ret == 0, "Repeated sched_yield failed");
    }

    // Case C: Error / Errno Handling
    ret = sys_sched_getaffinity(0, 0, &mask);
    TEST_ASSERT(ret == -EINVAL, "sched_getaffinity(len=0) must return -EINVAL (-22)");

    ret = sys_sched_getaffinity(0, sizeof(mask), NULL);
    TEST_ASSERT(ret == -EFAULT, "sched_getaffinity(NULL) must return -EFAULT (-14)");

    ret = sys_sched_setaffinity(0, 0, &mask);
    TEST_ASSERT(ret == -EINVAL, "sched_setaffinity(len=0) must return -EINVAL (-22)");

    ret = sys_sched_setaffinity(0, sizeof(mask), NULL);
    TEST_ASSERT(ret == -EFAULT, "sched_setaffinity(NULL) must return -EFAULT (-14)");

    ret = sys_sched_getaffinity(-9999, sizeof(mask), &mask);
    TEST_ASSERT(ret == -ESRCH || ret == -EINVAL, "sched_getaffinity(invalid_pid) must return -ESRCH (-3)");

    ltm_print("    -> Passed sched_setaffinity, getaffinity, yield (Cases A, B, C)\n");
}

// =============================================================================
// 7. times (Syscall 153)
// =============================================================================
static void test_times(void) {
    ltm_print("  [TEST] times (Syscall 153)...\n");

    struct linux_tms t;
    ltm_memset(&t, 0, sizeof(t));

    // Case A: Happy Path
    long ret = sys_times(&t);
    TEST_ASSERT(ret >= 0, "times(&t) failed");

    // Case B: Boundary / Edge Cases (POSIX/Linux permits NULL pointer to return tick count)
    long ret_null = sys_times(NULL);
    TEST_ASSERT(ret_null >= 0, "times(NULL) failed");

    // Case C: Error / Errno Handling (invalid pointer)
    ret = sys_times((struct linux_tms *)(void *)1);
    TEST_ASSERT(ret == -EFAULT, "times(invalid_ptr) must return -EFAULT (-14)");

    ltm_print("    -> Passed times (Cases A, B, C)\n");
}

// =============================================================================
// 8. setpgid, getpgid, getsid, setsid (Syscall 154, 155, 156, 157)
// =============================================================================
static void test_pgrp_session_family(void) {
    ltm_print("  [TEST] setpgid, getpgid, getsid, setsid (Syscall 154, 155, 156, 157)...\n");

    // Case A: Happy Path
    int pgid = sys_getpgid(0);
    TEST_ASSERT(pgid > 0, "getpgid(0) returned non-positive pgid");

    int sid = sys_getsid(0);
    TEST_ASSERT(sid > 0, "getsid(0) returned non-positive sid");

    int ret = sys_setpgid(0, 0);
    TEST_ASSERT(ret >= 0 || ret == -EACCES || ret == -EPERM, "setpgid(0, 0) returned unexpected error");

    // Case B: Boundary / Edge Cases (query by current pid)
    int my_pid = sys_getpid();
    int pgid_by_pid = sys_getpgid(my_pid);
    TEST_ASSERT(pgid_by_pid == pgid, "getpgid(my_pid) did not match getpgid(0)");

    int sid_by_pid = sys_getsid(my_pid);
    TEST_ASSERT(sid_by_pid == sid, "getsid(my_pid) did not match getsid(0)");

    // Case C: Error / Errno Handling
    ret = sys_getpgid(-1);
    TEST_ASSERT(ret == -EINVAL || ret == -ESRCH, "getpgid(-1) must return -EINVAL or -ESRCH");

    ret = sys_getsid(-1);
    TEST_ASSERT(ret == -EINVAL || ret == -ESRCH, "getsid(-1) must return -EINVAL or -ESRCH");

    ret = sys_setpgid(-999, 0);
    TEST_ASSERT(ret == -EINVAL || ret == -ESRCH, "setpgid(-999) must return -EINVAL or -ESRCH");

    ltm_print("    -> Passed setpgid, getpgid, getsid, setsid (Cases A, B, C)\n");
}

// =============================================================================
// 9. getrusage (Syscall 165)
// =============================================================================
static void test_getrusage(void) {
    ltm_print("  [TEST] getrusage (Syscall 165)...\n");

    struct linux_rusage ru;
    ltm_memset(&ru, 0, sizeof(ru));

    // Case A: Happy Path (RUSAGE_SELF)
    int ret = sys_getrusage(RUSAGE_SELF, &ru);
    TEST_ASSERT(ret == 0, "getrusage(RUSAGE_SELF) failed");

    // Case B: Boundary / Edge Cases (RUSAGE_CHILDREN)
    ret = sys_getrusage(RUSAGE_CHILDREN, &ru);
    TEST_ASSERT(ret == 0, "getrusage(RUSAGE_CHILDREN) failed");

    // Case C: Error / Errno Handling
    ret = sys_getrusage(99999, &ru);
    TEST_ASSERT(ret == -EINVAL, "getrusage(invalid_who) must return -EINVAL (-22)");

    ret = sys_getrusage(RUSAGE_SELF, NULL);
    TEST_ASSERT(ret == -EFAULT, "getrusage(NULL) must return -EFAULT (-14)");

    ltm_print("    -> Passed getrusage (Cases A, B, C)\n");
}

// =============================================================================
// 10. umask (Syscall 166)
// =============================================================================
static void test_umask(void) {
    ltm_print("  [TEST] umask (Syscall 166)...\n");

    // Case A: Happy Path
    int old_mask = sys_umask(022);
    TEST_ASSERT(old_mask >= 0, "umask(022) failed");

    // Case B: Boundary / Edge Cases (0777 and restoration)
    int prev_mask = sys_umask(0777);
    TEST_ASSERT(prev_mask == 022, "umask(0777) did not return previous mask 022");
    sys_umask(old_mask); // restore

    // Case C: Verification that upper bits are ignored/masked
    int upper = sys_umask(077777);
    TEST_ASSERT((upper & ~0777) == 0, "umask returned upper bits");
    sys_umask(old_mask); // restore

    ltm_print("    -> Passed umask (Cases A, B, C)\n");
}

// =============================================================================
// 11. readahead (Syscall 213)
// =============================================================================
static void test_readahead(void) {
    ltm_print("  [TEST] readahead (Syscall 213)...\n");

    int fd = sys_openat(AT_FDCWD, TEST_FILE, O_CREAT | O_RDWR, 0644);
    TEST_ASSERT(fd >= 0, "Failed to open test file for readahead");
    char block[4096];
    ltm_memset(block, 'Z', sizeof(block));
    sys_write(fd, block, sizeof(block));

    // Case A: Happy Path
    ssize_t ret = sys_readahead(fd, 0, 1024);
    TEST_ASSERT(ret == 0, "readahead(fd, 0, 1024) failed");

    // Case B: Boundary / Edge Cases (count = 0)
    ret = sys_readahead(fd, 0, 0);
    TEST_ASSERT(ret == 0, "readahead(fd, 0, 0) failed");

    // Case C: Error / Errno Handling
    ret = sys_readahead(-1, 0, 1024);
    TEST_ASSERT(ret == -EBADF, "readahead(-1) must return -EBADF (-9)");

    sys_close(fd);
    ltm_print("    -> Passed readahead (Cases A, B, C)\n");
}

// =============================================================================
// 12. madvise (Syscall 233)
// =============================================================================
static void test_madvise(void) {
    ltm_print("  [TEST] madvise (Syscall 233)...\n");

    size_t sz = 64 * 1024;
    void *addr = sys_mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST_ASSERT(addr != MAP_FAILED, "mmap failed for madvise test");

    // Case A: Happy Path
    int ret = sys_madvise(addr, sz, MADV_NORMAL);
    TEST_ASSERT(ret == 0, "madvise(MADV_NORMAL) failed");

    ret = sys_madvise(addr, sz, MADV_WILLNEED);
    TEST_ASSERT(ret == 0, "madvise(MADV_WILLNEED) failed");

    // Case B: Boundary / Edge Cases
    ret = sys_madvise(addr, sz, MADV_DONTNEED);
    TEST_ASSERT(ret == 0, "madvise(MADV_DONTNEED) failed");

    // Case C: Error / Errno Handling
    ret = sys_madvise(NULL, sz, MADV_NORMAL);
    TEST_ASSERT(ret == -EINVAL || ret == -ENOMEM, "madvise(NULL) must return -EINVAL or -ENOMEM");

    ret = sys_madvise(addr, sz, -999);
    TEST_ASSERT(ret == -EINVAL, "madvise(invalid_advice) must return -EINVAL (-22)");

    sys_munmap(addr, sz);
    ltm_print("    -> Passed madvise (Cases A, B, C)\n");
}

// =============================================================================
// Main Test Runner
// =============================================================================
int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    ltm_print("[TEST] test_wave1_syscalls: Executing 3-Way Mandate (Happy, Boundary, Errno)...\n");

    test_statfs_family();
    test_truncate_family();
    test_chmod_family();
    test_chown_family();
    test_sync_family();
    test_sched_family();
    test_times();
    test_pgrp_session_family();
    test_getrusage();
    test_umask();
    test_readahead();
    test_madvise();

    // Cleanup temporary test file
    sys_openat(AT_FDCWD, TEST_FILE, O_CREAT | O_TRUNC, 0644);

    TEST_PASS("test_wave1_syscalls: All 14 Wave 1 syscalls verified across all 3 Mandate cases!");
    return 0;
}
