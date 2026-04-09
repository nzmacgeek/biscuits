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

    // Calculate total size needed - sum of all allocated sections
    elf32_shdr_t *shdr = (elf32_shdr_t *)(data + ehdr->e_shoff);
    size_t total_size = 0;
    uint32_t *section_addrs = NULL;

    // First pass: calculate size
    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (shdr[i].sh_flags & SHF_ALLOC) {
            // Align section
            if (shdr[i].sh_addralign > 1) {
                total_size = (total_size + shdr[i].sh_addralign - 1) & ~(shdr[i].sh_addralign - 1);
            }
            total_size += shdr[i].sh_size;
        }
    }

    // Add padding
    total_size += 4096;

    // Allocate memory for the module
    void *module_mem = kheap_alloc(total_size, 1); // Aligned
    if (!module_mem) {
        kprintf("[MODELF] Failed to allocate %u bytes for module\n", (uint32_t)total_size);
        return -1;
    }

    memset(module_mem, 0, total_size);
    kprintf("[MODELF] Allocated %u bytes at 0x%x\n", (uint32_t)total_size, (uint32_t)module_mem);

    // Allocate section address array
    section_addrs = (uint32_t*)kheap_alloc(sizeof(uint32_t) * ehdr->e_shnum, 0);
    if (!section_addrs) {
        kheap_free(module_mem);
        return -1;
    }
    memset(section_addrs, 0, sizeof(uint32_t) * ehdr->e_shnum);

    // Second pass: load sections and track their addresses
    size_t offset = 0;
    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (!(shdr[i].sh_flags & SHF_ALLOC)) {
            section_addrs[i] = 0;
            continue;
        }

        // Align section
        if (shdr[i].sh_addralign > 1) {
            offset = (offset + shdr[i].sh_addralign - 1) & ~(shdr[i].sh_addralign - 1);
        }

        section_addrs[i] = (uint32_t)module_mem + offset;

        if (shdr[i].sh_type == SHT_PROGBITS) {
            // Copy section data
            if (shdr[i].sh_offset + shdr[i].sh_size <= len) {
                memcpy((void*)section_addrs[i],
                       data + shdr[i].sh_offset,
                       shdr[i].sh_size);
                kprintf("[MODELF] Loaded section %d at 0x%x (size %u)\n",
                        i, section_addrs[i], shdr[i].sh_size);
            }
        } else if (shdr[i].sh_type == SHT_NOBITS) {
            // BSS - already zeroed
            kprintf("[MODELF] BSS section %d at 0x%x (size %u)\n",
                    i, section_addrs[i], shdr[i].sh_size);
        }

        offset += shdr[i].sh_size;
    }

    // Find symbol table and string table
    elf32_shdr_t *symtab = NULL;
    const char *strtab = NULL;
    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (shdr[i].sh_type == SHT_SYMTAB) {
            symtab = &shdr[i];
            if (shdr[i].sh_link < ehdr->e_shnum) {
                strtab = (const char*)(data + shdr[shdr[i].sh_link].sh_offset);
            }
            break;
        }
    }

    // Process relocations
    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (shdr[i].sh_type != SHT_REL && shdr[i].sh_type != SHT_RELA) continue;

        // Get target section
        if (shdr[i].sh_info >= ehdr->e_shnum) continue;
        if (!section_addrs[shdr[i].sh_info]) continue;

        kprintf("[MODELF] Processing relocations for section %d\n", shdr[i].sh_info);

        if (shdr[i].sh_type == SHT_REL) {
            elf32_rel_t *rel = (elf32_rel_t*)(data + shdr[i].sh_offset);
            int num_rels = shdr[i].sh_size / sizeof(elf32_rel_t);

            for (int j = 0; j < num_rels; j++) {
                uint32_t sym_idx = ELF32_R_SYM(rel[j].r_info);
                uint32_t rel_type = ELF32_R_TYPE(rel[j].r_info);
                uint32_t *ref = (uint32_t*)(section_addrs[shdr[i].sh_info] + rel[j].r_offset);

                // Get symbol
                elf32_sym_t *sym = NULL;
                if (symtab && sym_idx < (symtab->sh_size / sizeof(elf32_sym_t))) {
                    sym = (elf32_sym_t*)(data + symtab->sh_offset) + sym_idx;
                }

                uint32_t sym_addr = 0;
                if (sym) {
                    if (sym->st_shndx == SHN_UNDEF) {
                        // External symbol - look up in kernel symbol table
                        const char *sym_name = strtab ? strtab + sym->st_name : NULL;
                        if (sym_name) {
                            void *addr = ksym_lookup(sym_name);
                            if (addr) {
                                sym_addr = (uint32_t)addr;
                            } else {
                                kprintf("[MODELF] WARNING: Undefined symbol '%s'\n", sym_name);
                            }
                        }
                    } else if (sym->st_shndx < ehdr->e_shnum && section_addrs[sym->st_shndx]) {
                        // Internal symbol
                        sym_addr = section_addrs[sym->st_shndx] + sym->st_value;
                    }
                }

                // Apply relocation
                switch (rel_type) {
                    case R_386_NONE:
                        break;
                    case R_386_32:
                        // Direct 32-bit: S + A
                        *ref += sym_addr;
                        break;
                    case R_386_PC32:
                        // PC-relative: S + A - P
                        *ref += sym_addr - (uint32_t)ref;
                        break;
                    case R_386_RELATIVE:
                        // Adjust by program base: B + A
                        *ref += (uint32_t)module_mem;
                        break;
                    default:
                        kprintf("[MODELF] WARNING: Unsupported relocation type %d\n", rel_type);
                        break;
                }
            }
        }
    }

    // Try to find module_info structure
    module_info_t *module_info = NULL;
    if (symtab && strtab) {
        elf32_sym_t *syms = (elf32_sym_t*)(data + symtab->sh_offset);
        int num_syms = symtab->sh_size / sizeof(elf32_sym_t);

        for (int i = 0; i < num_syms; i++) {
            const char *name = strtab + syms[i].st_name;
            if (strcmp(name, "module_info") == 0) {
                if (syms[i].st_shndx < ehdr->e_shnum && section_addrs[syms[i].st_shndx]) {
                    module_info = (module_info_t*)(section_addrs[syms[i].st_shndx] + syms[i].st_value);
                    kprintf("[MODELF] Found module_info at 0x%x\n", (uint32_t)module_info);
                    break;
                }
            }
        }
    }

    kheap_free(section_addrs);

    *base_out = module_mem;
    *size_out = total_size;
    *info_out = module_info;

    kprintf("[MODELF] Module loaded at 0x%x (size %u)\n", (uint32_t)module_mem, (uint32_t)total_size);
    return 0;
}

void module_elf_unload(void *base, size_t size) {
    if (base) {
        kprintf("[MODELF] Unloading module at 0x%x (size %u)\n", (uint32_t)base, (uint32_t)size);
        kheap_free(base);
    }
}
