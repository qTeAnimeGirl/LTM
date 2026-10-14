#include "include/ltm_test.h"

LTM_TEST_ENTRYPOINT

static const char *TEST_FILE = "wave2_test_file.tmp";
static const char *TEST_FILE_COPY = "wave2_copy_file.tmp";

// =============================================================================
// 1. flock (Syscall 32)
// =============================================================================
static void test_flock(void) {
    ltm_print("  [TEST] flock (Syscall 32)...\n");
    int fd = sys_openat(AT_FDCWD, TEST_FILE, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed for flock test");

    // Case A: Happy Path
    int ret = sys_flock(fd, LOCK_EX);
    TEST_ASSERT(ret == 0, "flock(LOCK_EX) failed");
    ret = sys_flock(fd, LOCK_UN);
    TEST_ASSERT(ret == 0, "flock(LOCK_UN) failed");

    // Case B: Boundary / Edge Cases (non-blocking shared lock)
    ret = sys_flock(fd, LOCK_SH | LOCK_NB);
    TEST_ASSERT(ret == 0, "flock(LOCK_SH | LOCK_NB) failed");
    sys_flock(fd, LOCK_UN);

    // Case C: Error / Errno Handling
    ret = sys_flock(-1, LOCK_EX);
    TEST_ASSERT(ret == -EBADF, "flock(-1) must return -EBADF (-9)");

    ret = sys_flock(fd, 9999);
    TEST_ASSERT(ret == -EINVAL, "flock(invalid_op) must return -EINVAL (-22)");

    sys_close(fd);
    ltm_print("    -> Passed flock (Cases A, B, C)\n");
}

// =============================================================================
// 2. fallocate (Syscall 47)
// =============================================================================
static void test_fallocate(void) {
    ltm_print("  [TEST] fallocate (Syscall 47)...\n");
    int fd = sys_openat(AT_FDCWD, TEST_FILE, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed for fallocate test");

    // Case A: Happy Path (allocate 4096 bytes)
    int ret = sys_fallocate(fd, 0, 0, 4096);
    TEST_ASSERT(ret == 0, "fallocate(0, 0, 4096) failed");

    // Case B: Boundary / Edge Cases (FALLOC_FL_KEEP_SIZE)
    ret = sys_fallocate(fd, FALLOC_FL_KEEP_SIZE, 0, 8192);
    TEST_ASSERT(ret == 0, "fallocate(KEEP_SIZE) failed");

    // Case C: Error / Errno Handling
    ret = sys_fallocate(-1, 0, 0, 4096);
    TEST_ASSERT(ret == -EBADF, "fallocate(-1) must return -EBADF (-9)");

    ret = sys_fallocate(fd, 0, 0, -100);
    TEST_ASSERT(ret == -EINVAL, "fallocate(len <= 0) must return -EINVAL (-22)");

    ret = sys_fallocate(fd, 0, -5, 4096);
    TEST_ASSERT(ret == -EINVAL, "fallocate(offset < 0) must return -EINVAL (-22)");

    sys_close(fd);
    ltm_print("    -> Passed fallocate (Cases A, B, C)\n");
}

// =============================================================================
// 3. chroot (Syscall 51)
// =============================================================================
static void test_chroot(void) {
    ltm_print("  [TEST] chroot (Syscall 51)...\n");

    // Case A: Happy Path
    int ret = sys_chroot(".");
    TEST_ASSERT(ret == 0, "chroot('.') failed");

    // Case B: Boundary / Edge Cases
    ret = sys_chroot("/");
    TEST_ASSERT(ret == 0, "chroot('/') failed");

    // Case C: Error / Errno Handling
    ret = sys_chroot(NULL);
    TEST_ASSERT(ret == -EFAULT, "chroot(NULL) must return -EFAULT (-14)");

    ret = sys_chroot("/nonexistent_chroot_path_xyz");
    TEST_ASSERT(ret == -ENOENT, "chroot(nonexistent) must return -ENOENT (-2)");

    ltm_print("    -> Passed chroot (Cases A, B, C)\n");
}

// =============================================================================
// 4. pread64 & pwrite64 (Syscall 67, 68)
// =============================================================================
static void test_pread_pwrite(void) {
    ltm_print("  [TEST] pread64 & pwrite64 (Syscall 67, 68)...\n");
    int fd = sys_openat(AT_FDCWD, TEST_FILE, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed for pread/pwrite test");

    const char *msg = "PREAD_PWRITE_TEST_DATA_12345";
    size_t len = ltm_strlen(msg);

    // Case A: Happy Path (write at offset 100, read at offset 100)
    ssize_t written = sys_pwrite64(fd, msg, len, 100);
    TEST_ASSERT(written == (ssize_t)len, "pwrite64 failed");

    char buf[64];
    ltm_memset(buf, 0, sizeof(buf));
    ssize_t read_bytes = sys_pread64(fd, buf, len, 100);
    TEST_ASSERT(read_bytes == (ssize_t)len, "pread64 failed");
    TEST_ASSERT(ltm_memcmp(buf, msg, len) == 0, "pread64 data corrupted");

    // Check that file offset was preserved
    off_t cur_pos = sys_lseek(fd, 0, SEEK_CUR);
    TEST_ASSERT(cur_pos == 0, "pwrite64/pread64 unexpectedly altered file offset");

    // Case B: Boundary / Edge Cases (count = 0)
    written = sys_pwrite64(fd, msg, 0, 100);
    TEST_ASSERT(written == 0, "pwrite64(count=0) must return 0");

    read_bytes = sys_pread64(fd, buf, 0, 100);
    TEST_ASSERT(read_bytes == 0, "pread64(count=0) must return 0");

    // Case C: Error / Errno Handling
    written = sys_pwrite64(-1, msg, len, 100);
    TEST_ASSERT(written == -EBADF, "pwrite64(-1) must return -EBADF (-9)");

    written = sys_pwrite64(fd, NULL, len, 100);
    TEST_ASSERT(written == -EFAULT, "pwrite64(NULL) must return -EFAULT (-14)");

    written = sys_pwrite64(fd, msg, len, -1);
    TEST_ASSERT(written == -EINVAL, "pwrite64(negative_offset) must return -EINVAL (-22)");

    read_bytes = sys_pread64(-1, buf, len, 100);
    TEST_ASSERT(read_bytes == -EBADF, "pread64(-1) must return -EBADF (-9)");

    read_bytes = sys_pread64(fd, NULL, len, 100);
    TEST_ASSERT(read_bytes == -EFAULT, "pread64(NULL) must return -EFAULT (-14)");

    read_bytes = sys_pread64(fd, buf, len, -1);
    TEST_ASSERT(read_bytes == -EINVAL, "pread64(negative_offset) must return -EINVAL (-22)");

    sys_close(fd);
    ltm_print("    -> Passed pread64 & pwrite64 (Cases A, B, C)\n");
}

// =============================================================================
// 5. signalfd4 (Syscall 74)
// =============================================================================
static void test_signalfd4(void) {
    ltm_print("  [TEST] signalfd4 (Syscall 74)...\n");
    uint64_t sigmask = (1ULL << 14); // SIGTERM mask

    // Case A: Happy Path
    int sfd = sys_signalfd4(-1, &sigmask, 8, 0);
    TEST_ASSERT(sfd >= 0, "signalfd4(-1, 0) failed");
    sys_close(sfd);

    // Case B: Boundary / Edge Cases (SFD_NONBLOCK | SFD_CLOEXEC)
    sfd = sys_signalfd4(-1, &sigmask, 8, SFD_NONBLOCK | SFD_CLOEXEC);
    TEST_ASSERT(sfd >= 0, "signalfd4(NONBLOCK | CLOEXEC) failed");
    sys_close(sfd);

    // Case C: Error / Errno Handling
    int ret = sys_signalfd4(-1, NULL, 8, 0);
    TEST_ASSERT(ret == -EFAULT, "signalfd4(NULL) must return -EFAULT (-14)");

    ret = sys_signalfd4(-1, &sigmask, 4, 0); // Invalid sizemask
    TEST_ASSERT(ret == -EINVAL, "signalfd4(sizemask!=8) must return -EINVAL (-22)");

    ret = sys_signalfd4(-999, &sigmask, 8, 0);
    TEST_ASSERT(ret == -EBADF, "signalfd4(invalid_ufd) must return -EBADF (-9)");

    ltm_print("    -> Passed signalfd4 (Cases A, B, C)\n");
}

// =============================================================================
// 6. sync & syncfs (Syscall 81, 267)
// =============================================================================
static void test_sync_family(void) {
    ltm_print("  [TEST] sync & syncfs (Syscall 81, 267)...\n");
    int fd = sys_openat(AT_FDCWD, TEST_FILE, O_CREAT | O_RDWR, 0644);
    TEST_ASSERT(fd >= 0, "openat failed for sync/syncfs test");

    // Case A: Happy Path
    sys_sync();

    int ret = sys_syncfs(fd);
    TEST_ASSERT(ret == 0, "syncfs(fd) failed");

    // Case B: Boundary / Edge Cases (repeated syncfs calls)
    for (int i = 0; i < 3; i++) {
        ret = sys_syncfs(fd);
        TEST_ASSERT(ret == 0, "repeated syncfs failed");
    }

    // Case C: Error / Errno Handling
    ret = sys_syncfs(-1);
    TEST_ASSERT(ret == -EBADF, "syncfs(-1) must return -EBADF (-9)");

    sys_close(fd);
    ltm_print("    -> Passed sync & syncfs (Cases A, B, C)\n");
}

// =============================================================================
// 7. timerfd_create, timerfd_settime, timerfd_gettime (Syscall 85, 86, 87)
// =============================================================================
static void test_timerfd_family(void) {
    ltm_print("  [TEST] timerfd family (Syscall 85, 86, 87)...\n");

    // Case A: Happy Path
    int tfd = sys_timerfd_create(CLOCK_MONOTONIC, 0);
    TEST_ASSERT(tfd >= 0, "timerfd_create(CLOCK_MONOTONIC) failed");

    struct linux_itimerspec new_val;
    new_val.it_interval.tv_sec = 1;
    new_val.it_interval.tv_nsec = 0;
    new_val.it_value.tv_sec = 5;
    new_val.it_value.tv_nsec = 0;

    struct linux_itimerspec old_val;
    ltm_memset(&old_val, 0, sizeof(old_val));

    int ret = sys_timerfd_settime(tfd, 0, &new_val, &old_val);
    TEST_ASSERT(ret == 0, "timerfd_settime failed");

    struct linux_itimerspec curr_val;
    ret = sys_timerfd_gettime(tfd, &curr_val);
    TEST_ASSERT(ret == 0, "timerfd_gettime failed");
    TEST_ASSERT(curr_val.it_value.tv_sec > 0 || curr_val.it_value.tv_nsec > 0, "timerfd_gettime returned zero value");

    sys_close(tfd);

    // Case B: Boundary / Edge Cases (TFD_NONBLOCK | TFD_CLOEXEC, old_val NULL, disarm)
    tfd = sys_timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK | TFD_CLOEXEC);
    TEST_ASSERT(tfd >= 0, "timerfd_create(NONBLOCK | CLOEXEC) failed");

    new_val.it_value.tv_sec = 0; // disarm
    new_val.it_value.tv_nsec = 0;
    ret = sys_timerfd_settime(tfd, 0, &new_val, NULL);
    TEST_ASSERT(ret == 0, "timerfd_settime disarm failed");
    sys_close(tfd);

    // Case C: Error / Errno Handling
    ret = sys_timerfd_create(9999, 0);
    TEST_ASSERT(ret == -EINVAL, "timerfd_create(invalid_clockid) must return -EINVAL (-22)");

    ret = sys_timerfd_settime(-1, 0, &new_val, NULL);
    TEST_ASSERT(ret == -EBADF, "timerfd_settime(-1) must return -EBADF (-9)");

    tfd = sys_timerfd_create(CLOCK_MONOTONIC, 0);
    ret = sys_timerfd_settime(tfd, 0, NULL, NULL);
    TEST_ASSERT(ret == -EFAULT, "timerfd_settime(NULL) must return -EFAULT (-14)");

    ret = sys_timerfd_gettime(-1, &curr_val);
    TEST_ASSERT(ret == -EBADF, "timerfd_gettime(-1) must return -EBADF (-9)");

    ret = sys_timerfd_gettime(tfd, NULL);
    TEST_ASSERT(ret == -EFAULT, "timerfd_gettime(NULL) must return -EFAULT (-14)");

    sys_close(tfd);
    ltm_print("    -> Passed timerfd family (Cases A, B, C)\n");
}

// =============================================================================
// 8. clock_nanosleep (Syscall 115)
// =============================================================================
static void test_clock_nanosleep(void) {
    ltm_print("  [TEST] clock_nanosleep (Syscall 115)...\n");

    struct linux_timespec req;
    struct linux_timespec rem;

    // Case A: Happy Path (1 millisecond)
    req.tv_sec = 0;
    req.tv_nsec = 1000000; // 1 ms
    int ret = sys_clock_nanosleep(CLOCK_MONOTONIC, 0, &req, &rem);
    TEST_ASSERT(ret == 0, "clock_nanosleep(1ms) failed");

    // Case B: Boundary / Edge Cases (0 sleep duration, remain=NULL)
    req.tv_sec = 0;
    req.tv_nsec = 0;
    ret = sys_clock_nanosleep(CLOCK_MONOTONIC, 0, &req, NULL);
    TEST_ASSERT(ret == 0, "clock_nanosleep(0) failed");

    // Case C: Error / Errno Handling
    ret = sys_clock_nanosleep(CLOCK_MONOTONIC, 0, NULL, NULL);
    TEST_ASSERT(ret == -EFAULT, "clock_nanosleep(NULL) must return -EFAULT (-14)");

    req.tv_sec = 0;
    req.tv_nsec = 1000000000L; // Invalid nsec >= 10^9
    ret = sys_clock_nanosleep(CLOCK_MONOTONIC, 0, &req, NULL);
    TEST_ASSERT(ret == -EINVAL, "clock_nanosleep(invalid_nsec) must return -EINVAL (-22)");

    req.tv_nsec = 1000;
    ret = sys_clock_nanosleep(9999, 0, &req, NULL);
    TEST_ASSERT(ret == -EINVAL, "clock_nanosleep(invalid_clock) must return -EINVAL (-22)");

    ltm_print("    -> Passed clock_nanosleep (Cases A, B, C)\n");
}

// =============================================================================
// 9. accept4 (Syscall 242)
// =============================================================================
static void test_accept4(void) {
    ltm_print("  [TEST] accept4 (Syscall 242)...\n");

    // Case A: Happy Path / Non-blocking accept check
    // Case C covers invalid fd, not a socket, and invalid flags
    int ret = sys_accept4(-1, NULL, NULL, 0);
    TEST_ASSERT(ret == -EBADF, "accept4(-1) must return -EBADF (-9)");

    int fd = sys_openat(AT_FDCWD, TEST_FILE, O_CREAT | O_RDWR, 0644);
    TEST_ASSERT(fd >= 0, "openat failed for accept4 test");

    ret = sys_accept4(fd, NULL, NULL, 0);
    TEST_ASSERT(ret == -ENOTSOCK || ret == -EINVAL, "accept4(regular_file) must return -ENOTSOCK (-88)");

    ret = sys_accept4(fd, NULL, NULL, 0xffffff);
    TEST_ASSERT(ret == -EINVAL || ret == -ENOTSOCK, "accept4(invalid_flags) must return -EINVAL (-22)");

    // Case B: Boundary flags (SOCK_NONBLOCK | SOCK_CLOEXEC)
    ret = sys_accept4(-1, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    TEST_ASSERT(ret == -EBADF, "accept4(flags) on bad fd must return -EBADF");

    sys_close(fd);
    ltm_print("    -> Passed accept4 (Cases A, B, C)\n");
}

// =============================================================================
// 10. memfd_create (Syscall 279)
// =============================================================================
static void test_memfd_create(void) {
    ltm_print("  [TEST] memfd_create (Syscall 279)...\n");

    // Case A: Happy Path
    int mfd = sys_memfd_create("test_memfd", MFD_CLOEXEC);
    TEST_ASSERT(mfd >= 0, "memfd_create failed");

    const char *data = "MEMFD_STORAGE_DATA";
    size_t len = ltm_strlen(data);
    ssize_t w = sys_write(mfd, data, len);
    TEST_ASSERT(w == (ssize_t)len, "write to memfd failed");

    sys_lseek(mfd, 0, SEEK_SET);
    char buf[32];
    ltm_memset(buf, 0, sizeof(buf));
    ssize_t r = sys_read(mfd, buf, len);
    TEST_ASSERT(r == (ssize_t)len && ltm_memcmp(buf, data, len) == 0, "read from memfd corrupted");

    sys_close(mfd);

    // Case B: Boundary / Edge Cases (empty name, MFD_ALLOW_SEALING)
    mfd = sys_memfd_create("", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    TEST_ASSERT(mfd >= 0, "memfd_create(empty_name) failed");
    sys_close(mfd);

    // Case C: Error / Errno Handling
    int ret = sys_memfd_create(NULL, 0);
    TEST_ASSERT(ret == -EFAULT, "memfd_create(NULL) must return -EFAULT (-14)");

    ret = sys_memfd_create("test", 0xffff0000);
    TEST_ASSERT(ret == -EINVAL, "memfd_create(invalid_flags) must return -EINVAL (-22)");

    ltm_print("    -> Passed memfd_create (Cases A, B, C)\n");
}

// =============================================================================
// 11. copy_file_range (Syscall 285)
// =============================================================================
static void test_copy_file_range(void) {
    ltm_print("  [TEST] copy_file_range (Syscall 285)...\n");

    int fd_src = sys_openat(AT_FDCWD, TEST_FILE, O_CREAT | O_RDWR | O_TRUNC, 0644);
    int fd_dst = sys_openat(AT_FDCWD, TEST_FILE_COPY, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd_src >= 0 && fd_dst >= 0, "openat failed for copy_file_range");

    char payload[512];
    ltm_memset(payload, 'K', sizeof(payload));
    sys_write(fd_src, payload, sizeof(payload));
    sys_lseek(fd_src, 0, SEEK_SET);

    // Case A: Happy Path
    ssize_t copied = sys_copy_file_range(fd_src, NULL, fd_dst, NULL, sizeof(payload), 0);
    TEST_ASSERT(copied == (ssize_t)sizeof(payload), "copy_file_range failed to copy full size");

    sys_lseek(fd_dst, 0, SEEK_SET);
    char check_buf[512];
    sys_read(fd_dst, check_buf, sizeof(check_buf));
    TEST_ASSERT(ltm_memcmp(payload, check_buf, sizeof(payload)) == 0, "copied content mismatch");

    // Case B: Boundary / Edge Cases (len = 0)
    copied = sys_copy_file_range(fd_src, NULL, fd_dst, NULL, 0, 0);
    TEST_ASSERT(copied == 0, "copy_file_range(len=0) must return 0");

    // Case C: Error / Errno Handling
    copied = sys_copy_file_range(-1, NULL, fd_dst, NULL, 100, 0);
    TEST_ASSERT(copied == -EBADF, "copy_file_range(fd_in=-1) must return -EBADF (-9)");

    copied = sys_copy_file_range(fd_src, NULL, -1, NULL, 100, 0);
    TEST_ASSERT(copied == -EBADF, "copy_file_range(fd_out=-1) must return -EBADF (-9)");

    copied = sys_copy_file_range(fd_src, NULL, fd_dst, NULL, 100, 0xff);
    TEST_ASSERT(copied == -EINVAL, "copy_file_range(flags!=0) must return -EINVAL (-22)");

    sys_close(fd_src);
    sys_close(fd_dst);
    ltm_print("    -> Passed copy_file_range (Cases A, B, C)\n");
}

// =============================================================================
// 12. close_range (Syscall 436)
// =============================================================================
static void test_close_range(void) {
    ltm_print("  [TEST] close_range (Syscall 436)...\n");

    int f1 = sys_openat(AT_FDCWD, TEST_FILE, O_RDONLY, 0);
    int f2 = sys_openat(AT_FDCWD, TEST_FILE, O_RDONLY, 0);
    int f3 = sys_openat(AT_FDCWD, TEST_FILE, O_RDONLY, 0);
    TEST_ASSERT(f1 >= 0 && f2 >= 0 && f3 >= 0, "openat failed for close_range test");

    // Case A: Happy Path (close range [f1, f3])
    int ret = sys_close_range((unsigned int)f1, (unsigned int)f3, 0);
    TEST_ASSERT(ret == 0, "close_range failed");

    // Verify all fds are closed
    TEST_ASSERT(sys_close(f1) == -EBADF, "f1 should be closed");
    TEST_ASSERT(sys_close(f2) == -EBADF, "f2 should be closed");
    TEST_ASSERT(sys_close(f3) == -EBADF, "f3 should be closed");

    // Case B: Boundary / Edge Cases (first == last)
    int f4 = sys_openat(AT_FDCWD, TEST_FILE, O_RDONLY, 0);
    TEST_ASSERT(f4 >= 0, "openat failed");
    ret = sys_close_range((unsigned int)f4, (unsigned int)f4, 0);
    TEST_ASSERT(ret == 0, "close_range(single_fd) failed");
    TEST_ASSERT(sys_close(f4) == -EBADF, "f4 should be closed");

    // Case C: Error / Errno Handling
    ret = sys_close_range(100, 10, 0); // first > last
    TEST_ASSERT(ret == -EINVAL, "close_range(first > last) must return -EINVAL (-22)");

    ret = sys_close_range(10, 20, 0xffffff); // invalid flags
    TEST_ASSERT(ret == -EINVAL, "close_range(invalid_flags) must return -EINVAL (-22)");

    ltm_print("    -> Passed close_range (Cases A, B, C)\n");
}

// =============================================================================
// 13. openat2 (Syscall 437)
// =============================================================================
static void test_openat2(void) {
    ltm_print("  [TEST] openat2 (Syscall 437)...\n");

    struct linux_open_how how;
    ltm_memset(&how, 0, sizeof(how));
    how.flags = O_CREAT | O_RDWR | O_TRUNC;
    how.mode = 0644;
    how.resolve = 0;

    // Case A: Happy Path
    int fd = sys_openat2(AT_FDCWD, TEST_FILE, &how, sizeof(how));
    TEST_ASSERT(fd >= 0, "openat2 failed");
    sys_close(fd);

    // Case B: Boundary / Edge Cases (RESOLVE_BENEATH)
    how.flags = O_RDONLY;
    how.mode = 0;
    how.resolve = RESOLVE_BENEATH;
    fd = sys_openat2(AT_FDCWD, TEST_FILE, &how, sizeof(how));
    TEST_ASSERT(fd >= 0, "openat2(RESOLVE_BENEATH) failed");
    sys_close(fd);

    // Case C: Error / Errno Handling
    int ret = sys_openat2(AT_FDCWD, TEST_FILE, &how, sizeof(how) - 1); // Invalid struct size
    TEST_ASSERT(ret == -EINVAL, "openat2(small_size) must return -EINVAL (-22)");

    ret = sys_openat2(AT_FDCWD, TEST_FILE, NULL, sizeof(how));
    TEST_ASSERT(ret == -EFAULT, "openat2(NULL_how) must return -EFAULT (-14)");

    ret = sys_openat2(AT_FDCWD, NULL, &how, sizeof(how));
    TEST_ASSERT(ret == -EFAULT, "openat2(NULL_path) must return -EFAULT (-14)");

    ret = sys_openat2(AT_FDCWD, "/nonexistent_openat2_path_xyz", &how, sizeof(how));
    TEST_ASSERT(ret == -ENOENT, "openat2(nonexistent) must return -ENOENT (-2)");

    ret = sys_openat2(-999, TEST_FILE, &how, sizeof(how));
    TEST_ASSERT(ret == -EBADF, "openat2(invalid_dirfd) must return -EBADF (-9)");

    ltm_print("    -> Passed openat2 (Cases A, B, C)\n");
}

// =============================================================================
// 14. faccessat2 (Syscall 439)
// =============================================================================
static void test_faccessat2(void) {
    ltm_print("  [TEST] faccessat2 (Syscall 439)...\n");

    int fd = sys_openat(AT_FDCWD, TEST_FILE, O_CREAT | O_RDWR, 0644);
    TEST_ASSERT(fd >= 0, "openat failed for faccessat2 test");
    sys_close(fd);

    // Case A: Happy Path
    int ret = sys_faccessat2(AT_FDCWD, TEST_FILE, R_OK | W_OK, 0);
    TEST_ASSERT(ret == 0, "faccessat2(R_OK | W_OK) failed");

    // Case B: Boundary / Edge Cases (F_OK, AT_EACCESS)
    ret = sys_faccessat2(AT_FDCWD, TEST_FILE, F_OK, AT_EACCESS);
    TEST_ASSERT(ret == 0, "faccessat2(F_OK, AT_EACCESS) failed");

    // Case C: Error / Errno Handling
    ret = sys_faccessat2(AT_FDCWD, NULL, F_OK, 0);
    TEST_ASSERT(ret == -EFAULT, "faccessat2(NULL) must return -EFAULT (-14)");

    ret = sys_faccessat2(AT_FDCWD, "/nonexistent_faccessat2_path_xyz", F_OK, 0);
    TEST_ASSERT(ret == -ENOENT, "faccessat2(nonexistent) must return -ENOENT (-2)");

    ret = sys_faccessat2(-999, TEST_FILE, F_OK, 0);
    TEST_ASSERT(ret == -EBADF, "faccessat2(invalid_dirfd) must return -EBADF (-9)");

    ret = sys_faccessat2(AT_FDCWD, TEST_FILE, F_OK, 0xffffff);
    TEST_ASSERT(ret == -EINVAL, "faccessat2(invalid_flags) must return -EINVAL (-22)");

    ltm_print("    -> Passed faccessat2 (Cases A, B, C)\n");
}

// =============================================================================
// Main Test Runner
// =============================================================================
int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    ltm_print("[TEST] test_wave2_syscalls: Executing 3-Way Mandate (Happy, Boundary, Errno)...\n");

    test_flock();
    test_fallocate();
    test_chroot();
    test_pread_pwrite();
    test_signalfd4();
    test_sync_family();
    test_timerfd_family();
    test_clock_nanosleep();
    test_accept4();
    test_memfd_create();
    test_copy_file_range();
    test_close_range();
    test_openat2();
    test_faccessat2();

    // Clean up temporary files
    sys_openat(AT_FDCWD, TEST_FILE, O_CREAT | O_TRUNC, 0644);
    sys_openat(AT_FDCWD, TEST_FILE_COPY, O_CREAT | O_TRUNC, 0644);

    TEST_PASS("test_wave2_syscalls: All Wave 2 syscalls verified across all 3 Mandate cases!");
    return 0;
}
