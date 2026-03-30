#include "rootfs.h"

#include "../fs/vfs.h"
#include "../lib/stdio.h"
#include "../lib/string.h"

static bool rootfs_map_device_lba(const char *device, uint32_t *start_lba) {
    if (!device || !device[0]) return false;

    if (strcmp(device, "none") == 0 || strcmp(device, "diskless") == 0) {
        *start_lba = 0;
        return true;
    }

    if (strcmp(device, "/dev/hda") == 0 ||
        strcmp(device, "/dev/hda0") == 0 ||
        strcmp(device, "/dev/sda") == 0 ||
        strcmp(device, "/dev/sda0") == 0 ||
        strcmp(device, "/dev/ata0") == 0 ||
        strcmp(device, "/dev/cd0") == 0) {
        *start_lba = 0;
        return true;
    }

    if (strcmp(device, "/dev/hda1") == 0 ||
        strcmp(device, "/dev/sda1") == 0 ||
        strcmp(device, "/dev/ata0p1") == 0) {
        *start_lba = 63;
        return true;
    }

    return false;
}

void rootfs_config_init(rootfs_config_t *cfg) {
    if (!cfg) return;

    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->device, "/dev/hda", sizeof(cfg->device) - 1);
    cfg->start_lba = 0;
    cfg->auto_probe = true;
}

int rootfs_apply_boot_args(rootfs_config_t *cfg, const boot_args_t *args) {
    if (!cfg) return -1;

    if (args && args->root_device[0]) {
        strncpy(cfg->device, args->root_device, sizeof(cfg->device) - 1);
        cfg->device[sizeof(cfg->device) - 1] = '\0';
    }

    if (args && args->root_fstype[0]) {
        strncpy(cfg->fs_name, args->root_fstype, sizeof(cfg->fs_name) - 1);
        cfg->fs_name[sizeof(cfg->fs_name) - 1] = '\0';
        cfg->auto_probe = false;
    }

    if (strcmp(cfg->device, "none") == 0 || strcmp(cfg->device, "diskless") == 0) {
        cfg->diskless = true;
        return 0;
    }

    if (!rootfs_map_device_lba(cfg->device, &cfg->start_lba)) {
        kprintf("[ROOT] Unsupported root device '%s'\n", cfg->device);
        return -1;
    }

    return 0;
}

int rootfs_mount_config(const rootfs_config_t *cfg) {
    uint32_t fallback_lba;

    if (!cfg) return -1;
    if (cfg->diskless) return 0;

    if (!cfg->auto_probe) {
        if (vfs_mount("/", cfg->fs_name, cfg->start_lba) == 0) {
            kprintf("[ROOT] Mounted %s on %s\n", cfg->device, cfg->fs_name);
            return 0;
        }
        return -1;
    }

    if (vfs_mount("/", "biscuitfs", cfg->start_lba) == 0) {
        kprintf("[ROOT] Mounted %s on biscuitfs\n", cfg->device);
        return 0;
    }

    if (vfs_mount("/", "fat16", cfg->start_lba) == 0) {
        kprintf("[ROOT] Mounted %s on fat16\n", cfg->device);
        return 0;
    }

    fallback_lba = 0;
    if (cfg->start_lba == 0 && rootfs_map_device_lba("/dev/hda1", &fallback_lba)) {
        if (vfs_mount("/", "biscuitfs", fallback_lba) == 0) {
            kprintf("[ROOT] Mounted %s via partition fallback on biscuitfs\n", cfg->device);
            return 0;
        }

        if (vfs_mount("/", "fat16", fallback_lba) == 0) {
            kprintf("[ROOT] Mounted %s via partition fallback on fat16\n", cfg->device);
            return 0;
        }
    }

    return -1;
}

void rootfs_ensure_layout(void) {
    static const char *dirs[] = {
        "/bin",
        "/boot",
        "/etc",
        "/lib",
        "/root",
        "/tmp",
        "/usr",
        "/usr/bin",
        "/usr/lib",
        "/var",
        "/var/log",
        "/var/pid",
    };
    int created = 0;

    for (size_t i = 0; i < ARRAY_SIZE(dirs); i++) {
        if (vfs_mkdir(dirs[i]) == 0) created++;
    }

    if (created > 0) {
        kprintf("[ROOT] Ensured %d base directories\n", created);
    }
}