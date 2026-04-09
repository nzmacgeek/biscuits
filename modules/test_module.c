// BlueyOS Test Module - "Dad's First Module"
// Simple test module to verify dynamic module loading works
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.

#include "../kernel/module_elf.h"

// Kernel functions we'll use (imported via symbol table)
extern int kprintf(const char *fmt, ...);

static int test_init(void) {
    kprintf("[TEST_MODULE] Hello from dynamically loaded module!\n");
    kprintf("[TEST_MODULE] This proves the module loader works!\n");
    return 0;
}

static int test_exit(void) {
    kprintf("[TEST_MODULE] Goodbye from test module\n");
    return 0;
}

// Module metadata - must be named "module_info"
module_info_t module_info = {
    .name        = "test_module",
    .description = "Simple test module for dynamic loading",
    .version     = "1.0",
    .author      = "BlueyOS",
    .init        = test_init,
    .exit        = test_exit,
};
