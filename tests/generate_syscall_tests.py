#!/usr/bin/env python3
import os
import sys

OUTPUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "syscalls")
os.makedirs(OUTPUT_DIR, exist_ok=True)

TESTS = {}

# 1. 032 flock
TESTS["test_sys_032_flock.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "flock_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    // Case A: Happy Path
    int ret = sys_flock(fd, LOCK_EX);
    TEST_ASSERT(ret == 0, "flock(LOCK_EX) failed");
    ret = sys_flock(fd, LOCK_UN);
    TEST_ASSERT(ret == 0, "flock(LOCK_UN) failed");

    // Case B: Boundary / Edge Cases
    ret = sys_flock(fd, LOCK_SH | LOCK_NB);
    TEST_ASSERT(ret == 0, "flock(LOCK_SH | LOCK_NB) failed");
    sys_flock(fd, LOCK_UN);

    // Case C: Error / Errno Handling
    ret = sys_flock(-1, LOCK_EX);
    TEST_ASSERT(ret == -EBADF, "flock(-1) must return -EBADF");
    ret = sys_flock(fd, 9999);
    TEST_ASSERT(ret == -EINVAL, "flock(invalid) must return -EINVAL");

    sys_close(fd);
    TEST_PASS("test_sys_032_flock passed!");
    return 0;
}
"""

# 2. 043 statfs
TESTS["test_sys_043_statfs.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    struct linux_statfs sfs;
    ltm_memset(&sfs, 0, sizeof(sfs));

    // Case A: Happy Path
    int ret = sys_statfs(".", &sfs);
    TEST_ASSERT(ret == 0, "statfs('.') failed");
    TEST_ASSERT(sfs.f_bsize > 0, "statfs.f_bsize > 0");
    TEST_ASSERT(sfs.f_blocks > 0, "statfs.f_blocks > 0");

    // Case B: Boundary / Edge Cases
    ret = sys_statfs("/", &sfs);
    TEST_ASSERT(ret == 0, "statfs('/') failed");

    // Case C: Error / Errno Handling
    ret = sys_statfs(NULL, &sfs);
    TEST_ASSERT(ret == -EFAULT, "statfs(NULL) must return -EFAULT");
    ret = sys_statfs("/nonexistent_statfs_xyz", &sfs);
    TEST_ASSERT(ret == -ENOENT, "statfs(nonexistent) must return -ENOENT");

    TEST_PASS("test_sys_043_statfs passed!");
    return 0;
}
"""

# 3. 044 fstatfs
TESTS["test_sys_044_fstatfs.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "fstatfs_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    struct linux_statfs sfs;
    ltm_memset(&sfs, 0, sizeof(sfs));

    // Case A: Happy Path
    int ret = sys_fstatfs(fd, &sfs);
    TEST_ASSERT(ret == 0, "fstatfs(fd) failed");
    TEST_ASSERT(sfs.f_bsize > 0, "fstatfs.f_bsize > 0");

    // Case B: Boundary / Edge Cases (stdin fd 0)
    ret = sys_fstatfs(0, &sfs);
    TEST_ASSERT(ret == 0 || ret == -EBADF, "fstatfs(0) unexpected return");

    // Case C: Error / Errno Handling
    ret = sys_fstatfs(-1, &sfs);
    TEST_ASSERT(ret == -EBADF, "fstatfs(-1) must return -EBADF");
    ret = sys_fstatfs(fd, NULL);
    TEST_ASSERT(ret == -EFAULT, "fstatfs(fd, NULL) must return -EFAULT");

    sys_close(fd);
    TEST_PASS("test_sys_044_fstatfs passed!");
    return 0;
}
"""

# 4. 045 truncate
TESTS["test_sys_045_truncate.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "truncate_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    char dummy[256];
    ltm_memset(dummy, 'A', sizeof(dummy));
    sys_write(fd, dummy, sizeof(dummy));
    sys_close(fd);

    // Case A: Happy Path
    int ret = sys_truncate(tmp, 100);
    TEST_ASSERT(ret == 0, "truncate(100) failed");

    // Case B: Boundary / Edge Cases
    ret = sys_truncate(tmp, 0);
    TEST_ASSERT(ret == 0, "truncate(0) failed");

    // Case C: Error / Errno Handling
    ret = sys_truncate(NULL, 10);
    TEST_ASSERT(ret == -EFAULT, "truncate(NULL) must return -EFAULT");
    ret = sys_truncate("nonexistent_truncate_xyz", 10);
    TEST_ASSERT(ret == -ENOENT, "truncate(nonexistent) must return -ENOENT");
    ret = sys_truncate(tmp, -5);
    TEST_ASSERT(ret == -EINVAL, "truncate(negative) must return -EINVAL");

    TEST_PASS("test_sys_045_truncate passed!");
    return 0;
}
"""

# 5. 046 ftruncate
TESTS["test_sys_046_ftruncate.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "ftruncate_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    char dummy[256];
    ltm_memset(dummy, 'B', sizeof(dummy));
    sys_write(fd, dummy, sizeof(dummy));

    // Case A: Happy Path
    int ret = sys_ftruncate(fd, 50);
    TEST_ASSERT(ret == 0, "ftruncate(50) failed");

    // Case B: Boundary / Edge Cases
    ret = sys_ftruncate(fd, 0);
    TEST_ASSERT(ret == 0, "ftruncate(0) failed");

    // Case C: Error / Errno Handling
    ret = sys_ftruncate(-1, 10);
    TEST_ASSERT(ret == -EBADF, "ftruncate(-1) must return -EBADF");
    ret = sys_ftruncate(fd, -5);
    TEST_ASSERT(ret == -EINVAL, "ftruncate(negative) must return -EINVAL");

    sys_close(fd);
    TEST_PASS("test_sys_046_ftruncate passed!");
    return 0;
}
"""

# 6. 047 fallocate
TESTS["test_sys_047_fallocate.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "fallocate_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    // Case A: Happy Path
    int ret = sys_fallocate(fd, 0, 0, 4096);
    TEST_ASSERT(ret == 0, "fallocate(0, 0, 4096) failed");

    // Case B: Boundary / Edge Cases
    ret = sys_fallocate(fd, FALLOC_FL_KEEP_SIZE, 0, 8192);
    TEST_ASSERT(ret == 0, "fallocate(KEEP_SIZE) failed");

    // Case C: Error / Errno Handling
    ret = sys_fallocate(-1, 0, 0, 4096);
    TEST_ASSERT(ret == -EBADF, "fallocate(-1) must return -EBADF");
    ret = sys_fallocate(fd, 0, 0, -100);
    TEST_ASSERT(ret == -EINVAL, "fallocate(len<=0) must return -EINVAL");
    ret = sys_fallocate(fd, 0, -5, 4096);
    TEST_ASSERT(ret == -EINVAL, "fallocate(offset<0) must return -EINVAL");

    sys_close(fd);
    TEST_PASS("test_sys_047_fallocate passed!");
    return 0;
}
"""

# 7. 051 chroot
TESTS["test_sys_051_chroot.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int ret = sys_chroot(".");
    TEST_ASSERT(ret == 0, "chroot('.') failed");

    // Case B: Boundary / Edge Cases
    ret = sys_chroot("/");
    TEST_ASSERT(ret == 0, "chroot('/') failed");

    // Case C: Error / Errno Handling
    ret = sys_chroot(NULL);
    TEST_ASSERT(ret == -EFAULT, "chroot(NULL) must return -EFAULT");
    ret = sys_chroot("/nonexistent_chroot_path_xyz");
    TEST_ASSERT(ret == -ENOENT, "chroot(nonexistent) must return -ENOENT");

    TEST_PASS("test_sys_051_chroot passed!");
    return 0;
}
"""

# 8. 052 fchmod
TESTS["test_sys_052_fchmod.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "fchmod_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    // Case A: Happy Path
    int ret = sys_fchmod(fd, 0600);
    TEST_ASSERT(ret == 0, "fchmod(0600) failed");

    // Case B: Boundary / Edge Cases
    ret = sys_fchmod(fd, 0700);
    TEST_ASSERT(ret == 0, "fchmod(0700) failed");

    // Case C: Error / Errno Handling
    ret = sys_fchmod(-1, 0644);
    TEST_ASSERT(ret == -EBADF, "fchmod(-1) must return -EBADF");

    sys_close(fd);
    TEST_PASS("test_sys_052_fchmod passed!");
    return 0;
}
"""

# 9. 053 fchmodat
TESTS["test_sys_053_fchmodat.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "fchmodat_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_close(fd);

    // Case A: Happy Path
    int ret = sys_fchmodat(AT_FDCWD, tmp, 0644, 0);
    TEST_ASSERT(ret == 0, "fchmodat failed");

    // Case B: Boundary / Edge Cases
    ret = sys_fchmodat(AT_FDCWD, tmp, 0777, 0);
    TEST_ASSERT(ret == 0, "fchmodat(0777) failed");

    // Case C: Error / Errno Handling
    ret = sys_fchmodat(AT_FDCWD, NULL, 0644, 0);
    TEST_ASSERT(ret == -EFAULT, "fchmodat(NULL) must return -EFAULT");
    ret = sys_fchmodat(AT_FDCWD, "nonexistent_fchmodat", 0644, 0);
    TEST_ASSERT(ret == -ENOENT, "fchmodat(nonexistent) must return -ENOENT");
    ret = sys_fchmodat(-999, tmp, 0644, 0);
    TEST_ASSERT(ret == -EBADF, "fchmodat(invalid_dirfd) must return -EBADF");

    TEST_PASS("test_sys_053_fchmodat passed!");
    return 0;
}
"""

# 10. 054 fchown
TESTS["test_sys_054_fchown.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "fchown_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    // Case A: Happy Path (-1, -1 no-op)
    int ret = sys_fchown(fd, -1, -1);
    TEST_ASSERT(ret == 0, "fchown(-1, -1) failed");

    // Case B: Boundary / Edge Cases
    ret = sys_fchown(fd, 0, 0);
    TEST_ASSERT(ret == 0, "fchown(0, 0) failed");

    // Case C: Error / Errno Handling
    ret = sys_fchown(-1, 0, 0);
    TEST_ASSERT(ret == -EBADF, "fchown(-1) must return -EBADF");

    sys_close(fd);
    TEST_PASS("test_sys_054_fchown passed!");
    return 0;
}
"""

# 11. 055 fchownat
TESTS["test_sys_055_fchownat.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "fchownat_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_close(fd);

    // Case A: Happy Path (-1, -1 no-op)
    int ret = sys_fchownat(AT_FDCWD, tmp, -1, -1, 0);
    TEST_ASSERT(ret == 0, "fchownat(-1, -1) failed");

    // Case B: Boundary / Edge Cases
    ret = sys_fchownat(AT_FDCWD, tmp, 0, 0, 0);
    TEST_ASSERT(ret == 0, "fchownat(0, 0) failed");

    // Case C: Error / Errno Handling
    ret = sys_fchownat(AT_FDCWD, NULL, 0, 0, 0);
    TEST_ASSERT(ret == -EFAULT, "fchownat(NULL) must return -EFAULT");
    ret = sys_fchownat(AT_FDCWD, "nonexistent_fchownat", 0, 0, 0);
    TEST_ASSERT(ret == -ENOENT, "fchownat(nonexistent) must return -ENOENT");
    ret = sys_fchownat(-999, tmp, 0, 0, 0);
    TEST_ASSERT(ret == -EBADF, "fchownat(invalid_dirfd) must return -EBADF");

    TEST_PASS("test_sys_055_fchownat passed!");
    return 0;
}
"""

# 12. 056 openat
TESTS["test_sys_056_openat.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "openat_test.tmp";

    // Case A: Happy Path
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat(CREAT|RDWR) failed");
    sys_close(fd);

    // Case B: Boundary / Edge Cases
    fd = sys_openat(AT_FDCWD, tmp, O_RDONLY, 0);
    TEST_ASSERT(fd >= 0, "openat(RDONLY) failed");
    sys_close(fd);

    // Case C: Error / Errno Handling
    int ret = sys_openat(AT_FDCWD, NULL, O_RDONLY, 0);
    TEST_ASSERT(ret == -EFAULT, "openat(NULL) must return -EFAULT");
    ret = sys_openat(AT_FDCWD, "/nonexistent_openat_path_xyz", O_RDONLY, 0);
    TEST_ASSERT(ret == -ENOENT, "openat(nonexistent) must return -ENOENT");
    ret = sys_openat(-999, tmp, O_RDONLY, 0);
    TEST_ASSERT(ret == -EBADF, "openat(invalid_dirfd) must return -EBADF");

    TEST_PASS("test_sys_056_openat passed!");
    return 0;
}
"""

# 13. 057 close
TESTS["test_sys_057_close.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "close_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    // Case A: Happy Path
    int ret = sys_close(fd);
    TEST_ASSERT(ret == 0, "close(fd) failed");

    // Case B: Boundary / Edge Cases (double close)
    ret = sys_close(fd);
    TEST_ASSERT(ret == -EBADF, "double close must return -EBADF");

    // Case C: Error / Errno Handling
    ret = sys_close(-1);
    TEST_ASSERT(ret == -EBADF, "close(-1) must return -EBADF");

    TEST_PASS("test_sys_057_close passed!");
    return 0;
}
"""

# 14. 062 lseek
TESTS["test_sys_062_lseek.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "lseek_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    char dummy[100];
    ltm_memset(dummy, 'X', sizeof(dummy));
    sys_write(fd, dummy, sizeof(dummy));

    // Case A: Happy Path
    off_t pos = sys_lseek(fd, 25, SEEK_SET);
    TEST_ASSERT(pos == 25, "lseek(SEEK_SET) failed");

    // Case B: Boundary / Edge Cases
    pos = sys_lseek(fd, 0, SEEK_END);
    TEST_ASSERT(pos == 100, "lseek(SEEK_END) failed");
    pos = sys_lseek(fd, 0, SEEK_CUR);
    TEST_ASSERT(pos == 100, "lseek(SEEK_CUR) failed");

    // Case C: Error / Errno Handling
    pos = sys_lseek(-1, 0, SEEK_SET);
    TEST_ASSERT(pos == -EBADF, "lseek(-1) must return -EBADF");
    pos = sys_lseek(fd, -10, SEEK_SET);
    TEST_ASSERT(pos == -EINVAL, "lseek(negative) must return -EINVAL");
    pos = sys_lseek(fd, 0, 9999);
    TEST_ASSERT(pos == -EINVAL, "lseek(invalid_whence) must return -EINVAL");

    sys_close(fd);
    TEST_PASS("test_sys_062_lseek passed!");
    return 0;
}
"""

# 15. 063 read
TESTS["test_sys_063_read.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "read_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    const char *msg = "HELLO_READ";
    sys_write(fd, msg, 10);
    sys_lseek(fd, 0, SEEK_SET);

    // Case A: Happy Path
    char buf[32];
    ltm_memset(buf, 0, sizeof(buf));
    ssize_t r = sys_read(fd, buf, 10);
    TEST_ASSERT(r == 10, "read failed");
    TEST_ASSERT(ltm_memcmp(buf, msg, 10) == 0, "read data corrupted");

    // Case B: Boundary / Edge Cases
    r = sys_read(fd, buf, 0);
    TEST_ASSERT(r == 0, "read(count=0) must return 0");

    // Case C: Error / Errno Handling
    r = sys_read(-1, buf, 10);
    TEST_ASSERT(r == -EBADF, "read(-1) must return -EBADF");
    r = sys_read(fd, NULL, 10);
    TEST_ASSERT(r == -EFAULT, "read(NULL) must return -EFAULT");

    sys_close(fd);
    TEST_PASS("test_sys_063_read passed!");
    return 0;
}
"""

# 16. 064 write
TESTS["test_sys_064_write.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "write_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    // Case A: Happy Path
    const char *msg = "HELLO_WRITE";
    ssize_t w = sys_write(fd, msg, 11);
    TEST_ASSERT(w == 11, "write failed");

    // Case B: Boundary / Edge Cases
    w = sys_write(fd, msg, 0);
    TEST_ASSERT(w == 0, "write(count=0) must return 0");

    // Case C: Error / Errno Handling
    w = sys_write(-1, msg, 11);
    TEST_ASSERT(w == -EBADF, "write(-1) must return -EBADF");
    w = sys_write(fd, NULL, 11);
    TEST_ASSERT(w == -EFAULT, "write(NULL) must return -EFAULT");

    sys_close(fd);
    TEST_PASS("test_sys_064_write passed!");
    return 0;
}
"""

# 17. 067 pread64
TESTS["test_sys_067_pread64.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "pread64_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_pwrite64(fd, "PREAD_TARGET", 12, 50);

    // Case A: Happy Path
    char buf[32];
    ltm_memset(buf, 0, sizeof(buf));
    ssize_t r = sys_pread64(fd, buf, 12, 50);
    TEST_ASSERT(r == 12, "pread64 failed");
    TEST_ASSERT(ltm_memcmp(buf, "PREAD_TARGET", 12) == 0, "pread64 data corrupted");
    off_t pos = sys_lseek(fd, 0, SEEK_CUR);
    TEST_ASSERT(pos == 0, "pread64 changed offset");

    // Case B: Boundary / Edge Cases
    r = sys_pread64(fd, buf, 0, 50);
    TEST_ASSERT(r == 0, "pread64(0) must return 0");

    // Case C: Error / Errno Handling
    r = sys_pread64(-1, buf, 12, 50);
    TEST_ASSERT(r == -EBADF, "pread64(-1) must return -EBADF");
    r = sys_pread64(fd, NULL, 12, 50);
    TEST_ASSERT(r == -EFAULT, "pread64(NULL) must return -EFAULT");
    r = sys_pread64(fd, buf, 12, -1);
    TEST_ASSERT(r == -EINVAL, "pread64(negative) must return -EINVAL");

    sys_close(fd);
    TEST_PASS("test_sys_067_pread64 passed!");
    return 0;
}
"""

# 18. 068 pwrite64
TESTS["test_sys_068_pwrite64.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "pwrite64_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    // Case A: Happy Path
    ssize_t w = sys_pwrite64(fd, "PWRITE_TEST", 11, 100);
    TEST_ASSERT(w == 11, "pwrite64 failed");
    off_t pos = sys_lseek(fd, 0, SEEK_CUR);
    TEST_ASSERT(pos == 0, "pwrite64 changed offset");

    // Case B: Boundary / Edge Cases
    w = sys_pwrite64(fd, "PWRITE_TEST", 0, 100);
    TEST_ASSERT(w == 0, "pwrite64(0) must return 0");

    // Case C: Error / Errno Handling
    w = sys_pwrite64(-1, "PWRITE_TEST", 11, 100);
    TEST_ASSERT(w == -EBADF, "pwrite64(-1) must return -EBADF");
    w = sys_pwrite64(fd, NULL, 11, 100);
    TEST_ASSERT(w == -EFAULT, "pwrite64(NULL) must return -EFAULT");
    w = sys_pwrite64(fd, "PWRITE_TEST", 11, -5);
    TEST_ASSERT(w == -EINVAL, "pwrite64(negative) must return -EINVAL");

    sys_close(fd);
    TEST_PASS("test_sys_068_pwrite64 passed!");
    return 0;
}
"""

# 19. 074 signalfd4
TESTS["test_sys_074_signalfd4.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    uint64_t mask = (1ULL << 14);

    // Case A: Happy Path
    int sfd = sys_signalfd4(-1, &mask, 8, 0);
    TEST_ASSERT(sfd >= 0, "signalfd4 failed");
    sys_close(sfd);

    // Case B: Boundary / Edge Cases
    sfd = sys_signalfd4(-1, &mask, 8, SFD_NONBLOCK | SFD_CLOEXEC);
    TEST_ASSERT(sfd >= 0, "signalfd4(NONBLOCK|CLOEXEC) failed");
    sys_close(sfd);

    // Case C: Error / Errno Handling
    int ret = sys_signalfd4(-1, NULL, 8, 0);
    TEST_ASSERT(ret == -EFAULT, "signalfd4(NULL) must return -EFAULT");
    ret = sys_signalfd4(-1, &mask, 4, 0);
    TEST_ASSERT(ret == -EINVAL, "signalfd4(sizemask!=8) must return -EINVAL");
    ret = sys_signalfd4(-999, &mask, 8, 0);
    TEST_ASSERT(ret == -EBADF, "signalfd4(bad_ufd) must return -EBADF");

    TEST_PASS("test_sys_074_signalfd4 passed!");
    return 0;
}
"""

# 20. 079 newfstatat
TESTS["test_sys_079_newfstatat.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "fstatat_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_write(fd, "STAT_TEST", 9);
    sys_close(fd);

    char statbuf[256];
    ltm_memset(statbuf, 0, sizeof(statbuf));

    // Case A: Happy Path
    int ret = sys_syscall4(SYS_newfstatat, AT_FDCWD, (long)tmp, (long)statbuf, 0);
    TEST_ASSERT(ret == 0, "newfstatat failed");

    // Case B: Boundary / Edge Cases
    ret = sys_syscall4(SYS_newfstatat, AT_FDCWD, (long)tmp, (long)statbuf, AT_SYMLINK_NOFOLLOW);
    TEST_ASSERT(ret == 0, "newfstatat(NOFOLLOW) failed");

    // Case C: Error / Errno Handling
    ret = sys_syscall4(SYS_newfstatat, AT_FDCWD, (long)NULL, (long)statbuf, 0);
    TEST_ASSERT(ret == -EFAULT, "newfstatat(NULL_path) must return -EFAULT");
    ret = sys_syscall4(SYS_newfstatat, AT_FDCWD, (long)"nonexistent_stat", (long)statbuf, 0);
    TEST_ASSERT(ret == -ENOENT, "newfstatat(nonexistent) must return -ENOENT");
    ret = sys_syscall4(SYS_newfstatat, -999, (long)tmp, (long)statbuf, 0);
    TEST_ASSERT(ret == -EBADF, "newfstatat(bad_dirfd) must return -EBADF");

    TEST_PASS("test_sys_079_newfstatat passed!");
    return 0;
}
"""

# 21. 081 sync
TESTS["test_sys_081_sync.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    sys_sync();

    // Case B: Boundary / Edge Cases (sequential calls)
    sys_sync();
    sys_sync();

    // Case C: Register preservation test
    register long r19 __asm__("x19") = 0x12345678;
    sys_sync();
    TEST_ASSERT(r19 == 0x12345678, "sync corrupted callee-saved register x19");

    TEST_PASS("test_sys_081_sync passed!");
    return 0;
}
"""

# 22. 082 fsync
TESTS["test_sys_082_fsync.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "fsync_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    // Case A: Happy Path
    int ret = sys_fsync(fd);
    TEST_ASSERT(ret == 0, "fsync(fd) failed");

    // Case B: Boundary / Edge Cases (sync after write)
    sys_write(fd, "FLUSH", 5);
    ret = sys_fsync(fd);
    TEST_ASSERT(ret == 0, "fsync after write failed");

    // Case C: Error / Errno Handling
    ret = sys_fsync(-1);
    TEST_ASSERT(ret == -EBADF, "fsync(-1) must return -EBADF");

    sys_close(fd);
    TEST_PASS("test_sys_082_fsync passed!");
    return 0;
}
"""

# 23. 083 fdatasync
TESTS["test_sys_083_fdatasync.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "fdatasync_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    // Case A: Happy Path
    int ret = sys_fdatasync(fd);
    TEST_ASSERT(ret == 0, "fdatasync(fd) failed");

    // Case B: Boundary / Edge Cases
    sys_write(fd, "DATA", 4);
    ret = sys_fdatasync(fd);
    TEST_ASSERT(ret == 0, "fdatasync after write failed");

    // Case C: Error / Errno Handling
    ret = sys_fdatasync(-1);
    TEST_ASSERT(ret == -EBADF, "fdatasync(-1) must return -EBADF");

    sys_close(fd);
    TEST_PASS("test_sys_083_fdatasync passed!");
    return 0;
}
"""

# 24. 085 timerfd_create
TESTS["test_sys_085_timerfd_create.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int tfd = sys_timerfd_create(CLOCK_MONOTONIC, 0);
    TEST_ASSERT(tfd >= 0, "timerfd_create failed");
    sys_close(tfd);

    // Case B: Boundary / Edge Cases
    tfd = sys_timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK | TFD_CLOEXEC);
    TEST_ASSERT(tfd >= 0, "timerfd_create(NONBLOCK|CLOEXEC) failed");
    sys_close(tfd);

    // Case C: Error / Errno Handling
    int ret = sys_timerfd_create(9999, 0);
    TEST_ASSERT(ret == -EINVAL, "timerfd_create(invalid_clock) must return -EINVAL");

    TEST_PASS("test_sys_085_timerfd_create passed!");
    return 0;
}
"""

# 25. 086 timerfd_settime
TESTS["test_sys_086_timerfd_settime.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    int tfd = sys_timerfd_create(CLOCK_MONOTONIC, 0);
    TEST_ASSERT(tfd >= 0, "timerfd_create failed");

    struct linux_itimerspec new_val;
    new_val.it_interval.tv_sec = 0;
    new_val.it_interval.tv_nsec = 0;
    new_val.it_value.tv_sec = 10;
    new_val.it_value.tv_nsec = 0;

    struct linux_itimerspec old_val;

    // Case A: Happy Path
    int ret = sys_timerfd_settime(tfd, 0, &new_val, &old_val);
    TEST_ASSERT(ret == 0, "timerfd_settime failed");

    // Case B: Boundary / Edge Cases (disarm, old_val NULL)
    new_val.it_value.tv_sec = 0;
    ret = sys_timerfd_settime(tfd, 0, &new_val, NULL);
    TEST_ASSERT(ret == 0, "timerfd_settime disarm failed");

    // Case C: Error / Errno Handling
    ret = sys_timerfd_settime(-1, 0, &new_val, NULL);
    TEST_ASSERT(ret == -EBADF, "timerfd_settime(-1) must return -EBADF");
    ret = sys_timerfd_settime(tfd, 0, NULL, NULL);
    TEST_ASSERT(ret == -EFAULT, "timerfd_settime(NULL) must return -EFAULT");

    sys_close(tfd);
    TEST_PASS("test_sys_086_timerfd_settime passed!");
    return 0;
}
"""

# 26. 087 timerfd_gettime
TESTS["test_sys_087_timerfd_gettime.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    int tfd = sys_timerfd_create(CLOCK_MONOTONIC, 0);
    TEST_ASSERT(tfd >= 0, "timerfd_create failed");

    struct linux_itimerspec new_val;
    new_val.it_interval.tv_sec = 1;
    new_val.it_interval.tv_nsec = 0;
    new_val.it_value.tv_sec = 5;
    new_val.it_value.tv_nsec = 0;
    sys_timerfd_settime(tfd, 0, &new_val, NULL);

    // Case A: Happy Path
    struct linux_itimerspec curr;
    int ret = sys_timerfd_gettime(tfd, &curr);
    TEST_ASSERT(ret == 0, "timerfd_gettime failed");
    TEST_ASSERT(curr.it_value.tv_sec > 0 || curr.it_value.tv_nsec > 0, "timerfd_gettime value zero");

    // Case B: Boundary / Edge Cases (disarmed timer)
    new_val.it_value.tv_sec = 0;
    sys_timerfd_settime(tfd, 0, &new_val, NULL);
    ret = sys_timerfd_gettime(tfd, &curr);
    TEST_ASSERT(ret == 0, "timerfd_gettime on disarmed failed");

    // Case C: Error / Errno Handling
    ret = sys_timerfd_gettime(-1, &curr);
    TEST_ASSERT(ret == -EBADF, "timerfd_gettime(-1) must return -EBADF");
    ret = sys_timerfd_gettime(tfd, NULL);
    TEST_ASSERT(ret == -EFAULT, "timerfd_gettime(NULL) must return -EFAULT");

    sys_close(tfd);
    TEST_PASS("test_sys_087_timerfd_gettime passed!");
    return 0;
}
"""

# 27. 093 exit
TESTS["test_sys_093_exit.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    TEST_PASS("test_sys_093_exit passed!");
    sys_exit(0);
    return 0;
}
"""

# 28. 115 clock_nanosleep
TESTS["test_sys_115_clock_nanosleep.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    struct linux_timespec req;
    struct linux_timespec rem;

    // Case A: Happy Path (1ms sleep)
    req.tv_sec = 0;
    req.tv_nsec = 1000000;
    int ret = sys_clock_nanosleep(CLOCK_MONOTONIC, 0, &req, &rem);
    TEST_ASSERT(ret == 0, "clock_nanosleep(1ms) failed");

    // Case B: Boundary / Edge Cases (0ns sleep, remain=NULL)
    req.tv_sec = 0;
    req.tv_nsec = 0;
    ret = sys_clock_nanosleep(CLOCK_MONOTONIC, 0, &req, NULL);
    TEST_ASSERT(ret == 0, "clock_nanosleep(0) failed");

    // Case C: Error / Errno Handling
    ret = sys_clock_nanosleep(CLOCK_MONOTONIC, 0, NULL, NULL);
    TEST_ASSERT(ret == -EFAULT, "clock_nanosleep(NULL) must return -EFAULT");
    req.tv_nsec = 1000000000L;
    ret = sys_clock_nanosleep(CLOCK_MONOTONIC, 0, &req, NULL);
    TEST_ASSERT(ret == -EINVAL, "clock_nanosleep(invalid_nsec) must return -EINVAL");
    req.tv_nsec = 1000;
    ret = sys_clock_nanosleep(9999, 0, &req, NULL);
    TEST_ASSERT(ret == -EINVAL, "clock_nanosleep(invalid_clock) must return -EINVAL");

    TEST_PASS("test_sys_115_clock_nanosleep passed!");
    return 0;
}
"""

# 29. 122 sched_setaffinity
TESTS["test_sys_122_sched_setaffinity.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    unsigned long mask = 0;
    sys_sched_getaffinity(0, sizeof(mask), &mask);

    // Case A: Happy Path
    int ret = sys_sched_setaffinity(0, sizeof(mask), &mask);
    TEST_ASSERT(ret == 0, "sched_setaffinity failed");

    // Case B: Boundary / Edge Cases
    unsigned long mask2 = 1;
    ret = sys_sched_setaffinity(0, sizeof(mask2), &mask2);
    TEST_ASSERT(ret == 0, "sched_setaffinity(mask=1) failed");

    // Case C: Error / Errno Handling
    ret = sys_sched_setaffinity(0, 0, &mask);
    TEST_ASSERT(ret == -EINVAL, "sched_setaffinity(len=0) must return -EINVAL");
    ret = sys_sched_setaffinity(0, sizeof(mask), NULL);
    TEST_ASSERT(ret == -EFAULT, "sched_setaffinity(NULL) must return -EFAULT");

    TEST_PASS("test_sys_122_sched_setaffinity passed!");
    return 0;
}
"""

# 30. 123 sched_getaffinity
TESTS["test_sys_123_sched_getaffinity.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    unsigned long mask = 0;

    // Case A: Happy Path
    int ret = sys_sched_getaffinity(0, sizeof(mask), &mask);
    TEST_ASSERT(ret >= 0, "sched_getaffinity failed");
    TEST_ASSERT(mask != 0, "mask != 0");

    // Case B: Boundary / Edge Cases
    unsigned long big_mask[16];
    ret = sys_sched_getaffinity(0, sizeof(big_mask), big_mask);
    TEST_ASSERT(ret >= 0, "sched_getaffinity(big_mask) failed");

    // Case C: Error / Errno Handling
    ret = sys_sched_getaffinity(0, 0, &mask);
    TEST_ASSERT(ret == -EINVAL, "sched_getaffinity(len=0) must return -EINVAL");
    ret = sys_sched_getaffinity(0, sizeof(mask), NULL);
    TEST_ASSERT(ret == -EFAULT, "sched_getaffinity(NULL) must return -EFAULT");
    ret = sys_sched_getaffinity(-9999, sizeof(mask), &mask);
    TEST_ASSERT(ret == -ESRCH || ret == -EINVAL, "sched_getaffinity(bad_pid) must return -ESRCH");

    TEST_PASS("test_sys_123_sched_getaffinity passed!");
    return 0;
}
"""

# 31. 124 sched_yield
TESTS["test_sys_124_sched_yield.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int ret = sys_sched_yield();
    TEST_ASSERT(ret == 0, "sched_yield failed");

    // Case B: Boundary / Edge Cases (repeated yields)
    for (int i = 0; i < 5; i++) {
        ret = sys_sched_yield();
        TEST_ASSERT(ret == 0, "repeated sched_yield failed");
    }

    // Case C: Register preservation
    register long r20 __asm__("x20") = 0xabcdef01;
    sys_sched_yield();
    TEST_ASSERT(r20 == 0xabcdef01, "sched_yield corrupted x20");

    TEST_PASS("test_sys_124_sched_yield passed!");
    return 0;
}
"""

# 32. 153 times
TESTS["test_sys_153_times.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    struct linux_tms t;
    ltm_memset(&t, 0, sizeof(t));

    // Case A: Happy Path
    long ret = sys_times(&t);
    TEST_ASSERT(ret >= 0, "times(&t) failed");

    // Case B: Boundary / Edge Cases (NULL allowed)
    long ret_null = sys_times(NULL);
    TEST_ASSERT(ret_null >= 0, "times(NULL) failed");

    // Case C: Error / Errno Handling
    ret = sys_times((struct linux_tms *)(void *)1);
    TEST_ASSERT(ret == -EFAULT, "times(bad_ptr) must return -EFAULT");

    TEST_PASS("test_sys_153_times passed!");
    return 0;
}
"""

# 33. 154 setpgid
TESTS["test_sys_154_setpgid.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int ret = sys_setpgid(0, 0);
    TEST_ASSERT(ret >= 0 || ret == -EACCES || ret == -EPERM, "setpgid(0, 0) unexpected error");

    // Case B: Boundary / Edge Cases
    int my_pid = sys_getpid();
    int pgid = sys_getpgid(0);
    ret = sys_setpgid(my_pid, pgid);
    TEST_ASSERT(ret >= 0 || ret == -EACCES || ret == -EPERM, "setpgid(my_pid, pgid) failed");

    // Case C: Error / Errno Handling
    ret = sys_setpgid(-999, 0);
    TEST_ASSERT(ret == -EINVAL || ret == -ESRCH, "setpgid(-999) must return -EINVAL or -ESRCH");

    TEST_PASS("test_sys_154_setpgid passed!");
    return 0;
}
"""

# 34. 155 getpgid
TESTS["test_sys_155_getpgid.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int pgid = sys_getpgid(0);
    TEST_ASSERT(pgid > 0, "getpgid(0) > 0");

    // Case B: Boundary / Edge Cases
    int my_pid = sys_getpid();
    int pgid2 = sys_getpgid(my_pid);
    TEST_ASSERT(pgid == pgid2, "getpgid(my_pid) == getpgid(0)");

    // Case C: Error / Errno Handling
    int ret = sys_getpgid(-1);
    TEST_ASSERT(ret == -EINVAL || ret == -ESRCH, "getpgid(-1) must return -EINVAL or -ESRCH");

    TEST_PASS("test_sys_155_getpgid passed!");
    return 0;
}
"""

# 35. 156 getsid
TESTS["test_sys_156_getsid.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int sid = sys_getsid(0);
    TEST_ASSERT(sid > 0, "getsid(0) > 0");

    // Case B: Boundary / Edge Cases
    int my_pid = sys_getpid();
    int sid2 = sys_getsid(my_pid);
    TEST_ASSERT(sid == sid2, "getsid(my_pid) == getsid(0)");

    // Case C: Error / Errno Handling
    int ret = sys_getsid(-1);
    TEST_ASSERT(ret == -EINVAL || ret == -ESRCH, "getsid(-1) must return -EINVAL or -ESRCH");

    TEST_PASS("test_sys_156_getsid passed!");
    return 0;
}
"""

# 36. 157 setsid
TESTS["test_sys_157_setsid.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int ret = sys_setsid();
    TEST_ASSERT(ret > 0 || ret == -EPERM, "setsid() unexpected return");

    // Case B: Boundary / Edge Cases (repeated call)
    int ret2 = sys_setsid();
    TEST_ASSERT(ret2 == -EPERM, "repeated setsid must return -EPERM");

    // Case C: Verify sid valid
    int sid = sys_getsid(0);
    TEST_ASSERT(sid > 0, "getsid > 0");

    TEST_PASS("test_sys_157_setsid passed!");
    return 0;
}
"""

# 37. 160 uname
TESTS["test_sys_160_uname.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    char ubuf[512];
    ltm_memset(ubuf, 0, sizeof(ubuf));

    // Case A: Happy Path
    int ret = sys_syscall1(SYS_uname, (long)ubuf);
    TEST_ASSERT(ret == 0, "uname failed");
    TEST_ASSERT(ltm_strstr(ubuf, "Linux") != NULL, "uname sysname != Linux");

    // Case B: Boundary / Edge Cases
    TEST_ASSERT(ltm_strlen(ubuf) > 0, "uname non-empty");

    // Case C: Error / Errno Handling
    ret = sys_syscall1(SYS_uname, (long)NULL);
    TEST_ASSERT(ret == -EFAULT, "uname(NULL) must return -EFAULT");

    TEST_PASS("test_sys_160_uname passed!");
    return 0;
}
"""

# 38. 165 getrusage
TESTS["test_sys_165_getrusage.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    struct linux_rusage ru;
    ltm_memset(&ru, 0, sizeof(ru));

    // Case A: Happy Path
    int ret = sys_getrusage(RUSAGE_SELF, &ru);
    TEST_ASSERT(ret == 0, "getrusage(RUSAGE_SELF) failed");

    // Case B: Boundary / Edge Cases
    ret = sys_getrusage(RUSAGE_CHILDREN, &ru);
    TEST_ASSERT(ret == 0, "getrusage(RUSAGE_CHILDREN) failed");

    // Case C: Error / Errno Handling
    ret = sys_getrusage(99999, &ru);
    TEST_ASSERT(ret == -EINVAL, "getrusage(invalid_who) must return -EINVAL");
    ret = sys_getrusage(RUSAGE_SELF, NULL);
    TEST_ASSERT(ret == -EFAULT, "getrusage(NULL) must return -EFAULT");

    TEST_PASS("test_sys_165_getrusage passed!");
    return 0;
}
"""

# 39. 166 umask
TESTS["test_sys_166_umask.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int old_mask = sys_umask(022);
    TEST_ASSERT(old_mask >= 0, "umask(022) failed");

    // Case B: Boundary / Edge Cases
    int prev = sys_umask(0777);
    TEST_ASSERT(prev == 022, "umask(0777) returned wrong mask");
    sys_umask(old_mask);

    // Case C: Error / Upper bits masked
    int upper = sys_umask(077777);
    TEST_ASSERT((upper & ~0777) == 0, "umask returned upper bits");
    sys_umask(old_mask);

    TEST_PASS("test_sys_166_umask passed!");
    return 0;
}
"""

# 40. 172 getpid
TESTS["test_sys_172_getpid.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int pid = sys_getpid();
    TEST_ASSERT(pid > 0, "getpid > 0");

    // Case B: Boundary / Consistency
    int pid2 = sys_getpid();
    TEST_ASSERT(pid == pid2, "getpid consistent");

    // Case C: Register preservation
    register long r21 __asm__("x21") = 0x99887766;
    sys_getpid();
    TEST_ASSERT(r21 == 0x99887766, "getpid corrupted x21");

    TEST_PASS("test_sys_172_getpid passed!");
    return 0;
}
"""

# 41. 174 getuid
TESTS["test_sys_174_getuid.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int uid = (int)sys_syscall0(SYS_getuid);
    TEST_ASSERT(uid == 0, "getuid == 0 (root in container jail)");

    // Case B: Consistency
    int uid2 = (int)sys_syscall0(SYS_getuid);
    TEST_ASSERT(uid == uid2, "getuid consistent");

    // Case C: Register preservation
    register long r22 __asm__("x22") = 0x55443322;
    sys_syscall0(SYS_getuid);
    TEST_ASSERT(r22 == 0x55443322, "getuid corrupted x22");

    TEST_PASS("test_sys_174_getuid passed!");
    return 0;
}
"""

# 42. 176 getgid
TESTS["test_sys_176_getgid.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int gid = (int)sys_syscall0(SYS_getgid);
    TEST_ASSERT(gid == 0, "getgid == 0 (root in container jail)");

    // Case B: Consistency
    int gid2 = (int)sys_syscall0(SYS_getgid);
    TEST_ASSERT(gid == gid2, "getgid consistent");

    // Case C: Register preservation
    register long r23 __asm__("x23") = 0x11223344;
    sys_syscall0(SYS_getgid);
    TEST_ASSERT(r23 == 0x11223344, "getgid corrupted x23");

    TEST_PASS("test_sys_176_getgid passed!");
    return 0;
}
"""

# 43. 213 readahead
TESTS["test_sys_213_readahead.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "readahead_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    char dummy[4096];
    ltm_memset(dummy, 'R', sizeof(dummy));
    sys_write(fd, dummy, sizeof(dummy));

    // Case A: Happy Path
    ssize_t ret = sys_readahead(fd, 0, 1024);
    TEST_ASSERT(ret == 0, "readahead failed");

    // Case B: Boundary / Edge Cases
    ret = sys_readahead(fd, 0, 0);
    TEST_ASSERT(ret == 0, "readahead(count=0) must return 0");

    // Case C: Error / Errno Handling
    ret = sys_readahead(-1, 0, 1024);
    TEST_ASSERT(ret == -EBADF, "readahead(-1) must return -EBADF");

    sys_close(fd);
    TEST_PASS("test_sys_213_readahead passed!");
    return 0;
}
"""

# 44. 214 brk
TESTS["test_sys_214_brk.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (query and expand)
    void *cur = sys_brk(NULL);
    TEST_ASSERT(cur != MAP_FAILED && cur != NULL, "sys_brk(0) failed");

    uintptr_t expand_sz = 64 * 1024;
    void *target = (void *)((uintptr_t)cur + expand_sz);
    void *new_brk = sys_brk(target);
    TEST_ASSERT(new_brk >= target, "sys_brk expand failed");

    // Case B: Boundary / Integrity
    uint32_t *u = (uint32_t *)cur;
    u[0] = 0x12345678;
    TEST_ASSERT(u[0] == 0x12345678, "brk memory write corrupted");

    // Case C: Shrink
    void *shrunk = sys_brk(cur);
    TEST_ASSERT(shrunk != MAP_FAILED, "sys_brk shrink failed");

    TEST_PASS("test_sys_214_brk passed!");
    return 0;
}
"""

# 45. 215 munmap
TESTS["test_sys_215_munmap.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    size_t sz = 64 * 1024;
    void *p = sys_mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST_ASSERT(p != MAP_FAILED, "mmap failed");

    // Case A: Happy Path
    int ret = sys_munmap(p, sz);
    TEST_ASSERT(ret == 0, "munmap failed");

    // Case B: Boundary / Edge Cases (partial unmap)
    p = sys_mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST_ASSERT(p != MAP_FAILED, "mmap 2 failed");
    ret = sys_munmap(p, 16384);
    TEST_ASSERT(ret == 0, "munmap partial failed");
    sys_munmap((void *)((uintptr_t)p + 16384), sz - 16384);

    // Case C: Error / Errno Handling
    ret = sys_munmap(NULL, sz);
    TEST_ASSERT(ret == -EINVAL, "munmap(NULL) must return -EINVAL");
    ret = sys_munmap(p, 0);
    TEST_ASSERT(ret == -EINVAL, "munmap(len=0) must return -EINVAL");

    TEST_PASS("test_sys_215_munmap passed!");
    return 0;
}
"""

# 46. 222 mmap
TESTS["test_sys_222_mmap.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    size_t sz = 64 * 1024;

    // Case A: Happy Path
    void *p = sys_mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST_ASSERT(p != MAP_FAILED && p != NULL, "mmap failed");
    char *c = (char *)p;
    c[0] = 'M';
    c[sz - 1] = 'Z';
    TEST_ASSERT(c[0] == 'M' && c[sz - 1] == 'Z', "mmap write corrupted");
    sys_munmap(p, sz);

    // Case B: Boundary / Edge Cases (PROT_READ only)
    p = sys_mmap(NULL, 16384, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST_ASSERT(p != MAP_FAILED, "mmap(PROT_READ) failed");
    sys_munmap(p, 16384);

    // Case C: Error / Errno Handling
    p = sys_mmap(NULL, 0, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST_ASSERT(p == MAP_FAILED, "mmap(len=0) must return MAP_FAILED");

    TEST_PASS("test_sys_222_mmap passed!");
    return 0;
}
"""

# 47. 233 madvise
TESTS["test_sys_233_madvise.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    size_t sz = 64 * 1024;
    void *addr = sys_mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST_ASSERT(addr != MAP_FAILED, "mmap failed");

    // Case A: Happy Path
    int ret = sys_madvise(addr, sz, MADV_NORMAL);
    TEST_ASSERT(ret == 0, "madvise(NORMAL) failed");
    ret = sys_madvise(addr, sz, MADV_WILLNEED);
    TEST_ASSERT(ret == 0, "madvise(WILLNEED) failed");

    // Case B: Boundary / Edge Cases
    ret = sys_madvise(addr, sz, MADV_DONTNEED);
    TEST_ASSERT(ret == 0, "madvise(DONTNEED) failed");

    // Case C: Error / Errno Handling
    ret = sys_madvise(NULL, sz, MADV_NORMAL);
    TEST_ASSERT(ret == -EINVAL || ret == -ENOMEM, "madvise(NULL) must return -EINVAL or -ENOMEM");
    ret = sys_madvise(addr, sz, -999);
    TEST_ASSERT(ret == -EINVAL, "madvise(invalid_advice) must return -EINVAL");

    sys_munmap(addr, sz);
    TEST_PASS("test_sys_233_madvise passed!");
    return 0;
}
"""

# 48. 242 accept4
TESTS["test_sys_242_accept4.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path / Bad fd return
    int ret = sys_accept4(-1, NULL, NULL, 0);
    TEST_ASSERT(ret == -EBADF, "accept4(-1) must return -EBADF");

    // Case B: Boundary flags
    ret = sys_accept4(-1, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    TEST_ASSERT(ret == -EBADF, "accept4(flags) on -1 must return -EBADF");

    // Case C: Regular file descriptor
    const char *tmp = "accept4_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    ret = sys_accept4(fd, NULL, NULL, 0);
    TEST_ASSERT(ret == -ENOTSOCK || ret == -EINVAL, "accept4(file) must return -ENOTSOCK");
    ret = sys_accept4(fd, NULL, NULL, 0xffffff);
    TEST_ASSERT(ret == -EINVAL || ret == -ENOTSOCK, "accept4(invalid_flags) must return -EINVAL");
    sys_close(fd);

    TEST_PASS("test_sys_242_accept4 passed!");
    return 0;
}
"""

# 49. 267 syncfs
TESTS["test_sys_267_syncfs.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "syncfs_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    // Case A: Happy Path
    int ret = sys_syncfs(fd);
    TEST_ASSERT(ret == 0, "syncfs(fd) failed");

    // Case B: Boundary / Edge Cases
    for (int i = 0; i < 3; i++) {
        ret = sys_syncfs(fd);
        TEST_ASSERT(ret == 0, "repeated syncfs failed");
    }

    // Case C: Error / Errno Handling
    ret = sys_syncfs(-1);
    TEST_ASSERT(ret == -EBADF, "syncfs(-1) must return -EBADF");

    sys_close(fd);
    TEST_PASS("test_sys_267_syncfs passed!");
    return 0;
}
"""

# 50. 278 getrandom
TESTS["test_sys_278_getrandom.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    char buf[64];
    ltm_memset(buf, 0, sizeof(buf));

    // Case A: Happy Path
    ssize_t ret = sys_getrandom(buf, 32, 0);
    TEST_ASSERT(ret == 32, "getrandom failed to read 32 bytes");
    bool has_nonzero = false;
    for (int i = 0; i < 32; i++) {
        if (buf[i] != 0) has_nonzero = true;
    }
    TEST_ASSERT(has_nonzero, "getrandom returned all zeros");

    // Case B: Boundary / Edge Cases
    ret = sys_getrandom(buf, 0, 0);
    TEST_ASSERT(ret == 0, "getrandom(buflen=0) must return 0");

    // Case C: Error / Errno Handling
    ret = sys_getrandom(NULL, 32, 0);
    TEST_ASSERT(ret == -EFAULT, "getrandom(NULL) must return -EFAULT");
    ret = sys_getrandom(buf, 32, 0xffffff);
    TEST_ASSERT(ret == -EINVAL, "getrandom(invalid_flags) must return -EINVAL");

    TEST_PASS("test_sys_278_getrandom passed!");
    return 0;
}
"""

# 51. 279 memfd_create
TESTS["test_sys_279_memfd_create.c"] = """#include "../include/ltm_test.h"

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
"""

# 52. 285 copy_file_range
TESTS["test_sys_285_copy_file_range.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *src = "cfr_src.tmp";
    const char *dst = "cfr_dst.tmp";
    int fd_src = sys_openat(AT_FDCWD, src, O_CREAT | O_RDWR | O_TRUNC, 0644);
    int fd_dst = sys_openat(AT_FDCWD, dst, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd_src >= 0 && fd_dst >= 0, "openat failed");

    char payload[256];
    ltm_memset(payload, 'Z', sizeof(payload));
    sys_write(fd_src, payload, sizeof(payload));
    sys_lseek(fd_src, 0, SEEK_SET);

    // Case A: Happy Path
    ssize_t copied = sys_copy_file_range(fd_src, NULL, fd_dst, NULL, sizeof(payload), 0);
    TEST_ASSERT(copied == (ssize_t)sizeof(payload), "copy_file_range failed");
    sys_lseek(fd_dst, 0, SEEK_SET);
    char check[256];
    sys_read(fd_dst, check, sizeof(check));
    TEST_ASSERT(ltm_memcmp(payload, check, sizeof(payload)) == 0, "copied content mismatch");

    // Case B: Boundary / Edge Cases
    copied = sys_copy_file_range(fd_src, NULL, fd_dst, NULL, 0, 0);
    TEST_ASSERT(copied == 0, "copy_file_range(0) must return 0");

    // Case C: Error / Errno Handling
    copied = sys_copy_file_range(-1, NULL, fd_dst, NULL, 100, 0);
    TEST_ASSERT(copied == -EBADF, "copy_file_range(fd_in=-1) must return -EBADF");
    copied = sys_copy_file_range(fd_src, NULL, -1, NULL, 100, 0);
    TEST_ASSERT(copied == -EBADF, "copy_file_range(fd_out=-1) must return -EBADF");
    copied = sys_copy_file_range(fd_src, NULL, fd_dst, NULL, 100, 0xff);
    TEST_ASSERT(copied == -EINVAL, "copy_file_range(flags!=0) must return -EINVAL");

    sys_close(fd_src);
    sys_close(fd_dst);
    TEST_PASS("test_sys_285_copy_file_range passed!");
    return 0;
}
"""

# 53. 436 close_range
TESTS["test_sys_436_close_range.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "close_range_test.tmp";
    int f1 = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR, 0644);
    int f2 = sys_openat(AT_FDCWD, tmp, O_RDONLY, 0);
    int f3 = sys_openat(AT_FDCWD, tmp, O_RDONLY, 0);
    TEST_ASSERT(f1 >= 0 && f2 >= 0 && f3 >= 0, "openat failed");

    // Case A: Happy Path
    int ret = sys_close_range((unsigned int)f1, (unsigned int)f3, 0);
    TEST_ASSERT(ret == 0, "close_range failed");
    TEST_ASSERT(sys_close(f1) == -EBADF, "f1 must be closed");
    TEST_ASSERT(sys_close(f2) == -EBADF, "f2 must be closed");
    TEST_ASSERT(sys_close(f3) == -EBADF, "f3 must be closed");

    // Case B: Boundary / Edge Cases
    int f4 = sys_openat(AT_FDCWD, tmp, O_RDONLY, 0);
    TEST_ASSERT(f4 >= 0, "openat failed");
    ret = sys_close_range((unsigned int)f4, (unsigned int)f4, 0);
    TEST_ASSERT(ret == 0, "close_range(single_fd) failed");
    TEST_ASSERT(sys_close(f4) == -EBADF, "f4 must be closed");

    // Case C: Error / Errno Handling
    ret = sys_close_range(100, 10, 0);
    TEST_ASSERT(ret == -EINVAL, "close_range(first > last) must return -EINVAL");
    ret = sys_close_range(10, 20, 0xffffff);
    TEST_ASSERT(ret == -EINVAL, "close_range(invalid_flags) must return -EINVAL");

    TEST_PASS("test_sys_436_close_range passed!");
    return 0;
}
"""

# 54. 437 openat2
TESTS["test_sys_437_openat2.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "openat2_test.tmp";
    struct linux_open_how how;
    ltm_memset(&how, 0, sizeof(how));
    how.flags = O_CREAT | O_RDWR | O_TRUNC;
    how.mode = 0644;
    how.resolve = 0;

    // Case A: Happy Path
    int fd = sys_openat2(AT_FDCWD, tmp, &how, sizeof(how));
    TEST_ASSERT(fd >= 0, "openat2 failed");
    sys_close(fd);

    // Case B: Boundary / Edge Cases
    how.flags = O_RDONLY;
    how.mode = 0;
    how.resolve = RESOLVE_BENEATH;
    fd = sys_openat2(AT_FDCWD, tmp, &how, sizeof(how));
    TEST_ASSERT(fd >= 0, "openat2(RESOLVE_BENEATH) failed");
    sys_close(fd);

    // Case C: Error / Errno Handling
    int ret = sys_openat2(AT_FDCWD, tmp, &how, sizeof(how) - 1);
    TEST_ASSERT(ret == -EINVAL, "openat2(small_size) must return -EINVAL");
    ret = sys_openat2(AT_FDCWD, tmp, NULL, sizeof(how));
    TEST_ASSERT(ret == -EFAULT, "openat2(NULL_how) must return -EFAULT");
    ret = sys_openat2(AT_FDCWD, NULL, &how, sizeof(how));
    TEST_ASSERT(ret == -EFAULT, "openat2(NULL_path) must return -EFAULT");
    ret = sys_openat2(AT_FDCWD, "nonexistent_openat2", &how, sizeof(how));
    TEST_ASSERT(ret == -ENOENT, "openat2(nonexistent) must return -ENOENT");
    ret = sys_openat2(-999, tmp, &how, sizeof(how));
    TEST_ASSERT(ret == -EBADF, "openat2(bad_dirfd) must return -EBADF");

    TEST_PASS("test_sys_437_openat2 passed!");
    return 0;
}
"""

# 55. 439 faccessat2
TESTS["test_sys_439_faccessat2.c"] = """#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "faccessat2_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_close(fd);

    // Case A: Happy Path
    int ret = sys_faccessat2(AT_FDCWD, tmp, R_OK | W_OK, 0);
    TEST_ASSERT(ret == 0, "faccessat2 failed");

    // Case B: Boundary / Edge Cases
    ret = sys_faccessat2(AT_FDCWD, tmp, F_OK, AT_EACCESS);
    TEST_ASSERT(ret == 0, "faccessat2(AT_EACCESS) failed");

    // Case C: Error / Errno Handling
    ret = sys_faccessat2(AT_FDCWD, NULL, F_OK, 0);
    TEST_ASSERT(ret == -EFAULT, "faccessat2(NULL) must return -EFAULT");
    ret = sys_faccessat2(AT_FDCWD, "nonexistent_faccessat2", F_OK, 0);
    TEST_ASSERT(ret == -ENOENT, "faccessat2(nonexistent) must return -ENOENT");
    ret = sys_faccessat2(-999, tmp, F_OK, 0);
    TEST_ASSERT(ret == -EBADF, "faccessat2(bad_dirfd) must return -EBADF");
    ret = sys_faccessat2(AT_FDCWD, tmp, F_OK, 0xffffff);
    TEST_ASSERT(ret == -EINVAL, "faccessat2(invalid_flags) must return -EINVAL");

    TEST_PASS("test_sys_439_faccessat2 passed!");
    return 0;
}
"""

count = 0
for filename, content in TESTS.items():
    filepath = os.path.join(OUTPUT_DIR, filename)
    with open(filepath, "w") as f:
        f.write(content)
    count += 1

print(f"[+] Successfully generated {count} standalone syscall test suites in {OUTPUT_DIR}")
