#include "bootargs.h"

#include "../lib/string.h"

static const char *boot_args_multiboot_cmdline(const uint32_t *mboot_info) {
    if (!mboot_info) return "";
    if ((mboot_info[0] & 0x4u) == 0) return "";
    if (mboot_info[4] == 0) return "";
    return (const char *)(uintptr_t)mboot_info[4];
}

static const char *boot_args_next_token(const char *cursor, const char **end_out) {
    const char *start = cursor;

    while (*start == ' ') start++;
    cursor = start;
    while (*cursor && *cursor != ' ') cursor++;

    if (end_out) *end_out = cursor;
    return start;
}

bool boot_args_has_flag(const char *cmdline, const char *flag) {
    const char *cursor;
    size_t flag_len;

    if (!cmdline || !flag) return false;
    flag_len = strlen(flag);
    cursor = cmdline;

    while (*cursor) {
        const char *end = NULL;
        const char *token = boot_args_next_token(cursor, &end);
        size_t token_len = (size_t)(end - token);
        if (token_len == flag_len && memcmp(token, flag, flag_len) == 0) {
            return true;
        }
        cursor = end;
    }

    return false;
}

bool boot_args_get_value(const char *cmdline, const char *key, char *out, size_t out_len) {
    const char *cursor;
    size_t key_len;

    if (!cmdline || !key || !out || out_len == 0) return false;
    out[0] = '\0';
    key_len = strlen(key);
    cursor = cmdline;

    while (*cursor) {
        const char *end = NULL;
        const char *token = boot_args_next_token(cursor, &end);
        size_t token_len = (size_t)(end - token);

        if (token_len > key_len + 1 &&
            memcmp(token, key, key_len) == 0 &&
            token[key_len] == '=') {
            size_t value_len = token_len - key_len - 1;
            if (value_len >= out_len) value_len = out_len - 1;
            memcpy(out, token + key_len + 1, value_len);
            out[value_len] = '\0';
            return true;
        }

        cursor = end;
    }

    return false;
}

void boot_args_init(boot_args_t *out, const uint32_t *mboot_info) {
    if (!out) return;

    memset(out, 0, sizeof(*out));
    out->cmdline = boot_args_multiboot_cmdline(mboot_info);
    out->safe_mode = boot_args_has_flag(out->cmdline, "safe");
    boot_args_get_value(out->cmdline, "root", out->root_device, sizeof(out->root_device));
    boot_args_get_value(out->cmdline, "rootfstype", out->root_fstype, sizeof(out->root_fstype));
    if (!boot_args_get_value(out->cmdline, "init", out->init_path, sizeof(out->init_path))) {
        strncpy(out->init_path, "/sbin/init", sizeof(out->init_path) - 1);
        out->init_path[sizeof(out->init_path) - 1] = '\0';
    }
}