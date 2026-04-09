#pragma once
// BlueyOS Module ELF Loader - "Judo's Module Flip"
// Episode ref: "Judo" - flipping modules into kernel memory!
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"

// Module metadata structure that must be present in every module
typedef struct {
    const char *name;
    const char *description;
    const char *version;
    const char *author;
    int       (*init)(void);
    int       (*exit)(void);
} module_info_t;

// Load a module from ELF data in memory
// Returns 0 on success, populates module_out with loaded module info
int module_elf_load(const uint8_t *data, size_t len, void **base_out, size_t *size_out,
                    module_info_t **info_out);

// Unload a module and free its memory
void module_elf_unload(void *base, size_t size);

// Simple relocation types we support (x86)
#define R_386_NONE      0   // No relocation
#define R_386_32        1   // Direct 32-bit
#define R_386_PC32      2   // PC-relative 32-bit
#define R_386_RELATIVE  8   // Adjust by program base
