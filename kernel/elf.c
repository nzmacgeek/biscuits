// BlueyOS ELF32 Loader - "Judo's ELF Loader: Flipping programs into memory!"
// Episode ref: "Judo" - she can flip anything
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../lib/string.h"
#include "../lib/stdio.h"
#include "elf.h"
#include "paging.h"

int elf_validate(const uint8_t *data, size_t len) {
    if (len < sizeof(elf32_ehdr_t)) {
        kprintf("[ELF] Too small to be an ELF file\n");
        return -1;
    }
    const elf32_ehdr_t *hdr = (const elf32_ehdr_t*)data;

    // Check magic: 0x7F 'E' 'L' 'F'
    if (hdr->e_ident[0] != 0x7F ||
        hdr->e_ident[1] != 'E'  ||
        hdr->e_ident[2] != 'L'  ||
        hdr->e_ident[3] != 'F') {
        kprintf("[ELF] Invalid magic - that's not an ELF!\n");
        return -1;
    }
    // 32-bit ELF only (class = 1)
    if (hdr->e_ident[4] != 1) {
        kprintf("[ELF] Not a 32-bit ELF\n");
        return -1;
    }
    // Must be executable
    if (hdr->e_type != ET_EXEC) {
        kprintf("[ELF] Not an executable ELF (type=%d)\n", hdr->e_type);
        return -1;
    }
    // Must be x86
    if (hdr->e_machine != EM_386) {
        kprintf("[ELF] Not an x86 ELF (machine=%d)\n", hdr->e_machine);
        return -1;
    }
    return 0;
}

int elf_load(const uint8_t *data, size_t len, uint32_t *entry_out) {
    if (elf_validate(data, len) != 0) return -1;

    const elf32_ehdr_t *hdr = (const elf32_ehdr_t*)data;

    // Iterate program headers
    uint32_t loaded = 0;
    for (uint16_t i = 0; i < hdr->e_phnum; i++) {
        const elf32_phdr_t *ph = (const elf32_phdr_t*)
            (data + hdr->e_phoff + i * hdr->e_phentsize);

        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_filesz == 0)     continue;

        // Validate offsets are within the file
        if (ph->p_offset + ph->p_filesz > len) {
            kprintf("[ELF] Segment %d extends beyond file!\n", i);
            return -1;
        }

        // Bounds check: refuse to load into first page (NULL guard) or kernel
        if (ph->p_vaddr < 0x1000) {
            kprintf("[ELF] Segment %d tries to load at NULL page - denied!\n", i);
            return -1;
        }

        // Copy filesz bytes from file to vaddr
        uint32_t flags = PAGE_PRESENT | PAGE_USER;
        if (ph->p_flags & PF_W) flags |= PAGE_WRITABLE;

        // Map pages for this segment
        uint32_t vstart = ph->p_vaddr & ~0xFFF;
        uint32_t vend   = (ph->p_vaddr + ph->p_memsz + 0xFFF) & ~0xFFF;
        for (uint32_t va = vstart; va < vend; va += PAGE_SIZE) {
            uint32_t phys = pmm_alloc_frame();
            if (!phys) { kprintf("[ELF] Out of physical frames!\n"); return -1; }
            paging_map(va, phys, flags);
        }

        // Copy data
        memcpy((void*)ph->p_vaddr, data + ph->p_offset, ph->p_filesz);

        // Zero-fill memsz - filesz (BSS-like region)
        if (ph->p_memsz > ph->p_filesz) {
            memset((void*)(ph->p_vaddr + ph->p_filesz), 0,
                   ph->p_memsz - ph->p_filesz);
        }
        loaded++;
        kprintf("[ELF] Loaded segment %d: vaddr=0x%x size=%d\n",
                i, ph->p_vaddr, ph->p_filesz);
    }

    if (loaded == 0) {
        kprintf("[ELF] No loadable segments found!\n");
        return -1;
    }

    *entry_out = hdr->e_entry;
    kprintf("[ELF] Entry point: 0x%x - Judo is ready to flip!\n", hdr->e_entry);
    return 0;
}
