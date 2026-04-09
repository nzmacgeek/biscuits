// BlueyOS Kernel Module Framework
// "Dad's got a toolbox for everything." - Bluey
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "../fs/vfs.h"
#include "module.h"
#include "module_elf.h"
#include "kheap.h"

static module_t *modules[MAX_MODULES];
static int module_count = 0;

void module_framework_init(void) {
    module_count = 0;
    for (int i = 0; i < MAX_MODULES; i++) modules[i] = NULL;
    kprintf("[MOD]  Module framework ready - Bluey's toolbox is open!\n");
}

int module_register(module_t *mod) {
    if (!mod || !mod->name) return -1;
    if (module_count >= MAX_MODULES) {
        kprintf("[MOD]  ERROR: module table full!\n");
        return -1;
    }
    modules[module_count++] = mod;
    kprintf("[MOD]  Registered module '%s'\n", mod->name);
    return 0;
}

module_t *module_find(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < module_count; i++) {
        if (modules[i] && strcmp(modules[i]->name, name) == 0) {
            return modules[i];
        }
    }
    return NULL;
}

static int module_load_driver(module_t *mod) {
    if (!mod || !mod->driver) return -1;
    if (driver_register(mod->driver) != 0) return -1;
    return 0;
}

int module_load(const char *name) {
    module_t *mod = module_find(name);
    if (!mod) {
        kprintf("[MOD]  Module '%s' not found\n", name ? name : "(null)");
        return -1;
    }
    if (mod->loaded) return 0;

    int result = 0;
    if (mod->driver) {
        result = module_load_driver(mod);
        if (result != 0) {
            kprintf("[MOD]  Module '%s' driver init failed\n", mod->name);
            return -1;
        }
    }
    if (mod->init) {
        result = mod->init();
    }

    if (result != 0) {
        kprintf("[MOD]  Module '%s' init failed\n", mod->name);
        return -1;
    }

    mod->loaded = 1;
    kprintf("[MOD]  Loaded module '%s'\n", mod->name);
    return 0;
}

int module_load_all(void) {
    int loaded = 0;
    for (int i = 0; i < module_count; i++) {
        if (!modules[i]) continue;
        if (module_load(modules[i]->name) == 0) loaded++;
    }
    return loaded;
}

void module_list(void) {
    kprintf("[MOD]  Modules (%d):\n", module_count);
    for (int i = 0; i < module_count; i++) {
        if (!modules[i]) continue;
        kprintf("  [%d] %s  %s %s\n",
                i,
                modules[i]->name,
                modules[i]->loaded ? "loaded" : "unloaded",
                modules[i]->is_dynamic ? "(dynamic)" : "(static)");
    }
}

// Dynamic module loading from memory buffer
// If name is NULL, the name is derived from the module's embedded module_info.name
int module_load_from_memory(const char *name, const uint8_t *data, size_t len) {
    if (!data || len == 0) {
        kprintf("[MOD]  Invalid parameters for module_load_from_memory\n");
        return -1;
    }

    // Load ELF module first to get module_info (and hence the real name)
    void *base = NULL;
    size_t size = 0;
    module_info_t *info = NULL;

    if (module_elf_load(data, len, &base, &size, &info) != 0) {
        kprintf("[MOD]  Failed to load ELF module\n");
        return -1;
    }

    // Determine module name: prefer embedded module_info.name over caller-supplied name
    const char *effective_name = name;
    if (info && info->name && info->name[0]) {
        effective_name = info->name;
    }
    if (!effective_name || !effective_name[0]) {
        kprintf("[MOD]  Cannot determine module name\n");
        module_elf_unload(base, size);
        return -1;
    }

    // Check if module already loaded (now that we know the real name)
    if (module_find(effective_name)) {
        kprintf("[MOD]  Module '%s' already loaded\n", effective_name);
        module_elf_unload(base, size);
        return -1;
    }

    // Allocate module structure
    module_t *mod = (module_t*)kheap_alloc(sizeof(module_t), 0);
    if (!mod) {
        kprintf("[MOD]  Failed to allocate module structure\n");
        module_elf_unload(base, size);
        return -1;
    }

    memset(mod, 0, sizeof(module_t));
    mod->is_dynamic = 1;

    // Allocate name copy
    size_t name_len = strlen(effective_name);
    char *name_copy = (char*)kheap_alloc(name_len + 1, 0);
    if (!name_copy) {
        kheap_free(mod);
        module_elf_unload(base, size);
        return -1;
    }
    strncpy(name_copy, effective_name, name_len);
    name_copy[name_len] = '\0';
    mod->name = name_copy;

    mod->base_addr = base;
    mod->size = size;

    // If we found module_info, use it
    if (info) {
        mod->description = info->description;
        mod->init = info->init;
        mod->deinit = info->exit;
    }

    // Register the module
    if (module_register(mod) != 0) {
        module_elf_unload(base, size);
        kheap_free((void*)name_copy);
        kheap_free(mod);
        return -1;
    }

    // Call init if present; on failure, undo registration and free resources
    if (mod->init) {
        if (mod->init() != 0) {
            kprintf("[MOD]  Module '%s' init failed\n", effective_name);
            // Remove from registry
            for (int i = 0; i < module_count; i++) {
                if (modules[i] == mod) {
                    for (int j = i; j < module_count - 1; j++) {
                        modules[j] = modules[j + 1];
                    }
                    modules[module_count - 1] = NULL;
                    module_count--;
                    break;
                }
            }
            module_elf_unload(base, size);
            kheap_free((void*)name_copy);
            kheap_free(mod);
            return -1;
        }
    }

    mod->loaded = 1;
    kprintf("[MOD]  Dynamically loaded module '%s' at 0x%x\n", effective_name, (uint32_t)base);
    return 0;
}

// Load module from file
int module_load_from_file(const char *path) {
    if (!path) {
        kprintf("[MOD]  No path specified\n");
        return -1;
    }

    kprintf("[MOD]  Loading module from %s\n", path);

    // Read file
    int fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        kprintf("[MOD]  Failed to open %s\n", path);
        return -1;
    }

    // Allocate buffer (max 1MB for a module)
    size_t max_size = 1024 * 1024;
    uint8_t *buffer = (uint8_t*)kheap_alloc(max_size, 0);
    if (!buffer) {
        vfs_close(fd);
        return -1;
    }

    // Read file
    int total = 0;
    while (total < (int)max_size) {
        int remaining = (int)max_size - total;
        int to_read = remaining < 4096 ? remaining : 4096;
        int n = vfs_read(fd, buffer + total, to_read);
        if (n <= 0) break;
        total += n;
    }

    vfs_close(fd);

    if (total == (int)max_size) {
        kprintf("[MOD]  Module file %s is too large (max %d bytes)\n", path, (int)max_size);
        kheap_free(buffer);
        return -1;
    }

    if (total == 0) {
        kprintf("[MOD]  Empty file %s\n", path);
        kheap_free(buffer);
        return -1;
    }

    // Extract module name from path (last component without .ko)
    const char *name_start = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/') name_start = p + 1;
    }

    char name[64];
    int i = 0;
    while (name_start[i] && name_start[i] != '.' && i < 63) {
        name[i] = name_start[i];
        i++;
    }
    name[i] = '\0';

    // Load module
    int result = module_load_from_memory(name, buffer, total);
    kheap_free(buffer);
    return result;
}

// Unload a module
int module_unload(const char *name) {
    module_t *mod = module_find(name);
    if (!mod) {
        kprintf("[MOD]  Module '%s' not found\n", name);
        return -1;
    }

    if (!mod->is_dynamic) {
        kprintf("[MOD]  Cannot unload static module '%s'\n", name);
        return -1;
    }

    if (mod->refcount > 0) {
        kprintf("[MOD]  Module '%s' is in use (refcount=%d)\n", name, mod->refcount);
        return -1;
    }

    // Call cleanup
    if (mod->deinit) {
        mod->deinit();
    }

    // Free module memory
    if (mod->base_addr) {
        module_elf_unload(mod->base_addr, mod->size);
    }

    // Remove from list
    for (int i = 0; i < module_count; i++) {
        if (modules[i] == mod) {
            for (int j = i; j < module_count - 1; j++) {
                modules[j] = modules[j + 1];
            }
            modules[module_count - 1] = NULL;
            module_count--;
            break;
        }
    }

    // Free module structure
    if (mod->name) kheap_free((void*)mod->name);
    kheap_free(mod);

    kprintf("[MOD]  Unloaded module '%s'\n", name);
    return 0;
}
