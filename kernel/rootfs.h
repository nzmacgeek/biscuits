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

/* Default geometry assumptions for BlueyOS disk layout.
 * These can be overridden at compile time (e.g. via -D flags or a config header)
 * to match the layout produced by tools/mkbluey_disk.py when using non-default
 * --boot-mb/--root-mb/--swap-mb arguments.
 */
#ifndef BLUEYOS_SECTORS_PER_MB
#define BLUEYOS_SECTORS_PER_MB     2048u
#endif

#ifndef BLUEYOS_BOOT_PARTITION_LBA
#define BLUEYOS_BOOT_PARTITION_LBA 2048u
#endif

#ifndef BLUEYOS_BOOT_PARTITION_MB
#define BLUEYOS_BOOT_PARTITION_MB  32u
#endif

#ifndef BLUEYOS_ROOT_PARTITION_MB
#define BLUEYOS_ROOT_PARTITION_MB  64u
#endif

#define BLUEYOS_ROOT_PARTITION_LBA (BLUEYOS_BOOT_PARTITION_LBA + ((uint32_t)BLUEYOS_BOOT_PARTITION_MB * BLUEYOS_SECTORS_PER_MB))
#define BLUEYOS_SWAP_PARTITION_LBA (BLUEYOS_ROOT_PARTITION_LBA + ((uint32_t)BLUEYOS_ROOT_PARTITION_MB * BLUEYOS_SECTORS_PER_MB))

void rootfs_config_init(rootfs_config_t *cfg);
int  rootfs_apply_boot_args(rootfs_config_t *cfg, const boot_args_t *args);
int  rootfs_mount_config(const rootfs_config_t *cfg);
void rootfs_ensure_layout(void);
int  rootfs_apply_fstab(void);