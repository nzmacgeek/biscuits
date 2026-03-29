// BlueyOS Driver Framework Implementation
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "driver.h"

static driver_t *drivers[MAX_DRIVERS];
static int driver_count_val = 0;

void driver_framework_init(void) {
    driver_count_val = 0;
    for (int i = 0; i < MAX_DRIVERS; i++) drivers[i] = NULL;
    kprintf("[DRV]  Driver framework ready - Bandit's toolbox is open!\n");
}

int driver_register(driver_t *drv) {
    if (driver_count_val >= MAX_DRIVERS) {
        kprintf("[DRV]  ERROR: driver table full!\n");
        return -1;
    }
    if (drv->init && drv->init() != 0) {
        kprintf("[DRV]  Driver '%s' init failed\n", drv->name);
        drv->present = 0;
        return -1;
    }
    drv->present = 1;
    drivers[driver_count_val++] = drv;
    kprintf("[DRV]  Registered driver '%s' (type=%d)\n", drv->name, drv->type);
    return 0;
}

driver_t *driver_find(const char *name) {
    for (int i = 0; i < driver_count_val; i++) {
        if (drivers[i] && strcmp(drivers[i]->name, name) == 0)
            return drivers[i];
    }
    return NULL;
}

void driver_list(void) {
    kprintf("[DRV]  Registered drivers (%d):\n", driver_count_val);
    for (int i = 0; i < driver_count_val; i++) {
        if (drivers[i])
            kprintf("  [%d] %s  type=%d  present=%d\n",
                    i, drivers[i]->name, drivers[i]->type, drivers[i]->present);
    }
}

int driver_count(void) { return driver_count_val; }
