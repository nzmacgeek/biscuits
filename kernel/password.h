#pragma once
// BlueyOS Password Hashing Helpers (PBKDF2-SHA256)
// "A little extra effort keeps the biscuit safe." - Bandit
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"

#define PASSWORD_SALT_HEX_LEN  32
#define PASSWORD_HASH_HEX_LEN  64
#define PASSWORD_DEFAULT_ITERS 10000u

// Generate a 16-byte salt as 32 hex characters (out must be 33 bytes incl. NUL).
void password_gen_salt(char salt_hex_out[PASSWORD_SALT_HEX_LEN + 1]);

// Hash password into $pbkdf2-sha256$<iters>$<salt>$<hash> format.
// out must be at least 128 bytes.
void password_hash_pbkdf2(const char *password,
                          const char *salt_hex,
                          uint32_t iterations,
                          char *out);

// Returns 1 if password matches stored_hash, 0 otherwise.
int password_verify(const char *password, const char *stored_hash);
