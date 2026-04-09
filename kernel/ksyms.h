#pragma once
// BlueyOS Kernel Symbol Table - "Bandit's Toolbox Labels"
// Episode ref: "The Weekend" - everything needs a label so you can find it!
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"

// Kernel symbol for export to modules
typedef struct {
    const char *name;
    void       *addr;
} ksym_t;

// Symbol table operations
void      ksym_init(void);
int       ksym_register(const char *name, void *addr);
void     *ksym_lookup(const char *name);
int       ksym_count(void);
void      ksym_list(void);

// Export functions for different subsystems
void      ksym_export_core(void);
void      ksym_export_drivers(void);
void      ksym_export_net(void);

// Macro to export a kernel symbol
#define EXPORT_SYMBOL(sym) \
    __attribute__((constructor)) static void __export_##sym(void) { \
        ksym_register(#sym, (void*)sym); \
    }
