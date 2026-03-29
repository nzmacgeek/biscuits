#pragma once
// BlueyOS Driver Framework
// "Every good cubby house needs the right parts!" - Bandit
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"

#define MAX_DRIVERS      32
#define DRIVER_NAME_LEN  32

typedef enum {
    DRIVER_CHAR  = 0,   // character device (keyboard, serial)
    DRIVER_BLOCK = 1,   // block device (ATA disk)
    DRIVER_NET   = 2,   // network device (NE2000)
} driver_type_t;

typedef struct driver {
    char          name[DRIVER_NAME_LEN];
    driver_type_t type;
    int  (*init)(void);
    int  (*deinit)(void);
    int  (*read)(uint8_t *buf, size_t len);
    int  (*write)(const uint8_t *buf, size_t len);
    int  (*ioctl)(uint32_t cmd, void *arg);
    int  present;   // 1 if driver initialised successfully
} driver_t;

void      driver_framework_init(void);
int       driver_register(driver_t *drv);
driver_t *driver_find(const char *name);
void      driver_list(void);
int       driver_count(void);
