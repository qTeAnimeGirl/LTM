#ifndef LTM_ROOTFS_INSTALLER_H
#define LTM_ROOTFS_INSTALLER_H

int ltm_install_arch_rootfs(const char *target_dir);
int ltm_install_alpine_rootfs(const char *target_dir);
int rootfs_install(const char *flavor, const char *target_dir);

#endif /* LTM_ROOTFS_INSTALLER_H */
