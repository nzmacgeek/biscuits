// BlueyOS Loopback Network Interface
// "Let's run around the yard!" - Bluey
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../../include/types.h"
#include "../../lib/string.h"
#include "../../lib/stdio.h"
#include "network.h"
#include "loopback.h"

#define LOOPBACK_QUEUE_LEN  8
#define LOOPBACK_FRAME_MAX  1536

typedef struct {
    uint16_t len;
    uint8_t  data[LOOPBACK_FRAME_MAX];
} loopback_frame_t;

static loopback_frame_t loopback_queue[LOOPBACK_QUEUE_LEN];
static uint32_t loopback_head = 0;
static uint32_t loopback_tail = 0;

static int loopback_queue_full(void) {
    return ((loopback_head + 1) % LOOPBACK_QUEUE_LEN) == loopback_tail;
}

static int loopback_queue_empty(void) {
    return loopback_head == loopback_tail;
}

static int loopback_send(const uint8_t *data, uint16_t len) {
    if (!data || len == 0 || len > LOOPBACK_FRAME_MAX) return -1;
    if (loopback_queue_full()) return -1;

    loopback_queue[loopback_head].len = len;
    memcpy(loopback_queue[loopback_head].data, data, len);
    loopback_head = (loopback_head + 1) % LOOPBACK_QUEUE_LEN;
    return 0;
}

static int loopback_recv(uint8_t *buf, uint16_t *len) {
    if (!buf || !len) return -1;
    if (loopback_queue_empty()) return -1;

    loopback_frame_t *frame = &loopback_queue[loopback_tail];
    if (frame->len > LOOPBACK_FRAME_MAX) return -1;

    memcpy(buf, frame->data, frame->len);
    *len = frame->len;
    loopback_tail = (loopback_tail + 1) % LOOPBACK_QUEUE_LEN;
    return 0;
}

int loopback_init(void) {
    static net_interface_t loopback_iface;
    memset(&loopback_iface, 0, sizeof(loopback_iface));
    strncpy(loopback_iface.name, "lo", sizeof(loopback_iface.name) - 1);
    memset(loopback_iface.mac, 0, sizeof(loopback_iface.mac));
    loopback_iface.send = loopback_send;
    loopback_iface.recv = loopback_recv;
    loopback_iface.up   = 1;

    loopback_head = 0;
    loopback_tail = 0;
    net_register_interface(&loopback_iface);
    kprintf("[NET]  Loopback interface 'lo' ready\n");
    return 0;
}
