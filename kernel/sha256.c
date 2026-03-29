// BlueyOS SHA-256 Implementation
// Pure C, no external libraries - runs in freestanding kernel environment
// RFC 6234 / FIPS 180-4 compliant.
//
// NOTE: This uses a simple SHA-256 + salt scheme. For production systems,
// use bcrypt, scrypt, or argon2id which include a configurable work factor
// and are specifically designed for password storage. This is a research OS!
//
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../lib/string.h"
#include "sha256.h"
#include "timer.h"

// SHA-256 round constants (first 32 bits of fractional parts of cube roots of first 64 primes)
static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

// Initial hash values (first 32 bits of fractional parts of sqrt of first 8 primes)
static const uint32_t H0[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

#define ROTR(x,n) (((x) >> (n)) | ((x) << (32-(n))))
#define CH(x,y,z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x)  (ROTR(x,2)  ^ ROTR(x,13) ^ ROTR(x,22))
#define EP1(x)  (ROTR(x,6)  ^ ROTR(x,11) ^ ROTR(x,25))
#define SIG0(x) (ROTR(x,7)  ^ ROTR(x,18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x,17) ^ ROTR(x,19) ^ ((x) >> 10))

static void sha256_transform(sha256_ctx_t *ctx, const uint8_t data[64]) {
    uint32_t a, b, c, d, e, f, g, h, t1, t2, m[64];
    int i;

    for (i = 0; i < 16; i++) {
        m[i] = ((uint32_t)data[i*4]     << 24) |
               ((uint32_t)data[i*4 + 1] << 16) |
               ((uint32_t)data[i*4 + 2] <<  8) |
               ((uint32_t)data[i*4 + 3]);
    }
    for (; i < 64; i++) {
        m[i] = SIG1(m[i-2]) + m[i-7] + SIG0(m[i-15]) + m[i-16];
    }

    a = ctx->state[0]; b = ctx->state[1];
    c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5];
    g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e,f,g) + K[i] + m[i];
        t2 = EP0(a) + MAJ(a,b,c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b;
    ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f;
    ctx->state[6] += g; ctx->state[7] += h;
}

void sha256_init(sha256_ctx_t *ctx) {
    ctx->count[0] = ctx->count[1] = 0;
    for (int i = 0; i < 8; i++) ctx->state[i] = H0[i];
}

void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len) {
    size_t i;
    for (i = 0; i < len; i++) {
        ctx->buf[ctx->count[0] / 8 % 64] = data[i];
        if ((ctx->count[0] += 8) == 0) ctx->count[1]++;
        if (ctx->count[0] % 512 == 0)
            sha256_transform(ctx, ctx->buf);
    }
}

void sha256_final(sha256_ctx_t *ctx, uint8_t digest[SHA256_DIGEST_SIZE]) {
    uint8_t data[64];
    uint32_t dbits = ctx->count[0];
    uint32_t i = (dbits / 8) % 64;
    int j;

    /* copy buffer */
    for (j = 0; j < (int)(dbits / 8 % 64 + 1); j++)
        data[j] = ctx->buf[j];

    data[i++] = 0x80;
    if (i > 56) {
        while (i < 64) data[i++] = 0;
        sha256_transform(ctx, data);
        i = 0;
    }
    while (i < 56) data[i++] = 0;

    uint64_t bits = ((uint64_t)ctx->count[1] << 32) | ctx->count[0];
    data[56] = (uint8_t)(bits >> 56);
    data[57] = (uint8_t)(bits >> 48);
    data[58] = (uint8_t)(bits >> 40);
    data[59] = (uint8_t)(bits >> 32);
    data[60] = (uint8_t)(bits >> 24);
    data[61] = (uint8_t)(bits >> 16);
    data[62] = (uint8_t)(bits >>  8);
    data[63] = (uint8_t)(bits);
    sha256_transform(ctx, data);

    for (i = 0; i < 4; i++) {
        digest[i]      = (ctx->state[0] >> (24 - i*8)) & 0xFF;
        digest[i + 4]  = (ctx->state[1] >> (24 - i*8)) & 0xFF;
        digest[i + 8]  = (ctx->state[2] >> (24 - i*8)) & 0xFF;
        digest[i + 12] = (ctx->state[3] >> (24 - i*8)) & 0xFF;
        digest[i + 16] = (ctx->state[4] >> (24 - i*8)) & 0xFF;
        digest[i + 20] = (ctx->state[5] >> (24 - i*8)) & 0xFF;
        digest[i + 24] = (ctx->state[6] >> (24 - i*8)) & 0xFF;
        digest[i + 28] = (ctx->state[7] >> (24 - i*8)) & 0xFF;
    }
}

void sha256(const uint8_t *data, size_t len, uint8_t digest[SHA256_DIGEST_SIZE]) {
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}

static const char hex_chars[] = "0123456789abcdef";

void sha256_to_hex(const uint8_t digest[SHA256_DIGEST_SIZE], char out[65]) {
    for (int i = 0; i < SHA256_DIGEST_SIZE; i++) {
        out[i*2]     = hex_chars[(digest[i] >> 4) & 0xF];
        out[i*2 + 1] = hex_chars[digest[i] & 0xF];
    }
    out[64] = '\0';
}

// hash = SHA256(salt_hex + password)
// Stored: "$sha256$" + salt_hex + "$" + 64-char hex hash
void sha256_hash_password(const char *password, const char *salt_hex, char *out) {
    uint8_t digest[SHA256_DIGEST_SIZE];
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t*)salt_hex, strlen(salt_hex));
    sha256_update(&ctx, (const uint8_t*)password,  strlen(password));
    sha256_final(&ctx, digest);

    char hash_hex[65];
    sha256_to_hex(digest, hash_hex);

    /* Format: $sha256$<salt>$<hash> */
    strcpy(out, "$sha256$");
    strcat(out, salt_hex);
    strcat(out, "$");
    strcat(out, hash_hex);
}

int sha256_verify_password(const char *password, const char *stored_hash) {
    /* expected format: $sha256$<16 hex salt>$<64 hex hash> */
    if (strncmp(stored_hash, "$sha256$", 8) != 0) return 0;
    const char *salt_start = stored_hash + 8;
    const char *dollar = strchr(salt_start, '$');
    if (!dollar) return 0;

    /* extract salt */
    char salt_hex[17];
    size_t salt_len = (size_t)(dollar - salt_start);
    if (salt_len > 16) salt_len = 16;
    memcpy(salt_hex, salt_start, salt_len);
    salt_hex[salt_len] = '\0';

    /* compute hash with same salt */
    char computed[90];
    sha256_hash_password(password, salt_hex, computed);
    return (strcmp(computed, stored_hash) == 0) ? 1 : 0;
}

void sha256_gen_salt(char salt_hex_out[17]) {
    /* Use timer ticks as entropy source - simple for a research OS */
    uint32_t t = timer_get_ticks();
    /* Mix with a fixed constant to reduce predictability */
    uint32_t a = t ^ 0xDEADBEEF;
    uint32_t b = t * 0x6C62272E; /* FNV prime */
    for (int i = 0; i < 4; i++) {
        salt_hex_out[i*2]     = hex_chars[(a >> (4*(7-i*2))) & 0xF];
        salt_hex_out[i*2 + 1] = hex_chars[(a >> (4*(6-i*2))) & 0xF];
    }
    for (int i = 0; i < 4; i++) {
        salt_hex_out[8 + i*2]     = hex_chars[(b >> (4*(7-i*2))) & 0xF];
        salt_hex_out[8 + i*2 + 1] = hex_chars[(b >> (4*(6-i*2))) & 0xF];
    }
    salt_hex_out[16] = '\0';
}
