#pragma once
// BlueyOS Kernel Module Framework
// "Dad's got a toolbox for everything." - Bluey
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../drivers/driver.h"

#define MAX_MODULES  32

typedef struct module {
    const char *name;
    const char *description;
    driver_t   *driver;     // optional: module provides a driver
    int       (*init)(void);
    int       (*deinit)(void);
    int        loaded;

    // Dynamic loading support
    void      *base_addr;   // loaded module base address (NULL for static)
    size_t     size;        // module memory size
    int        refcount;    // usage count
    int        is_dynamic;  // 1 if loaded from disk, 0 if statically registered
} module_t;

void      module_framework_init(void);
int       module_register(module_t *mod);
module_t *module_find(const char *name);
int       module_load(const char *name);
int       module_load_all(void);
void      module_list(void);

// Dynamic module loading
int       module_load_from_file(const char *path);
int       module_unload(const char *name);
int       module_load_from_memory(const char *name, const uint8_t *data, size_t len);
