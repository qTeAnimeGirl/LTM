#ifndef SANDBOX_H
#define SANDBOX_H

#include "ltm.h"

/* Initialize the sandbox with the given root directory (typically $PWD) */
int sandbox_init(const char *root_dir);
int sandbox_init_standalone(void);

/* Check if the given guest path is a virtual path (/proc/..., /dev/...) */
bool sandbox_is_virtual_path(const char *path);

/* Resolves and canonicalizes a guest path to a safe host path within the sandbox root.
 * Returns 0 on success, or a negative Linux errno (-LINUX_EACCES, -LINUX_ENOENT, etc.).
 * If the path is a virtual path, returns 1 (handled by virtual vfs). */
int sandbox_resolve_path(int dirfd, const char *guest_path, char *host_path_buf, size_t buf_sz, int flags);

/* Opens a virtual file (/proc/..., /dev/...) and returns a real host file descriptor,
 * or a negative Linux errno on failure. */
int sandbox_open_virtual(const char *path, int flags, mode_t mode);

/* Reads the target of a virtual symlink (such as /proc/self/exe) */
ssize_t sandbox_virtual_readlink(const char *path, char *buf, size_t bufsiz);

/* Populates stat structure for virtual files */
int sandbox_virtual_stat(const char *path, struct stat *st);

#endif /* SANDBOX_H */
