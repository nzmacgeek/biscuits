#pragma once
// BlueyOS SHA-256 Implementation
// Used for password hashing in /etc/shadow
// Pure C, no external dependencies - freestanding kernel code
//
// NOTE: SHA-256 with salt is acceptable for this research OS, but production
//       systems should use bcrypt, scrypt, or argon2 which include work factors.
//
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"

#define SHA256_DIGEST_SIZE  32
#define SHA256_BLOCK_SIZE   64

// SHA-256 context
typedef struct {
    uint32_t state[8];
    uint32_t count[2];    // bit count (lo, hi)
    uint8_t  buf[64];
} sha256_ctx_t;

// Core SHA-256 API
void sha256_init(sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len);
void sha256_final(sha256_ctx_t *ctx, uint8_t digest[SHA256_DIGEST_SIZE]);
void sha256(const uint8_t *data, size_t len, uint8_t digest[SHA256_DIGEST_SIZE]);

// Password hashing helpers
// Stored format in /etc/shadow: $sha256$<16 hex salt>$<64 hex hash>
// out must be at least 89 bytes: 8 + 16 + 1 + 64 + NUL
void sha256_hash_password(const char *password, const char *salt_hex, char *out);

// Returns 1 if password matches stored_hash, 0 otherwise
int  sha256_verify_password(const char *password, const char *stored_hash);

// Generate 8-byte (16 hex char) salt from timer ticks
void sha256_gen_salt(char salt_hex_out[17]);

// Utility: convert raw digest bytes to lowercase hex string (out needs 65 bytes)
void sha256_to_hex(const uint8_t digest[SHA256_DIGEST_SIZE], char out[65]);
