#pragma once
// BlueyOS Device Event Channel - "Claw's ears are always open!"
// A ring buffer of kernel events that the supervisor (claw) can read.
// Episode ref: "Neighbours" - someone always knows what's happening next door.
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"

// Event types posted to the device event channel
#define DEV_EV_NONE        0   // placeholder / empty
#define DEV_EV_CHILD_EXIT  1   // a child process has exited
#define DEV_EV_MOUNT       2   // a filesystem was mounted
#define DEV_EV_UMOUNT      3   // a filesystem was unmounted
#define DEV_EV_KEY         4   // a key was pressed (supplementary)
#define DEV_EV_DEVICE_ADD  5   // a device driver came online
#define DEV_EV_CTRL_ALT_DEL 6  // Ctrl+Alt+Del keyboard chord was detected

// Fixed size: 16 bytes per event so the ring can be stride-accessed
typedef struct {
    uint8_t  type;       // DEV_EV_*
    uint8_t  _pad[3];
    uint32_t pid;        // relevant pid (for CHILD_EXIT; 0 otherwise)
    uint32_t code;       // exit code / key scancode / reserved
    uint32_t reserved;
} devev_event_t;

void devev_init(void);
void devev_push(const devev_event_t *ev);
int  devev_read(devev_event_t *ev);          // read one event; 0 if none
int  devev_read_bytes(uint8_t *buf, size_t len); // read up to len bytes of events
int  devev_pending(void);                    // 1 if at least one event waiting
