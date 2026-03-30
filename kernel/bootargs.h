#pragma once

#include "../include/types.h"

typedef struct {
    const char *cmdline;
    bool        safe_mode;
    char        root_device[32];
    char        root_fstype[16];
} boot_args_t;

void boot_args_init(boot_args_t *out, const uint32_t *mboot_info);
bool boot_args_has_flag(const char *cmdline, const char *flag);
bool boot_args_get_value(const char *cmdline, const char *key, char *out, size_t out_len);