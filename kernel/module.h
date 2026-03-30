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
} module_t;

void      module_framework_init(void);
int       module_register(module_t *mod);
module_t *module_find(const char *name);
int       module_load(const char *name);
int       module_load_all(void);
void      module_list(void);
