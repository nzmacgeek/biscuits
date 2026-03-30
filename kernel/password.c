// BlueyOS Password Hashing Helpers (PBKDF2-SHA256)
// "A little extra effort keeps the biscuit safe." - Bandit
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../lib/string.h"
#include "sha256.h"
#include "password.h"
#include "timer.h"
#include "process.h"
#include "rtc.h"

#define HMAC_BLOCK_SIZE 64

static const char hex_chars[] = "0123456789abcdef";

static int from_hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static int hex_to_bytes(const char *hex, uint8_t *out, size_t out_len) {
    if (!hex || !out) return -1;
    for (size_t i = 0; i < out_len; i++) {
        int hi = from_hex(hex[i * 2]);
        int lo = from_hex(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

static int parse_uint32(const char *s, uint32_t *out) {
    if (!s || !*s || !out) return -1;
    uint32_t value = 0;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9') return -1;
        uint32_t digit = (uint32_t)(*p - '0');
        if (value > (0xFFFFFFFFu - digit) / 10u) return -1;
        value = value * 10u + digit;
    }
    *out = value;
    return 0;
}

static void bytes_to_hex(const uint8_t *in, size_t len, char *out) {
    for (size_t i = 0; i < len; i++) {
        out[i * 2]     = hex_chars[(in[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex_chars[in[i] & 0xF];
    }
    out[len * 2] = '\0';
}

static void hmac_sha256(const uint8_t *key, size_t key_len,
                        const uint8_t *data, size_t data_len,
                        uint8_t out[SHA256_DIGEST_SIZE]) {
    uint8_t key_block[HMAC_BLOCK_SIZE];
    uint8_t inner_hash[SHA256_DIGEST_SIZE];
    sha256_ctx_t ctx;

    if (key_len > HMAC_BLOCK_SIZE) {
        sha256(key, key_len, inner_hash);
        key = inner_hash;
        key_len = SHA256_DIGEST_SIZE;
    }

    memset(key_block, 0, sizeof(key_block));
    memcpy(key_block, key, key_len);

    for (int i = 0; i < HMAC_BLOCK_SIZE; i++) key_block[i] ^= 0x36;
    sha256_init(&ctx);
    sha256_update(&ctx, key_block, HMAC_BLOCK_SIZE);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, inner_hash);

    for (int i = 0; i < HMAC_BLOCK_SIZE; i++) key_block[i] ^= 0x36 ^ 0x5c;
    sha256_init(&ctx);
    sha256_update(&ctx, key_block, HMAC_BLOCK_SIZE);
    sha256_update(&ctx, inner_hash, SHA256_DIGEST_SIZE);
    sha256_final(&ctx, out);
}

static void pbkdf2_sha256(const uint8_t *password, size_t pass_len,
                          const uint8_t *salt, size_t salt_len,
                          uint32_t iterations,
                          uint8_t *out, size_t out_len) {
    uint8_t u[SHA256_DIGEST_SIZE];
    uint8_t t[SHA256_DIGEST_SIZE];
    uint8_t block[SHA256_DIGEST_SIZE];
    uint8_t salt_block[64];

    uint32_t block_index = 1;
    size_t produced = 0;

    while (produced < out_len) {
        size_t salt_block_len = 0;
        memcpy(salt_block, salt, salt_len);
        salt_block_len += salt_len;
        salt_block[salt_block_len++] = (uint8_t)(block_index >> 24);
        salt_block[salt_block_len++] = (uint8_t)(block_index >> 16);
        salt_block[salt_block_len++] = (uint8_t)(block_index >> 8);
        salt_block[salt_block_len++] = (uint8_t)(block_index);

        hmac_sha256(password, pass_len, salt_block, salt_block_len, u);
        memcpy(t, u, sizeof(t));

        for (uint32_t i = 1; i < iterations; i++) {
            hmac_sha256(password, pass_len, u, sizeof(u), u);
            for (size_t j = 0; j < sizeof(t); j++) t[j] ^= u[j];
        }

        memcpy(block, t, sizeof(block));

        size_t chunk = out_len - produced;
        if (chunk > sizeof(block)) chunk = sizeof(block);
        memcpy(out + produced, block, chunk);
        produced += chunk;
        block_index++;
    }
}

void password_gen_salt(char salt_hex_out[PASSWORD_SALT_HEX_LEN + 1]) {
    uint32_t t = timer_get_ticks();
    uint32_t pid = process_getpid();
    uint32_t addr = (uint32_t)(uintptr_t)salt_hex_out;
    uint32_t unix_time = 0;
    rtc_get_unix_time(&unix_time);
    uint32_t uptime = rtc_get_uptime_seconds();

    uint32_t entropy[6] = {
        t,
        pid,
        addr,
        unix_time,
        uptime,
        t ^ pid ^ addr ^ unix_time ^ uptime
    };

    uint8_t digest[SHA256_DIGEST_SIZE];
    sha256((const uint8_t *)entropy, sizeof(entropy), digest);
    bytes_to_hex(digest, 16, salt_hex_out);
}

void password_hash_pbkdf2(const char *password,
                          const char *salt_hex,
                          uint32_t iterations,
                          char *out) {
    uint8_t salt[16];
    uint8_t digest[SHA256_DIGEST_SIZE];
    char hash_hex[PASSWORD_HASH_HEX_LEN + 1];

    if (!password || !salt_hex || !out || iterations == 0) return;

    if (hex_to_bytes(salt_hex, salt, sizeof(salt)) != 0) return;

    pbkdf2_sha256((const uint8_t *)password, strlen(password),
                  salt, sizeof(salt), iterations, digest, sizeof(digest));

    bytes_to_hex(digest, sizeof(digest), hash_hex);

    strcpy(out, "$pbkdf2-sha256$");
    char iter_buf[16];
    itoa((int)iterations, iter_buf, 10);
    strcat(out, iter_buf);
    strcat(out, "$");
    strcat(out, salt_hex);
    strcat(out, "$");
    strcat(out, hash_hex);
}

int password_verify(const char *password, const char *stored_hash) {
    if (!password || !stored_hash) return 0;

    if (strncmp(stored_hash, "$pbkdf2-sha256$", 15) == 0) {
        const char *iter_start = stored_hash + 15;
        const char *iter_end = strchr(iter_start, '$');
        if (!iter_end) return 0;

        char iter_buf[16];
        size_t iter_len = (size_t)(iter_end - iter_start);
        if (iter_len == 0 || iter_len >= sizeof(iter_buf)) return 0;
        memcpy(iter_buf, iter_start, iter_len);
        iter_buf[iter_len] = '\0';

        uint32_t iterations = 0;
        if (parse_uint32(iter_buf, &iterations) != 0 || iterations == 0) return 0;

        const char *salt_start = iter_end + 1;
        const char *salt_end = strchr(salt_start, '$');
        if (!salt_end) return 0;

        size_t salt_len = (size_t)(salt_end - salt_start);
        if (salt_len != PASSWORD_SALT_HEX_LEN) return 0;

        char salt_hex[PASSWORD_SALT_HEX_LEN + 1];
        memcpy(salt_hex, salt_start, salt_len);
        salt_hex[salt_len] = '\0';

        char computed[128];
        password_hash_pbkdf2(password, salt_hex, iterations, computed);
        return strcmp(computed, stored_hash) == 0;
    }

    if (strncmp(stored_hash, "$sha256$", 8) == 0) {
        return sha256_verify_password(password, stored_hash);
    }

    return 0;
}
