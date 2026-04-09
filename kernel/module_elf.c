// BlueyOS Module ELF Loader - "Judo's Module Flip"
// Episode ref: "Judo" - flipping modules into kernel memory!
// Simplified ELF ET_REL (relocatable) loader for kernel modules
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "module_elf.h"
#include "elf.h"
#include "kheap.h"
#include "ksyms.h"

// ELF section header
typedef struct __attribute__((packed)) {
    uint32_t sh_name;       // Section name (string table index)
    uint32_t sh_type;       // Section type
    uint32_t sh_flags;      // Section flags
    uint32_t sh_addr;       // Section virtual address at execution
    uint32_t sh_offset;     // Section file offset
    uint32_t sh_size;       // Section size in bytes
    uint32_t sh_link;       // Link to another section
    uint32_t sh_info;       // Additional section information
    uint32_t sh_addralign;  // Section alignment
    uint32_t sh_entsize;    // Entry size if section holds table
} elf32_shdr_t;

// ELF symbol table entry
typedef struct __attribute__((packed)) {
    uint32_t st_name;       // Symbol name (string table index)
    uint32_t st_value;      // Symbol value
    uint32_t st_size;       // Symbol size
    uint8_t  st_info;       // Symbol type and binding
    uint8_t  st_other;      // Symbol visibility
    uint16_t st_shndx;      // Section index
} elf32_sym_t;

// ELF relocation entry
typedef struct __attribute__((packed)) {
    uint32_t r_offset;      // Location at which to apply relocation
    uint32_t r_info;        // Symbol table index and relocation type
} elf32_rel_t;

// ELF relocation entry with addend
typedef struct __attribute__((packed)) {
    uint32_t r_offset;
    uint32_t r_info;
    int32_t  r_addend;
} elf32_rela_t;

#define ELF32_R_SYM(i)    ((i) >> 8)
#define ELF32_R_TYPE(i)   ((i) & 0xFF)
#define ELF32_ST_BIND(i)  ((i) >> 4)
#define ELF32_ST_TYPE(i)  ((i) & 0xF)

#define ET_REL        1     // Relocatable file
#define SHT_NULL      0     // Unused section
#define SHT_PROGBITS  1     // Program data
#define SHT_SYMTAB    2     // Symbol table
#define SHT_STRTAB    3     // String table
#define SHT_RELA      4     // Relocation entries with addends
#define SHT_NOBITS    8     // BSS section
#define SHT_REL       9     // Relocation entries
#define SHF_ALLOC     0x2   // Section occupies memory during execution
#define SHF_EXECINSTR 0x4   // Section contains executable instructions
#define SHF_WRITE     0x1   // Section is writable

#define STB_LOCAL     0     // Local symbol
#define STB_GLOBAL    1     // Global symbol
#define STB_WEAK      2     // Weak symbol

#define STT_NOTYPE    0     // Symbol type is unspecified
#define STT_OBJECT    1     // Symbol is a data object
#define STT_FUNC      2     // Symbol is a code object
#define STT_SECTION   3     // Symbol associated with a section

#define SHN_UNDEF     0     // Undefined section
#define SHN_ABS       0xFFF1 // Absolute value

// Simplified module loader - loads ET_REL into kernel memory
int module_elf_load(const uint8_t *data, size_t len, void **base_out, size_t *size_out,
                    module_info_t **info_out) {
    if (!data || len < sizeof(elf32_ehdr_t) || !base_out || !size_out || !info_out) {
        kprintf("[MODELF] Invalid parameters\n");
        return -1;
    }

    elf32_ehdr_t *ehdr = (elf32_ehdr_t *)data;

    // Validate ELF header
    if (*(uint32_t*)ehdr->e_ident != ELF_MAGIC) {
        kprintf("[MODELF] Invalid ELF magic\n");
        return -1;
    }

    if (ehdr->e_type != ET_REL) {
        kprintf("[MODELF] Not a relocatable object (ET_REL required)\n");
        return -1;
    }

    if (ehdr->e_machine != EM_386) {
        kprintf("[MODELF] Not an x86 binary\n");
        return -1;
    }

    // Calculate total size needed
    elf32_shdr_t *shdr = (elf32_shdr_t *)(data + ehdr->e_shoff);
    size_t total_size = 0;

    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (shdr[i].sh_flags & SHF_ALLOC) {
            size_t end = shdr[i].sh_size;
            if (end > total_size) total_size = end;
        }
    }

    // Add some padding
    total_size += 4096;

    // Allocate memory for the module
    void *module_mem = kheap_alloc(total_size, 1); // Aligned
    if (!module_mem) {
        kprintf("[MODELF] Failed to allocate %u bytes for module\n", (uint32_t)total_size);
        return -1;
    }

    memset(module_mem, 0, total_size);
    kprintf("[MODELF] Allocated %u bytes at 0x%x\n", (uint32_t)total_size, (uint32_t)module_mem);

    // For now, simplified: just copy sections that should be loaded
    // A full implementation would handle relocations
    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (!(shdr[i].sh_flags & SHF_ALLOC)) continue;

        if (shdr[i].sh_type == SHT_PROGBITS) {
            // Copy section data
            if (shdr[i].sh_offset + shdr[i].sh_size <= len) {
                memcpy((uint8_t*)module_mem + shdr[i].sh_addr,
                       data + shdr[i].sh_offset,
                       shdr[i].sh_size);
                kprintf("[MODELF] Loaded section %d at offset 0x%x (size %u)\n",
                        i, shdr[i].sh_addr, shdr[i].sh_size);
            }
        } else if (shdr[i].sh_type == SHT_NOBITS) {
            // BSS - already zeroed
            kprintf("[MODELF] BSS section %d at offset 0x%x (size %u)\n",
                    i, shdr[i].sh_addr, shdr[i].sh_size);
        }
    }

    // TODO: Handle relocations (simplified for now)
    // TODO: Resolve symbols against kernel symbol table
    // TODO: Find module_info structure

    // For now, just return what we have
    *base_out = module_mem;
    *size_out = total_size;
    *info_out = NULL; // TODO: find module_info in loaded module

    kprintf("[MODELF] Module loaded at 0x%x (size %u)\n", (uint32_t)module_mem, (uint32_t)total_size);
    return 0;
}

void module_elf_unload(void *base, size_t size) {
    if (base) {
        kprintf("[MODELF] Unloading module at 0x%x (size %u)\n", (uint32_t)base, (uint32_t)size);
        kheap_free(base);
    }
}
