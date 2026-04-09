// BlueyOS Kernel Symbol Table - "Bandit's Toolbox Labels"
// Episode ref: "The Weekend" - everything needs a label so you can find it!
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "../drivers/driver.h"
#include "ksyms.h"
#include "kheap.h"

#define MAX_KSYMS 512

static ksym_t *ksyms[MAX_KSYMS];
static int ksym_count_val = 0;

void ksym_init(void) {
    ksym_count_val = 0;
    for (int i = 0; i < MAX_KSYMS; i++) ksyms[i] = NULL;
    kprintf("[KSYM] Kernel symbol table initialized\n");
}

int ksym_register(const char *name, void *addr) {
    if (!name || !addr) return -1;
    if (ksym_count_val >= MAX_KSYMS) {
        kprintf("[KSYM] Symbol table full!\n");
        return -1;
    }

    // Check for duplicates
    for (int i = 0; i < ksym_count_val; i++) {
        if (ksyms[i] && strcmp(ksyms[i]->name, name) == 0) {
            kprintf("[KSYM] Symbol '%s' already registered\n", name);
            return -1;
        }
    }

    ksym_t *sym = (ksym_t*)kheap_alloc(sizeof(ksym_t), 0);
    if (!sym) return -1;

    sym->name = name;
    sym->addr = addr;
    ksyms[ksym_count_val++] = sym;

    return 0;
}

void *ksym_lookup(const char *name) {
    if (!name) return NULL;

    for (int i = 0; i < ksym_count_val; i++) {
        if (ksyms[i] && strcmp(ksyms[i]->name, name) == 0) {
            return ksyms[i]->addr;
        }
    }

    return NULL;
}

int ksym_count(void) {
    return ksym_count_val;
}

void ksym_list(void) {
    kprintf("[KSYM] Kernel symbols (%d):\n", ksym_count_val);
    for (int i = 0; i < ksym_count_val && i < 20; i++) {
        if (ksyms[i]) {
            kprintf("  %s @ 0x%x\n", ksyms[i]->name, (uint32_t)ksyms[i]->addr);
        }
    }
    if (ksym_count_val > 20) {
        kprintf("  ... and %d more\n", ksym_count_val - 20);
    }
}

// Export essential kernel symbols that modules will need
void ksym_export_core(void) {
    // Memory management
    ksym_register("kheap_alloc", (void*)kheap_alloc);
    ksym_register("kheap_free", (void*)kheap_free);

    // String functions
    ksym_register("memcpy", (void*)memcpy);
    ksym_register("memset", (void*)memset);
    ksym_register("strcmp", (void*)strcmp);
    ksym_register("strncmp", (void*)strncmp);
    ksym_register("strlen", (void*)strlen);
    ksym_register("strncpy", (void*)strncpy);

    // I/O
    ksym_register("kprintf", (void*)kprintf);

    kprintf("[KSYM] Exported %d core symbols\n", ksym_count_val);
}

// Export driver framework symbols
void ksym_export_drivers(void);
void ksym_export_drivers(void) {
    extern int driver_register(struct driver *drv);
    ksym_register("driver_register", (void*)driver_register);
}

// Export networking symbols
void ksym_export_net(void);
void ksym_export_net(void) {
    extern int net_register_interface(const char *name, void *priv);
    ksym_register("net_register_interface", (void*)net_register_interface);
}

