#include "include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    ltm_print("[TEST] test_file_io: Testing openat, write, lseek, read, close in sandbox...\n");

    const char *test_filename = "ltm_io_test_file.tmp";
    const char *msg1 = "Hello, LTM File I/O Sandbox Test! Token=9876543210\n";
    size_t msg1_len = ltm_strlen(msg1);

    // 1. Create and write
    ltm_print("  Step 1: Creating file via openat...\n");
    int fd = sys_openat(AT_FDCWD, test_filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat(O_CREAT | O_WRONLY | O_TRUNC) failed");

    ltm_print("  Step 2: Writing data...\n");
    ssize_t written = sys_write(fd, msg1, msg1_len);
    TEST_ASSERT(written == (ssize_t)msg1_len, "sys_write did not write expected number of bytes");

    ltm_print("  Step 3: Closing file descriptor...\n");
    int close_res = sys_close(fd);
    TEST_ASSERT(close_res == 0, "sys_close failed");

    // 2. Re-open read/write and seek
    ltm_print("  Step 4: Re-opening file with O_RDWR...\n");
    fd = sys_openat(AT_FDCWD, test_filename, O_RDWR, 0);
    TEST_ASSERT(fd >= 0, "openat(O_RDWR) failed");

    ltm_print("  Step 5: Testing lseek to offset 7...\n");
    off_t pos = sys_lseek(fd, 7, SEEK_SET);
    TEST_ASSERT(pos == 7, "sys_lseek(SEEK_SET, 7) returned invalid offset");

    char token_buf[4];
    ltm_memset(token_buf, 0, sizeof(token_buf));
    ssize_t nread = sys_read(fd, token_buf, 3);
    TEST_ASSERT(nread == 3, "sys_read did not read 3 bytes");
    TEST_ASSERT(ltm_memcmp(token_buf, "LTM", 3) == 0, "sys_read data mismatch (expected 'LTM')");

    // 3. Append data at SEEK_END
    ltm_print("  Step 6: Seeking to end and appending data...\n");
    pos = sys_lseek(fd, 0, SEEK_END);
    TEST_ASSERT(pos == (off_t)msg1_len, "sys_lseek(SEEK_END) returned incorrect file size");

    const char *msg2 = "AppendedLine\n";
    size_t msg2_len = ltm_strlen(msg2);
    written = sys_write(fd, msg2, msg2_len);
    TEST_ASSERT(written == (ssize_t)msg2_len, "sys_write append failed");

    // 4. Seek back to 0 and read full content
    ltm_print("  Step 7: Seeking back to start and verifying full content...\n");
    pos = sys_lseek(fd, 0, SEEK_SET);
    TEST_ASSERT(pos == 0, "sys_lseek(SEEK_SET, 0) failed");

    char full_buf[128];
    ltm_memset(full_buf, 0, sizeof(full_buf));
    nread = sys_read(fd, full_buf, sizeof(full_buf));
    TEST_ASSERT(nread == (ssize_t)(msg1_len + msg2_len), "Full read length mismatch");

    TEST_ASSERT(ltm_strncmp(full_buf, msg1, msg1_len) == 0, "Initial content corrupted");
    TEST_ASSERT(ltm_strncmp(full_buf + msg1_len, msg2, msg2_len) == 0, "Appended content corrupted");

    close_res = sys_close(fd);
    TEST_ASSERT(close_res == 0, "Final sys_close failed");

    TEST_PASS("test_file_io: All file operations (openat, write, lseek, read, close) verified!");
    return 0;
}
