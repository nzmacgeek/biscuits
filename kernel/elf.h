#pragma once
// BlueyOS ELF32 Loader - "Judo's ELF Loader: Flipping programs into memory!"
// Episode ref: "Judo" - she flips everyone, just like ELF flips segments into RAM
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "process.h"

typedef struct {
    char     name[32];
    uint32_t entry;
    uint32_t program_entry;
    uint32_t interp_base;
    uint32_t image_end;
    uint32_t stack_base;
    uint32_t stack_top;
    uint32_t stack_pointer;
    uint32_t page_dir;
} elf_image_t;

typedef struct {
    uint32_t phdr;
    uint32_t phent;
    uint32_t phnum;
    uint32_t entry;
    uint32_t base;
    uint32_t secure;
} elf_auxv_info_t;

// ELF32 magic and constants
#define ELF_MAGIC    0x464C457F   // 0x7F 'E' 'L' 'F' (little-endian uint32)
#define ET_EXEC      2            // executable file
#define ET_DYN       3            // shared object
#define EM_386       3            // Intel 80386
#define PT_LOAD      1            // loadable segment
#define PT_DYNAMIC   2            // dynamic linking information
#define PT_INTERP    3            // program interpreter path
#define PT_PHDR      6            // program header table
#define PF_X         0x1          // segment executable
#define PF_W         0x2          // segment writable
#define PF_R         0x4          // segment readable

// ELF32 file header
typedef struct __attribute__((packed)) {
    uint8_t  e_ident[16];   // magic, class, data, version, OS/ABI
    uint16_t e_type;        // ET_EXEC etc.
    uint16_t e_machine;     // EM_386
    uint32_t e_version;
    uint32_t e_entry;       // entry point virtual address
    uint32_t e_phoff;       // program header table offset
    uint32_t e_shoff;       // section header table offset
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;       // number of program headers
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf32_ehdr_t;

// ELF32 program header
typedef struct __attribute__((packed)) {
    uint32_t p_type;    // PT_LOAD etc.
    uint32_t p_offset;  // offset in file
    uint32_t p_vaddr;   // virtual address to load at
    uint32_t p_paddr;   // physical address (usually == vaddr for exec)
    uint32_t p_filesz;  // size in file
    uint32_t p_memsz;   // size in memory (>= filesz, zero-pad the rest)
    uint32_t p_flags;   // PF_X | PF_W | PF_R
    uint32_t p_align;
} elf32_phdr_t;

// Returns 0 on success, -1 on error. Sets *entry_out to entry point.
int elf_load(const uint8_t *data, size_t len, uint32_t *entry_out);
int elf_validate(const uint8_t *data, size_t len, const char *name);
int elf_build_initial_stack(uint32_t page_dir,
                            const char *const argv[], const char *const envp[],
                            uint32_t *stack_base_out, uint32_t *stack_top_out,
                            uint32_t *stack_pointer_out,
                            const elf_auxv_info_t *auxv_info);
int elf_load_image(const char *path, const char *const argv[], const char *const envp[],
                   elf_image_t *image_out);
process_t *elf_exec(const char *path, uint32_t uid);
