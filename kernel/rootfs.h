#pragma once

#include "../include/types.h"
#include "bootargs.h"

typedef struct {
    char     device[32];
    char     fs_name[16];
    uint32_t start_lba;
    bool     auto_probe;
    bool     diskless;
} rootfs_config_t;

#define BLUEYOS_BOOT_PARTITION_LBA 2048u
#define BLUEYOS_BOOT_PARTITION_MB  32u
#define BLUEYOS_ROOT_PARTITION_LBA (BLUEYOS_BOOT_PARTITION_LBA + (BLUEYOS_BOOT_PARTITION_MB * 2048u))
#define BLUEYOS_ROOT_PARTITION_MB  64u
#define BLUEYOS_SWAP_PARTITION_LBA (BLUEYOS_ROOT_PARTITION_LBA + (BLUEYOS_ROOT_PARTITION_MB * 2048u))

void rootfs_config_init(rootfs_config_t *cfg);
int  rootfs_apply_boot_args(rootfs_config_t *cfg, const boot_args_t *args);
int  rootfs_mount_config(const rootfs_config_t *cfg);
void rootfs_ensure_layout(void);
int  rootfs_apply_fstab(void);