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

#define ELF_READ_CHUNK_SIZE   512u
#define ELF_MAX_IMAGE_SIZE    (1024u * 1024u)
#define ELF_USER_STACK_BASE   0x70000000u
#define ELF_USER_STACK_SIZE   (PAGE_SIZE * 4u)
#define ELF_USER_STACK_STRIDE 0x00010000u

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

static uint32_t elf_alloc_stack_region(void) {
    uint32_t base = elf_next_stack_base;
    elf_next_stack_base += ELF_USER_STACK_STRIDE;
    return base;
}

static int elf_map_stack_pages(uint32_t page_dir, uint32_t stack_base, uint32_t stack_size) {
    for (uint32_t addr = stack_base; addr < stack_base + stack_size; addr += PAGE_SIZE) {
        uint32_t phys = pmm_alloc_frame();
        if (!phys) return -1;
        paging_map_in_directory(page_dir, addr, phys, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    }
    return 0;
}

static int elf_zero_stack_pages(uint32_t stack_base, uint32_t stack_size) {
    for (uint32_t addr = stack_base; addr < stack_base + stack_size; addr += PAGE_SIZE) {
        memset((void*)addr, 0, PAGE_SIZE);
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

int elf_build_initial_stack(uint32_t page_dir,
                            const char *const argv[], const char *const envp[],
                            uint32_t *stack_base_out, uint32_t *stack_top_out,
                            uint32_t *stack_pointer_out) {
    size_t argc = elf_vector_count(argv);
    size_t envc = elf_vector_count(envp);
    uint32_t stack_base = elf_alloc_stack_region();
    uint32_t stack_top = stack_base + ELF_USER_STACK_SIZE;
    char *stack_ptr = (char*)(uintptr_t)stack_top;
    uint32_t *arg_ptrs = NULL;
    uint32_t *env_ptrs = NULL;
    uint32_t old_page_dir = paging_current_directory();

    if (elf_map_stack_pages(page_dir, stack_base, ELF_USER_STACK_SIZE) != 0) return -1;

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

    stack_ptr = (char*)((uint32_t)(uintptr_t)stack_ptr & ~0x3u);

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

    if (elf_validate(data, len) != 0) return -1;

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
    kprintf("[ELF] Entry point: 0x%x - Judo is ready to flip!\n", hdr->e_entry);
    return 0;
}

int elf_load_image(const char *path, const char *const argv[], const char *const envp[],
                   elf_image_t *image_out) {
    uint8_t *data = NULL;
    size_t len = 0;
    uint32_t page_dir;
    const char *name;

    if (!path || !image_out) return -1;

    memset(image_out, 0, sizeof(*image_out));
    /* Enforce execute-permission bits when supported by the filesystem.
       If vfs_stat is not available for the filesystem, allow execution. */
    uint16_t mode = 0;
    if (vfs_stat(path, &mode) == 0) {
        /* check any execute bit: owner/group/other
           Mode bit masks: owner=0x40, group=0x8, other=0x1 (octal 0100/0010/0001)
           Combined mask = 0x49 */
        if ((mode & 0x49) == 0) {
            kprintf("[ELF] Permission denied: %s not executable (mode=0%o)\n", path, mode);
            return -1;
        }
    }

    if (elf_read_file(path, &data, &len) != 0) {
        kprintf("[ELF] Failed to read %s\n", path);
        return -1;
    }

    page_dir = paging_create_address_space();
    if (!page_dir) {
        kheap_free(data);
        return -1;
    }

    if (!data || elf_load_into_address_space(page_dir, data, len,
                                             &image_out->entry,
                                             &image_out->image_end) != 0) {
        if (data) kheap_free(data);
        paging_destroy_address_space(page_dir);
        return -1;
    }

    if (elf_build_initial_stack(page_dir, argv, envp,
                                &image_out->stack_base,
                                &image_out->stack_top,
                                &image_out->stack_pointer) != 0) {
        kprintf("[ELF] Failed to build initial stack for %s\n", path);
        kheap_free(data);
        paging_destroy_address_space(page_dir);
        return -1;
    }

    name = strrchr(path, '/');
    if (name && name[1]) name++;
    else name = path;

    strncpy(image_out->name, name, sizeof(image_out->name) - 1);
    image_out->page_dir = page_dir;
    kheap_free(data);
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

    process_set_memory_layout(process, image.image_end);
    process_set_effective_ids(process, euid, egid);

    kprintf("[ELF] Prepared %s entry=0x%x stack=0x%x\n", path, image.entry, image.stack_pointer);
    return process;
}
