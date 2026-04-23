#include "bootargs.h"

#include "../lib/string.h"

static char boot_args_cmdline_storage[BOOT_ARGS_CMDLINE_LEN];

static const char *boot_args_multiboot_cmdline(const uint32_t *mboot_info) {
    if (!mboot_info) return "";
    if ((mboot_info[0] & 0x4u) == 0) return "";
    if (mboot_info[4] == 0) return "";
    return (const char *)(uintptr_t)mboot_info[4];
}

static void boot_args_store_cmdline(const uint32_t *mboot_info) {
    const char *cmdline = boot_args_multiboot_cmdline(mboot_info);

    strncpy(boot_args_cmdline_storage, cmdline, sizeof(boot_args_cmdline_storage) - 1);
    boot_args_cmdline_storage[sizeof(boot_args_cmdline_storage) - 1] = '\0';
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
    boot_args_store_cmdline(mboot_info);
    out->cmdline = boot_args_cmdline_storage;
    out->safe_mode = boot_args_has_flag(out->cmdline, "safe");
    boot_args_get_value(out->cmdline, "root", out->root_device, sizeof(out->root_device));
    boot_args_get_value(out->cmdline, "rootfstype", out->root_fstype, sizeof(out->root_fstype));
    if (!boot_args_get_value(out->cmdline, "init", out->init_path, sizeof(out->init_path))) {
        strncpy(out->init_path, "/sbin/init", sizeof(out->init_path) - 1);
        out->init_path[sizeof(out->init_path) - 1] = '\0';
    }

    /* verbose=N — kernel logging verbosity level (0=quiet, 1=info, 2=debug) */
    {
        char vbuf[4] = {0};
        if (boot_args_get_value(out->cmdline, "verbose", vbuf, sizeof(vbuf))) {
            int v = 0;
            const char *p = vbuf;
            while (*p >= '0' && *p <= '9') {
                v = v * 10 + (*p - '0');
                p++;
            }
            if (v > 2) v = 2;
            out->verbose = v;
        } else {
            out->verbose = 0;
        }
    }
    /* kdbg=0xN — initial kernel per-subsystem debug flags */
    {
        char kbuf[12] = {0};
        out->kdbg_flags = 0;
        if (boot_args_get_value(out->cmdline, "kdbg", kbuf, sizeof(kbuf))) {
            uint32_t v = 0;
            const char *p = kbuf;
            if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
            while (*p) {
                char c = *p++;
                uint32_t nibble;
                if (c >= '0' && c <= '9')      nibble = (uint32_t)(c - '0');
                else if (c >= 'a' && c <= 'f') nibble = (uint32_t)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') nibble = (uint32_t)(c - 'A' + 10);
                else break;
                v = (v << 4) | nibble;
            }
            out->kdbg_flags = v;
        }
    }
}

const char *boot_args_cmdline(void) {
    return boot_args_cmdline_storage;
}