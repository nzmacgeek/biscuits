#pragma once

#include "../include/types.h"

#define BOOT_ARGS_CMDLINE_LEN 512

typedef struct {
    const char *cmdline;
    bool        safe_mode;
    int         verbose;          /* 0=quiet (default), 1=info, 2=debug   */
    uint32_t    kdbg_flags;       /* kdbg=0xN — initial kernel debug flags */
    char        root_device[32];
    char        root_fstype[16];
    char        init_path[64];
} boot_args_t;

void boot_args_init(boot_args_t *out, const uint32_t *mboot_info);
bool boot_args_has_flag(const char *cmdline, const char *flag);
bool boot_args_get_value(const char *cmdline, const char *key, char *out, size_t out_len);
const char *boot_args_cmdline(void);