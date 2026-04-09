// Built-in driver modules for BlueyOS
// "Bandit's toolbox is full of surprises!" - Bluey
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../kernel/module.h"
#include "../lib/stdio.h"
#include "driver.h"
#include "keyboard.h"
#include "ata.h"
#include "net/ne2000.h"
#include "net/rtl8139.h"
#include "net/3c509.h"
#include "net/e1000.h"

static int keyboard_driver_init(void) { keyboard_init(); return 0; }
static int ata_driver_init(void)      { return ata_init(); }
static int ne2000_driver_init(void)   { ne2000_init(); return 0; }
static int rtl8139_driver_init(void)  { rtl8139_init(); return 0; }
static int el3_driver_init(void)      { el3_init(); return 0; }
static int e1000_driver_init(void)    { e1000_init(); return 0; }

static driver_t keyboard_driver = {
    .name = "keyboard",
    .type = DRIVER_CHAR,
    .init = keyboard_driver_init,
};

static driver_t ata_driver = {
    .name = "ata",
    .type = DRIVER_BLOCK,
    .init = ata_driver_init,
};

static driver_t ne2000_driver = {
    .name = "ne2000",
    .type = DRIVER_NET,
    .init = ne2000_driver_init,
};

static driver_t rtl8139_driver = {
    .name = "rtl8139",
    .type = DRIVER_NET,
    .init = rtl8139_driver_init,
};

static driver_t el3_driver = {
    .name = "3c509",
    .type = DRIVER_NET,
    .init = el3_driver_init,
};

static driver_t e1000_driver = {
    .name = "e1000",
    .type = DRIVER_NET,
    .init = e1000_driver_init,
};

static module_t keyboard_module = {
    .name = "keyboard",
    .description = "PS/2 keyboard driver",
    .driver = &keyboard_driver,
};

static module_t ata_module = {
    .name = "ata",
    .description = "ATA PIO disk driver",
    .driver = &ata_driver,
};

static module_t ne2000_module = {
    .name = "ne2000",
    .description = "NE2000 network driver",
    .driver = &ne2000_driver,
};

static module_t rtl8139_module = {
    .name = "rtl8139",
    .description = "RTL8139 network driver",
    .driver = &rtl8139_driver,
};

static module_t el3_module = {
    .name = "3c509",
    .description = "3Com EtherLink III (3c509) network driver",
    .driver = &el3_driver,
};

static module_t e1000_module = {
    .name = "e1000",
    .description = "Intel e1000 Gigabit Ethernet driver",
    .driver = &e1000_driver,
};

void driver_modules_register(void) {
    module_register(&keyboard_module);
    module_register(&ata_module);
    module_register(&ne2000_module);
    module_register(&rtl8139_module);
    module_register(&el3_module);
    module_register(&e1000_module);
    kprintf("[MOD]  Built-in driver modules registered\n");
}
