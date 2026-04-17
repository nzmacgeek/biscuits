// BlueyOS Device Event Channel
// "Claw keeps its ears open so nothing sneaks past!" - Bandit
// Episode ref: "Neighbours" - staying in the loop matters.
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "devev.h"

#define DEVEV_RING_SIZE 64   /* must be a power of two */

static devev_event_t devev_ring[DEVEV_RING_SIZE];
static volatile uint32_t devev_head = 0;  /* write index */
static volatile uint32_t devev_tail = 0;  /* read  index */

static uint32_t devev_irq_save(void) {
    uint32_t flags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void devev_irq_restore(uint32_t flags) {
    __asm__ volatile("push %0; popf" : : "r"(flags) : "memory", "cc");
}

void devev_init(void) {
    devev_head = 0;
    devev_tail = 0;
    memset(devev_ring, 0, sizeof(devev_ring));
    kprintf("[DEV]  Device event channel ready - Claw can listen now!\n");
}

void devev_push(const devev_event_t *ev) {
    uint32_t flags;
    uint32_t next;

    if (!ev) return;

    flags = devev_irq_save();
    next = (devev_head + 1u) & (DEVEV_RING_SIZE - 1u);
    if (next == devev_tail) {
        /* Ring is full — drop the oldest event to make room */
        devev_tail = (devev_tail + 1u) & (DEVEV_RING_SIZE - 1u);
    }
    devev_ring[devev_head] = *ev;
    devev_head = next;
    devev_irq_restore(flags);
}

int devev_pending(void) {
    uint32_t flags = devev_irq_save();
    int pending = (devev_head != devev_tail);
    devev_irq_restore(flags);
    return pending;
}

int devev_read(devev_event_t *ev) {
    uint32_t flags;

    if (!ev) return 0;

    flags = devev_irq_save();
    if (devev_head == devev_tail) {
        devev_irq_restore(flags);
        return 0;
    }

    *ev = devev_ring[devev_tail];
    devev_tail = (devev_tail + 1u) & (DEVEV_RING_SIZE - 1u);
    devev_irq_restore(flags);
    return 1;
}

int devev_read_bytes(uint8_t *buf, size_t len) {
    size_t bytes = 0;

    if (!buf) return 0;

    while (bytes + sizeof(devev_event_t) <= len && devev_pending()) {
        devev_read((devev_event_t *)(buf + bytes));
        bytes += sizeof(devev_event_t);
    }
    return (int)bytes;
}
