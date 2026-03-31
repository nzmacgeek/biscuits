#include "rootfs.h"

#include "../fs/vfs.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "swap.h"

static int rootfs_dbg_log_limited(int *counter, int limit) {
    if (*counter >= limit) return 0;
    (*counter)++;
    return 1;
}

static const char *rootfs_canonical_fs_name(const char *fs_name) {
    if (!fs_name || !fs_name[0]) return fs_name;
    if (strcmp(fs_name, "blueyfs") == 0) return "biscuitfs";
    return fs_name;
}

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
        *start_lba = BLUEYOS_BOOT_PARTITION_LBA;
        return true;
    }

    if (strcmp(device, "/dev/hda2") == 0 ||
        strcmp(device, "/dev/sda2") == 0 ||
        strcmp(device, "/dev/ata0p2") == 0) {
        *start_lba = BLUEYOS_ROOT_PARTITION_LBA;
        return true;
    }

    if (strcmp(device, "/dev/hda3") == 0 ||
        strcmp(device, "/dev/sda3") == 0 ||
        strcmp(device, "/dev/ata0p3") == 0) {
        *start_lba = BLUEYOS_SWAP_PARTITION_LBA;
        return true;
    }

    return false;
}

void rootfs_config_init(rootfs_config_t *cfg) {
    if (!cfg) return;

    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->device, "/dev/hda2", sizeof(cfg->device) - 1);
    strncpy(cfg->fs_name, "biscuitfs", sizeof(cfg->fs_name) - 1);
    cfg->start_lba = BLUEYOS_ROOT_PARTITION_LBA;
    cfg->auto_probe = true;
}

int rootfs_apply_boot_args(rootfs_config_t *cfg, const boot_args_t *args) {
    if (!cfg) return -1;

    if (args && args->root_device[0]) {
        strncpy(cfg->device, args->root_device, sizeof(cfg->device) - 1);
        cfg->device[sizeof(cfg->device) - 1] = '\0';
    }

    if (args && args->root_fstype[0]) {
        strncpy(cfg->fs_name, rootfs_canonical_fs_name(args->root_fstype), sizeof(cfg->fs_name) - 1);
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
    static int dbg_calls;
    uint32_t fallback_lba;

    if (!cfg) return -1;
    if (cfg->diskless) return 0;

    if (rootfs_dbg_log_limited(&dbg_calls, 16)) {
        kprintf("[ROOT DBG] mount_config cfg=%p device='%s' fs='%s' start_lba=%u auto_probe=%u diskless=%u caller=%p\n",
                (const void *)cfg, cfg->device, cfg->fs_name, cfg->start_lba,
                cfg->auto_probe ? 1u : 0u, cfg->diskless ? 1u : 0u,
                __builtin_return_address(0));
    }

    if (!cfg->auto_probe) {
        if (rootfs_dbg_log_limited(&dbg_calls, 16)) {
            kprintf("[ROOT DBG] explicit mount path='/' fs='%s' lba=%u\n",
                    rootfs_canonical_fs_name(cfg->fs_name), cfg->start_lba);
        }
        if (vfs_mount("/", rootfs_canonical_fs_name(cfg->fs_name), cfg->start_lba) == 0) {
            kprintf("[ROOT] Mounted %s on %s\n", cfg->device, rootfs_canonical_fs_name(cfg->fs_name));
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
    if (cfg->start_lba == 0 && rootfs_map_device_lba("/dev/hda2", &fallback_lba)) {
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
        "/sbin",
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

static int rootfs_parse_fstab_line(char *line) {
    char *fields[6];
    int count = 0;
    uint32_t start_lba;
    const char *fs_name;

    while (*line == ' ' || *line == '\t') line++;
    if (!*line || *line == '#') return 0;

    for (char *cursor = line; *cursor && count < 6; ) {
        while (*cursor == ' ' || *cursor == '\t') {
            *cursor++ = '\0';
        }
        if (!*cursor) break;
        fields[count++] = cursor;
        while (*cursor && *cursor != ' ' && *cursor != '\t') cursor++;
    }

    if (count < 3) return 0;

    fs_name = rootfs_canonical_fs_name(fields[2]);
    if (strcmp(fields[1], "/") == 0) return 0;

    if (!rootfs_map_device_lba(fields[0], &start_lba)) {
        kprintf("[FSTAB] Unsupported device '%s'\n", fields[0]);
        return -1;
    }

    if (strcmp(fields[2], "swap") == 0) {
        swap_init(start_lba, SWAP_MAX_PAGES);
        return 1;
    }

    if (vfs_mount(fields[1], fs_name, start_lba) == 0) {
        kprintf("[FSTAB] Mounted %s on %s (%s)\n", fields[0], fields[1], fs_name);
        return 1;
    }

    kprintf("[FSTAB] Failed to mount %s on %s (%s)\n", fields[0], fields[1], fs_name);
    return -1;
}

int rootfs_apply_fstab(void) {
    int fd;
    int nread;
    int applied = 0;
    char chunk[128];
    char line[256];
    size_t line_len = 0;

    fd = vfs_open("/etc/fstab", VFS_O_RDONLY);
    if (fd < 0) {
        kprintf("[FSTAB] /etc/fstab not found - leaving extra mounts alone\n");
        return 0;
    }

    while ((nread = vfs_read(fd, (uint8_t *)chunk, sizeof(chunk))) > 0) {
        for (int index = 0; index < nread; index++) {
            char ch = chunk[index];

            if (ch == '\r') continue;
            if (ch == '\n') {
                line[line_len] = '\0';
                if (rootfs_parse_fstab_line(line) > 0) applied++;
                line_len = 0;
                continue;
            }

            if (line_len + 1 < sizeof(line)) {
                line[line_len++] = ch;
            }
        }
    }

    if (line_len) {
        line[line_len] = '\0';
        if (rootfs_parse_fstab_line(line) > 0) applied++;
    }

    vfs_close(fd);
    kprintf("[FSTAB] Applied %d mount directives\n", applied);
    return applied;
}