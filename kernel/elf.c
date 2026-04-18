// BlueyOS ELF32 Loader - "Judo's ELF Loader: Flipping programs into memory!"
// Episode ref: "Judo" - she can flip anything
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../fs/vfs.h"
#include "../lib/string.h"
#include "../lib/stdio.h"
#include "elf.h"
#include "kheap.h"
#include "paging.h"
#include "multiuser.h"
#include "syslog.h"

#define ELF_READ_CHUNK_SIZE   512u
#define ELF_MAX_IMAGE_SIZE    (1024u * 1024u)
#define ELF_INTERP_BASE       0x20000000u
#define ELF_USER_STACK_BASE   0x70000000u
#define ELF_USER_STACK_SIZE   (8u * 1024u * 1024u)   /* 8 MiB soft limit, matching Linux default */
#define ELF_USER_STACK_PREFAULT_PAGES 4u
#define ELF_USER_STACK_STRIDE 0x00A00000u             /* 10 MiB between process stacks */

#define AT_NULL    0u
#define AT_PHDR    3u
#define AT_PHENT   4u
#define AT_PHNUM   5u
#define AT_PAGESZ  6u
#define AT_BASE    7u
#define AT_ENTRY   9u
#define AT_SECURE  23u
#define AT_RANDOM  25u

#define ELF_INTERP_PATH_MAX 128u

typedef struct {
    elf32_ehdr_t hdr;
    elf32_phdr_t *phdrs;
    uint32_t phdr_addr;
    char interp_path[ELF_INTERP_PATH_MAX];
    int has_interp;
} elf_metadata_t;

typedef struct {
    uint32_t type;
    uint32_t value;
} elf_aux_entry_t;

static uint32_t elf_next_stack_base = ELF_USER_STACK_BASE;

static size_t elf_vector_count(const char *const vec[]) {
    size_t count = 0;

    if (!vec) return 0;
    while (vec[count]) count++;
    return count;
}

static int elf_read_file(const char *path, uint8_t **data_out, size_t *len_out) {
    int fd;
    uint8_t chunk[ELF_READ_CHUNK_SIZE];
    uint8_t *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;
    uint32_t heap_total = 0;
    uint32_t heap_used = 0;
    uint32_t heap_free = 0;

    if (!path || !data_out || !len_out) return -1;

    fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        kprintf("[ELF] Open failed for %s\n", path);
        return -1;
    }

    for (;;) {
        int nread = vfs_read(fd, chunk, sizeof(chunk));
        if (nread < 0) {
            kprintf("[ELF] Read failed for %s at offset=%u\n", path, (uint32_t)length);
            vfs_close(fd);
            if (buffer) kheap_free(buffer);
            return -1;
        }
        if (nread == 0) break;

        if (length + (size_t)nread > capacity) {
            size_t next_capacity = capacity ? capacity * 2u : 4096u;
            uint8_t *next_buffer;

            while (next_capacity < length + (size_t)nread) next_capacity *= 2u;
            if (next_capacity > ELF_MAX_IMAGE_SIZE) {
                vfs_close(fd);
                if (buffer) kheap_free(buffer);
                return -1;
            }

            next_buffer = (uint8_t*)kheap_alloc(next_capacity, 0);
            if (!next_buffer) {
                kheap_get_stats(&heap_total, &heap_used, &heap_free);
                kprintf("[ELF] Heap alloc failed for %s size=%u total=%u used=%u free=%u\n",
                        path, (uint32_t)next_capacity, heap_total, heap_used, heap_free);
                vfs_close(fd);
                if (buffer) kheap_free(buffer);
                return -1;
            }
            if (buffer && length) memcpy(next_buffer, buffer, length);
            if (buffer) kheap_free(buffer);
            buffer = next_buffer;
            capacity = next_capacity;
        }

        memcpy(buffer + length, chunk, (size_t)nread);
        length += (size_t)nread;
    }

    vfs_close(fd);
    *data_out = buffer;
    *len_out = length;
    return 0;
}

static void elf_free_metadata(elf_metadata_t *meta) {
    if (!meta) return;
    if (meta->phdrs) kheap_free(meta->phdrs);
    memset(meta, 0, sizeof(*meta));
}

static int elf_validate_header(const elf32_ehdr_t *hdr, const char *name, int allow_dyn) {
    const char *n = (name && name[0]) ? name : "(unknown)";

    if (!hdr) return -1;

    if (hdr->e_ident[0] != 0x7F ||
        hdr->e_ident[1] != 'E'  ||
        hdr->e_ident[2] != 'L'  ||
        hdr->e_ident[3] != 'F') {
        kprintf("[ELF] Invalid magic - not an ELF: %s\n", n);
        return -1;
    }
    if (hdr->e_ident[4] != 1) {
        kprintf("[ELF] Not a 32-bit ELF: %s\n", n);
        return -1;
    }
    if (hdr->e_type != ET_EXEC && (!allow_dyn || hdr->e_type != ET_DYN)) {
        kprintf("[ELF] Unsupported ELF type=%d: %s\n", hdr->e_type, n);
        return -1;
    }
    if (hdr->e_machine != EM_386) {
        kprintf("[ELF] Not an x86 ELF (machine=%d): %s\n", hdr->e_machine, n);
        return -1;
    }
    return 0;
}

static int elf_read_metadata_fd(int fd, size_t file_len, const char *name,
                                int allow_dyn, elf_metadata_t *meta_out) {
    size_t ph_size;
    int found_phdr = 0;

    if (!meta_out) return -1;
    memset(meta_out, 0, sizeof(*meta_out));

    if (vfs_read_at(fd, (uint8_t*)&meta_out->hdr, sizeof(meta_out->hdr), 0) != (int)sizeof(meta_out->hdr)) {
        kprintf("[ELF] Failed to read ELF header\n");
        return -1;
    }

    if (elf_validate_header(&meta_out->hdr, name, allow_dyn) != 0) return -1;

    ph_size = (size_t)meta_out->hdr.e_phnum * meta_out->hdr.e_phentsize;
    if (ph_size == 0) {
        kprintf("[ELF] No program headers\n");
        return -1;
    }

    meta_out->phdrs = (elf32_phdr_t*)kheap_alloc(ph_size, 0);
    if (!meta_out->phdrs) {
        kprintf("[ELF] Failed to allocate program header buffer\n");
        return -1;
    }
    if (vfs_read_at(fd, (uint8_t*)meta_out->phdrs, ph_size, meta_out->hdr.e_phoff) != (int)ph_size) {
        kprintf("[ELF] Failed to read program headers\n");
        elf_free_metadata(meta_out);
        return -1;
    }

    for (uint16_t i = 0; i < meta_out->hdr.e_phnum; i++) {
        elf32_phdr_t *ph = &meta_out->phdrs[i];

        if (ph->p_type == PT_PHDR) {
            meta_out->phdr_addr = ph->p_vaddr;
            found_phdr = 1;
        } else if (ph->p_type == PT_INTERP) {
            size_t copy_len;

            if (ph->p_offset + ph->p_filesz > file_len || ph->p_filesz == 0) {
                kprintf("[ELF] PT_INTERP extends beyond file: %s\n", name ? name : "(unknown)");
                elf_free_metadata(meta_out);
                return -1;
            }

            copy_len = ph->p_filesz;
            if (copy_len >= sizeof(meta_out->interp_path)) copy_len = sizeof(meta_out->interp_path) - 1u;
            if (vfs_read_at(fd, (uint8_t*)meta_out->interp_path, copy_len, ph->p_offset) != (int)copy_len) {
                kprintf("[ELF] Failed to read PT_INTERP for %s\n", name ? name : "(unknown)");
                elf_free_metadata(meta_out);
                return -1;
            }
            meta_out->interp_path[copy_len] = '\0';
            meta_out->has_interp = 1;
        }
    }

    if (!found_phdr) {
        for (uint16_t i = 0; i < meta_out->hdr.e_phnum; i++) {
            elf32_phdr_t *ph = &meta_out->phdrs[i];
            if (ph->p_type != PT_LOAD) continue;
            if (meta_out->hdr.e_phoff < ph->p_offset) continue;
            if (meta_out->hdr.e_phoff >= ph->p_offset + ph->p_filesz) continue;
            meta_out->phdr_addr = ph->p_vaddr + (meta_out->hdr.e_phoff - ph->p_offset);
            break;
        }
    }

    return 0;
}

static uint32_t elf_alloc_stack_region(void) {
    // Defensive init: ensure we have a sane user stack base. Some build/runtime
    // scenarios can leave the static zeroed; fall back to the compile-time
    // default if that's the case.
    if (elf_next_stack_base == 0) elf_next_stack_base = ELF_USER_STACK_BASE;
    uint32_t base = elf_next_stack_base;
    elf_next_stack_base += ELF_USER_STACK_STRIDE;
    return base;
}

static int elf_map_stack_pages(uint32_t page_dir, uint32_t stack_base, uint32_t stack_size) {
    /* Reserve a larger stack region with a guard page at the very bottom.
     * Prefault a few top pages so libc startup and early userspace code do
     * not immediately fault on ordinary stack frames, while still allowing
     * deeper growth on demand. */
    uint32_t stack_top = stack_base + stack_size;
    uint32_t pages_to_map = ELF_USER_STACK_PREFAULT_PAGES;

    if (stack_size <= PAGE_SIZE) return -1;
    if (pages_to_map == 0) pages_to_map = 1;
    if (pages_to_map > (stack_size / PAGE_SIZE) - 1u) {
        pages_to_map = (stack_size / PAGE_SIZE) - 1u;
    }

    if (syslog_get_verbose() >= VERBOSE_DEBUG)
        kprintf("[ELF DBG] Mapping %u initial stack pages for page_dir=0x%08x top=0x%08x\n",
                pages_to_map, page_dir, stack_top - PAGE_SIZE);

    for (uint32_t page = 0; page < pages_to_map; page++) {
        uint32_t va = stack_top - ((page + 1u) * PAGE_SIZE);
        uint32_t phys = pmm_alloc_frame();
        if (!phys) {
            kprintf("[ELF DBG] pmm_alloc_frame failed while mapping initial stack pages\n");
            /* Unmap and free all pages already mapped in this loop. */
            for (uint32_t j = 0; j < page; j++) {
                uint32_t unmap_va = stack_top - ((j + 1u) * PAGE_SIZE);
                paging_unmap_in_directory(page_dir, unmap_va);
            }
            return -1;
        }
        if (syslog_get_verbose() >= VERBOSE_DEBUG)
            kprintf("[ELF DBG]  map stack va=0x%08x -> phys=0x%08x\n", va, phys);
        paging_map_in_directory(page_dir, va, phys, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    }
    return 0;
}

static int elf_zero_stack_pages(uint32_t stack_base, uint32_t stack_size) {
    uint32_t stack_top = stack_base + stack_size;
    uint32_t pages_to_zero = ELF_USER_STACK_PREFAULT_PAGES;

    if (stack_size <= PAGE_SIZE) return -1;
    if (pages_to_zero == 0) pages_to_zero = 1;
    if (pages_to_zero > (stack_size / PAGE_SIZE) - 1u) {
        pages_to_zero = (stack_size / PAGE_SIZE) - 1u;
    }

    for (uint32_t page = 0; page < pages_to_zero; page++) {
        uint32_t va = stack_top - ((page + 1u) * PAGE_SIZE);
        memset((void*)va, 0, PAGE_SIZE);
    }
    return 0;
}

static int elf_copy_string(char **stack_ptr, uint32_t stack_base, const char *value, uint32_t *user_addr_out) {
    size_t len = value ? strlen(value) + 1u : 1u;

    *stack_ptr -= len;
    if ((uint32_t)(uintptr_t)(*stack_ptr) < stack_base) return -1;

    if (value) memcpy(*stack_ptr, value, len);
    else (*stack_ptr)[0] = '\0';

    if (user_addr_out) *user_addr_out = (uint32_t)(uintptr_t)(*stack_ptr);
    return 0;
}

int elf_validate(const uint8_t *data, size_t len, const char *name) {
    if (len < sizeof(elf32_ehdr_t)) {
        kprintf("[ELF] Too small to be an ELF file: %s\n", (name && name[0]) ? name : "(unknown)");
        return -1;
    }
    return elf_validate_header((const elf32_ehdr_t*)data, name, 0);
}

int elf_load(const uint8_t *data, size_t len, uint32_t *entry_out) {
    if (elf_validate(data, len, NULL) != 0) return -1;

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
        if (syslog_get_verbose() >= VERBOSE_INFO)
            kprintf("[ELF] Loaded segment %d: vaddr=0x%x size=%d\n",
                    i, ph->p_vaddr, ph->p_filesz);
    }

    if (loaded == 0) {
        kprintf("[ELF] No loadable segments found!\n");
        return -1;
    }

    *entry_out = hdr->e_entry;
    if (syslog_get_verbose() >= VERBOSE_INFO)
        kprintf("[ELF] Entry point: 0x%x - Judo is ready to flip!\n", hdr->e_entry);
    return 0;
}

int elf_build_initial_stack(uint32_t page_dir,
                            const char *const argv[], const char *const envp[],
                            uint32_t *stack_base_out, uint32_t *stack_top_out,
                            uint32_t *stack_pointer_out,
                            const elf_auxv_info_t *auxv_info) {
    size_t argc = elf_vector_count(argv);
    size_t envc = elf_vector_count(envp);
    uint32_t stack_base = elf_alloc_stack_region();
    uint32_t stack_top = stack_base + ELF_USER_STACK_SIZE;
    char *stack_ptr = (char*)(uintptr_t)stack_top;
    uint32_t *arg_ptrs = NULL;
    uint32_t *env_ptrs = NULL;
    uint32_t old_page_dir = paging_current_directory();
    elf_aux_entry_t aux_entries[8];
    size_t aux_count = 0;
    uint32_t random_addr = 0;
    uint32_t execfn_addr = 0;

    if (elf_map_stack_pages(page_dir, stack_base, ELF_USER_STACK_SIZE) != 0) return -1;
    if (syslog_get_verbose() >= VERBOSE_DEBUG)
        kprintf("[ELF DBG] stack_base=0x%08x stack_top=0x%08x\n", stack_base, stack_top);
    paging_switch_directory(page_dir);
    if (elf_zero_stack_pages(stack_base, ELF_USER_STACK_SIZE) != 0) {
        paging_switch_directory(old_page_dir);
        return -1;
    }

    if (argc) {
        arg_ptrs = (uint32_t*)kheap_alloc(sizeof(uint32_t) * argc, 0);
        if (!arg_ptrs) return -1;
    }
    if (envc) {
        env_ptrs = (uint32_t*)kheap_alloc(sizeof(uint32_t) * envc, 0);
        if (!env_ptrs) {
            if (arg_ptrs) kheap_free(arg_ptrs);
            return -1;
        }
    }

    for (size_t index = envc; index > 0; index--) {
        if (elf_copy_string(&stack_ptr, stack_base, envp[index - 1], &env_ptrs[index - 1]) != 0) {
            paging_switch_directory(old_page_dir);
            if (env_ptrs) kheap_free(env_ptrs);
            if (arg_ptrs) kheap_free(arg_ptrs);
            return -1;
        }
    }

    for (size_t index = argc; index > 0; index--) {
        if (elf_copy_string(&stack_ptr, stack_base, argv[index - 1], &arg_ptrs[index - 1]) != 0) {
            paging_switch_directory(old_page_dir);
            if (env_ptrs) kheap_free(env_ptrs);
            if (arg_ptrs) kheap_free(arg_ptrs);
            return -1;
        }
    }

    stack_ptr -= 16u;
    if ((uint32_t)(uintptr_t)stack_ptr < stack_base) {
        paging_switch_directory(old_page_dir);
        if (env_ptrs) kheap_free(env_ptrs);
        if (arg_ptrs) kheap_free(arg_ptrs);
        return -1;
    }
    memset(stack_ptr, 0, 16u);
    random_addr = (uint32_t)(uintptr_t)stack_ptr;

    stack_ptr = (char*)((uint32_t)(uintptr_t)stack_ptr & ~0x3u);

    if (argc && arg_ptrs) execfn_addr = arg_ptrs[0];

    aux_entries[aux_count++] = (elf_aux_entry_t){ AT_PAGESZ, PAGE_SIZE };
    aux_entries[aux_count++] = (elf_aux_entry_t){ AT_RANDOM, random_addr };
    if (auxv_info) {
        if (auxv_info->phdr)  aux_entries[aux_count++] = (elf_aux_entry_t){ AT_PHDR,  auxv_info->phdr };
        if (auxv_info->phent) aux_entries[aux_count++] = (elf_aux_entry_t){ AT_PHENT, auxv_info->phent };
        if (auxv_info->phnum) aux_entries[aux_count++] = (elf_aux_entry_t){ AT_PHNUM, auxv_info->phnum };
        if (auxv_info->entry) aux_entries[aux_count++] = (elf_aux_entry_t){ AT_ENTRY, auxv_info->entry };
        if (auxv_info->base)  aux_entries[aux_count++] = (elf_aux_entry_t){ AT_BASE,  auxv_info->base };
    }
    if (execfn_addr && aux_count < (sizeof(aux_entries) / sizeof(aux_entries[0]))) {
        /* Reuse AT_SECURE slot only when no secure-mode flag needs to be exposed. */
        aux_entries[aux_count++] = (elf_aux_entry_t){ 31u, execfn_addr }; /* AT_EXECFN */
    }

    /* AT_NULL terminator (innermost push = lowest addr = last read). */
    stack_ptr -= sizeof(uint32_t); *(uint32_t*)stack_ptr = 0;
    stack_ptr -= sizeof(uint32_t); *(uint32_t*)stack_ptr = AT_NULL;
    for (size_t index = aux_count; index > 0; index--) {
        stack_ptr -= sizeof(uint32_t);
        *(uint32_t*)stack_ptr = aux_entries[index - 1].value;
        stack_ptr -= sizeof(uint32_t);
        *(uint32_t*)stack_ptr = aux_entries[index - 1].type;
    }

    stack_ptr -= sizeof(uint32_t);
    *(uint32_t*)stack_ptr = 0;
    for (size_t index = envc; index > 0; index--) {
        stack_ptr -= sizeof(uint32_t);
        *(uint32_t*)stack_ptr = env_ptrs[index - 1];
    }

    stack_ptr -= sizeof(uint32_t);
    *(uint32_t*)stack_ptr = 0;
    for (size_t index = argc; index > 0; index--) {
        stack_ptr -= sizeof(uint32_t);
        *(uint32_t*)stack_ptr = arg_ptrs[index - 1];
    }

    stack_ptr -= sizeof(uint32_t);
    *(uint32_t*)stack_ptr = (uint32_t)argc;

    if (env_ptrs) kheap_free(env_ptrs);
    if (arg_ptrs) kheap_free(arg_ptrs);

    paging_switch_directory(old_page_dir);

    if (stack_base_out) *stack_base_out = stack_base;
    if (stack_top_out) *stack_top_out = stack_top;
    if (stack_pointer_out) *stack_pointer_out = (uint32_t)(uintptr_t)stack_ptr;
    if (syslog_get_verbose() >= VERBOSE_DEBUG)
        kprintf("[ELF DBG] final user stack pointer = 0x%08x\n", (uint32_t)(uintptr_t)stack_ptr);
    return 0;
}

static int elf_load_into_address_space(uint32_t page_dir,
                                       const uint8_t *data,
                                       size_t len,
                                       uint32_t *entry_out,
                                       uint32_t *image_end_out) {
    uint32_t old_page_dir = paging_current_directory();
    const elf32_ehdr_t *hdr;
    uint32_t loaded = 0;
    uint32_t image_end = 0;

    if (elf_validate(data, len, NULL) != 0) return -1;

    hdr = (const elf32_ehdr_t*)data;
    paging_switch_directory(page_dir);

    for (uint16_t i = 0; i < hdr->e_phnum; i++) {
        const elf32_phdr_t *ph = (const elf32_phdr_t*)
            (data + hdr->e_phoff + i * hdr->e_phentsize);

        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_filesz == 0) continue;

        if (ph->p_offset + ph->p_filesz > len) {
            kprintf("[ELF] Segment %d extends beyond file!\n", i);
            paging_switch_directory(old_page_dir);
            return -1;
        }

        if (ph->p_vaddr < 0x1000) {
            kprintf("[ELF] Segment %d tries to load at NULL page - denied!\n", i);
            paging_switch_directory(old_page_dir);
            return -1;
        }

        uint32_t flags = PAGE_PRESENT | PAGE_USER;
        if (ph->p_flags & PF_W) flags |= PAGE_WRITABLE;

        uint32_t vstart = ph->p_vaddr & ~0xFFFu;
        uint32_t vend   = (ph->p_vaddr + ph->p_memsz + 0xFFFu) & ~0xFFFu;
        if (vend > image_end) image_end = vend;
        for (uint32_t va = vstart; va < vend; va += PAGE_SIZE) {
            uint32_t phys = pmm_alloc_frame();
            if (!phys) {
                kprintf("[ELF] Out of physical frames!\n");
                paging_switch_directory(old_page_dir);
                return -1;
            }
            paging_map_in_directory(page_dir, va, phys, flags);
        }

        memcpy((void*)ph->p_vaddr, data + ph->p_offset, ph->p_filesz);
        if (ph->p_memsz > ph->p_filesz) {
            memset((void*)(ph->p_vaddr + ph->p_filesz), 0,
                   ph->p_memsz - ph->p_filesz);
        }

        loaded++;
        if (syslog_get_verbose() >= VERBOSE_INFO)
            kprintf("[ELF] Loaded segment %d: vaddr=0x%x size=%d\n",
                    i, ph->p_vaddr, ph->p_filesz);
    }

    paging_switch_directory(old_page_dir);

    if (loaded == 0) {
        kprintf("[ELF] No loadable segments found!\n");
        return -1;
    }

    *entry_out = hdr->e_entry;
    if (image_end_out) *image_end_out = image_end;
    if (syslog_get_verbose() >= VERBOSE_INFO)
        kprintf("[ELF] Entry point: 0x%x - Judo is ready to flip!\n", hdr->e_entry);
    return 0;
}

static int elf_stream_load_metadata_into_address_space(uint32_t page_dir, int fd, size_t file_len,
                                                       const elf_metadata_t *meta, uint32_t load_bias,
                                                       uint32_t *entry_out, uint32_t *image_end_out) {
    uint32_t old_page_dir = paging_current_directory();
    uint32_t loaded = 0;
    uint32_t image_end = 0;
    const elf32_ehdr_t *hdr;

    if (!meta || !meta->phdrs) return -1;
    hdr = &meta->hdr;

    paging_switch_directory(page_dir);

    for (uint16_t i = 0; i < hdr->e_phnum; i++) {
        const elf32_phdr_t *ph = &meta->phdrs[i];
        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_memsz == 0) continue;

        if (ph->p_offset + ph->p_filesz > file_len) {
            kprintf("[ELF] Segment %d extends beyond file!\n", i);
            paging_switch_directory(old_page_dir);
            return -1;
        }

        if (load_bias == 0 && ph->p_vaddr < 0x1000) {
            kprintf("[ELF] Segment %d tries to load at NULL page - denied!\n", i);
            paging_switch_directory(old_page_dir);
            return -1;
        }

        uint32_t flags = PAGE_PRESENT | PAGE_USER;
        if (ph->p_flags & PF_W) flags |= PAGE_WRITABLE;

        uint32_t segment_vaddr = load_bias + ph->p_vaddr;
        uint32_t vstart = segment_vaddr & ~0xFFFu;
        uint32_t vend   = (segment_vaddr + ph->p_memsz + 0xFFFu) & ~0xFFFu;
        if (vend > image_end) image_end = vend;
        for (uint32_t va = vstart; va < vend; va += PAGE_SIZE) {
            uint32_t phys = pmm_alloc_frame();
            if (!phys) {
                kprintf("[ELF] Out of physical frames!\n");
                paging_switch_directory(old_page_dir);
                return -1;
            }
            paging_map_in_directory(page_dir, va, phys, flags);
        }

        /* Stream file contents directly into mapped vaddrs to avoid large
         * kernel heap allocations. Read by pages using vfs_read_at. */
        uint32_t copied = 0;
        while (copied < ph->p_filesz) {
            uint32_t file_off = ph->p_offset + copied;
            uint32_t dest_va = segment_vaddr + copied;
            uint32_t to_read = (uint32_t)PAGE_SIZE - (dest_va & 0xFFFu);
            if (to_read > ph->p_filesz - copied) to_read = (uint32_t)(ph->p_filesz - copied);

            int r = vfs_read_at(fd, (uint8_t*)(uintptr_t)dest_va, to_read, file_off);
            if (r < 0) {
                kprintf("[ELF] Read failed for segment %d at off=%u\n", i, file_off);
                paging_switch_directory(old_page_dir);
                return -1;
            }
            copied += (uint32_t)r;
        }

        /* Zero-fill remaining memsz beyond filesz */
        if (ph->p_memsz > ph->p_filesz) {
            uint32_t zero_addr = segment_vaddr + ph->p_filesz;
            uint32_t zero_len = ph->p_memsz - ph->p_filesz;
            memset((void*)(uintptr_t)zero_addr, 0, zero_len);
        }

        loaded++;
        if (syslog_get_verbose() >= VERBOSE_INFO)
            kprintf("[ELF] Stream-loaded segment %d: vaddr=0x%x size=%d\n",
                    i, ph->p_vaddr, ph->p_filesz);
    }

    paging_switch_directory(old_page_dir);

    if (loaded == 0) {
        kprintf("[ELF] No loadable segments found!\n");
        return -1;
    }

    *entry_out = load_bias + hdr->e_entry;
    if (image_end_out) *image_end_out = image_end;
    if (syslog_get_verbose() >= VERBOSE_INFO)
        kprintf("[ELF] Entry point: 0x%x - Judo is ready to flip!\n", load_bias + hdr->e_entry);
    return 0;
}


int elf_load_image(const char *path, const char *const argv[], const char *const envp[],
                   elf_image_t *image_out) {
    uint32_t page_dir;
    const char *name;
    elf_metadata_t exec_meta;
    elf_metadata_t interp_meta;
    elf_auxv_info_t auxv_info;
    int interp_fd = -1;
    int fd = -1;
    int32_t file_len;
    uint32_t program_image_end = 0;

    if (!path || !image_out) return -1;

    memset(image_out, 0, sizeof(*image_out));
    memset(&exec_meta, 0, sizeof(exec_meta));
    memset(&interp_meta, 0, sizeof(interp_meta));
    memset(&auxv_info, 0, sizeof(auxv_info));
    /* Enforce execute-permission bits when supported by the filesystem.
       If vfs_stat is not available for the filesystem, allow execution. */
    vfs_stat_t st;
    if (vfs_stat(path, &st) == 0) {
        if ((st.mode & 0x49) == 0) {
            kprintf("[ELF] Permission denied: %s not executable (mode=0%o)\n", path, st.mode);
            return -1;
        }
    }

    /* Open the file and determine its length so we can stream segments
     * directly into the new address space without buffering the whole
     * image in the kernel heap. */
    fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        kprintf("[ELF] Open failed for %s\n", path);
        return -1;
    }

    file_len = vfs_lseek(fd, 0, VFS_SEEK_END);
    if (file_len < 0) {
        kprintf("[ELF] Failed to seek EOF for %s\n", path);
        vfs_close(fd);
        return -1;
    }
    /* Rewind to start (not strictly necessary for read_at) */
    vfs_lseek(fd, 0, VFS_SEEK_SET);

    if (elf_read_metadata_fd(fd, (size_t)file_len, path, 0, &exec_meta) != 0) {
        vfs_close(fd);
        return -1;
    }

    page_dir = paging_create_address_space();
    if (!page_dir) {
        elf_free_metadata(&exec_meta);
        vfs_close(fd);
        return -1;
    }

    if (elf_stream_load_metadata_into_address_space(page_dir, fd, (size_t)file_len,
                                                    &exec_meta, 0,
                                                    &image_out->program_entry,
                                                    &program_image_end) != 0) {
        elf_free_metadata(&exec_meta);
        vfs_close(fd);
        paging_destroy_address_space(page_dir);
        return -1;
    }

    image_out->entry = image_out->program_entry;
    image_out->interp_base = 0;
    image_out->image_end = program_image_end;
    auxv_info.phdr = exec_meta.phdr_addr;
    auxv_info.phent = exec_meta.hdr.e_phentsize;
    auxv_info.phnum = exec_meta.hdr.e_phnum;
    auxv_info.entry = image_out->program_entry;

    if (exec_meta.has_interp && exec_meta.interp_path[0]) {
        int32_t interp_len;
        uint32_t interp_entry = 0;
        uint32_t interp_image_end = 0;

        interp_fd = vfs_open(exec_meta.interp_path, VFS_O_RDONLY);
        if (interp_fd < 0) {
            kprintf("[ELF] Failed to open interpreter %s for %s\n", exec_meta.interp_path, path);
            elf_free_metadata(&exec_meta);
            vfs_close(fd);
            paging_destroy_address_space(page_dir);
            return -1;
        }

        interp_len = vfs_lseek(interp_fd, 0, VFS_SEEK_END);
        if (interp_len < 0) {
            kprintf("[ELF] Failed to seek EOF for interpreter %s\n", exec_meta.interp_path);
            elf_free_metadata(&exec_meta);
            vfs_close(interp_fd);
            vfs_close(fd);
            paging_destroy_address_space(page_dir);
            return -1;
        }
        vfs_lseek(interp_fd, 0, VFS_SEEK_SET);

        if (elf_read_metadata_fd(interp_fd, (size_t)interp_len, exec_meta.interp_path, 1, &interp_meta) != 0) {
            elf_free_metadata(&exec_meta);
            vfs_close(interp_fd);
            vfs_close(fd);
            paging_destroy_address_space(page_dir);
            return -1;
        }

        if (interp_meta.hdr.e_type != ET_DYN) {
            kprintf("[ELF] Unsupported interpreter type=%u for %s\n",
                    interp_meta.hdr.e_type, exec_meta.interp_path);
            elf_free_metadata(&interp_meta);
            elf_free_metadata(&exec_meta);
            vfs_close(interp_fd);
            vfs_close(fd);
            paging_destroy_address_space(page_dir);
            return -1;
        }

        if (elf_stream_load_metadata_into_address_space(page_dir, interp_fd, (size_t)interp_len,
                                                        &interp_meta, ELF_INTERP_BASE,
                                                        &interp_entry, &interp_image_end) != 0) {
            elf_free_metadata(&interp_meta);
            elf_free_metadata(&exec_meta);
            vfs_close(interp_fd);
            vfs_close(fd);
            paging_destroy_address_space(page_dir);
            return -1;
        }

        image_out->entry = interp_entry;
        image_out->interp_base = ELF_INTERP_BASE;
        if (interp_image_end > image_out->image_end) image_out->image_end = interp_image_end;
        auxv_info.base = ELF_INTERP_BASE;
    }

    if (elf_build_initial_stack(page_dir, argv, envp,
                                &image_out->stack_base,
                                &image_out->stack_top,
                                &image_out->stack_pointer,
                                &auxv_info) != 0) {
        kprintf("[ELF] Failed to build initial stack for %s\n", path);
        elf_free_metadata(&interp_meta);
        elf_free_metadata(&exec_meta);
        if (interp_fd >= 0) vfs_close(interp_fd);
        vfs_close(fd);
        paging_destroy_address_space(page_dir);
        return -1;
    }

    name = strrchr(path, '/');
    if (name && name[1]) name++;
    else name = path;

    strncpy(image_out->name, name, sizeof(image_out->name) - 1);
    image_out->page_dir = page_dir;
    elf_free_metadata(&interp_meta);
    elf_free_metadata(&exec_meta);
    if (interp_fd >= 0) vfs_close(interp_fd);
    vfs_close(fd);
    return 0;
}

process_t *elf_exec(const char *path, uint32_t uid) {
    static const char *empty_envp[] = { NULL };
    const char *argv_storage[2];
    elf_image_t image;
    process_t *process;
    vfs_stat_t stat;
    uint32_t groups[PROC_MAX_GROUPS];
    vfs_cred_t cred;
    passwd_entry_t passwd;
    uint32_t gid = 0;
    uint32_t euid = uid;
    uint32_t egid = 0;

    if (!path) return NULL;

    argv_storage[0] = path;
    argv_storage[1] = NULL;

    if (multiuser_get_passwd(uid, &passwd) != 0) {
        kprintf("[ELF] Unknown uid %u for %s\n", uid, path);
        return NULL;
    }
    gid = passwd.gid;
    egid = gid;
    memset(&cred, 0, sizeof(cred));
    cred.uid = uid;
    cred.gid = gid;
    cred.group_count = multiuser_get_groups(uid, gid, groups, PROC_MAX_GROUPS);
    cred.groups = groups;

    if (vfs_stat(path, &stat) == 0) {
        if (vfs_access_cred(path, VFS_ACCESS_EXEC, &cred) != 0 || stat.is_dir) {
            kprintf("[ELF] Exec denied for %s (uid=%u)\n", path, uid);
            return NULL;
        }
        if (stat.mode & VFS_S_ISUID) euid = stat.uid;
        if (stat.mode & VFS_S_ISGID) egid = stat.gid;
    }

    if (elf_load_image(path, argv_storage, empty_envp, &image) != 0) {
        return NULL;
    }

    process = process_create_image(image.name, image.entry, image.stack_pointer,
                                   image.stack_base, image.stack_top, image.page_dir,
                                   uid, uid);

    if (!process) return NULL;

    if (image.interp_base != 0) {
        process->flags |= PROC_FLAG_LINUX_ABI;
    }

    process_set_memory_layout(process, image.image_end);
    process_set_effective_ids(process, euid, egid);

    kprintf("[ELF] Prepared %s entry=0x%x stack=0x%x\n", path, image.entry, image.stack_pointer);
    return process;
}
