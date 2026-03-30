// BlueyOS Kernel Module Framework
// "Dad's got a toolbox for everything." - Bluey
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "module.h"

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
    } else if (mod->init) {
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
        kprintf("  [%d] %s  %s\n",
                i,
                modules[i]->name,
                modules[i]->loaded ? "loaded" : "unloaded");
    }
}
