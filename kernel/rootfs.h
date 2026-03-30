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

void rootfs_config_init(rootfs_config_t *cfg);
int  rootfs_apply_boot_args(rootfs_config_t *cfg, const boot_args_t *args);
int  rootfs_mount_config(const rootfs_config_t *cfg);
void rootfs_ensure_layout(void);