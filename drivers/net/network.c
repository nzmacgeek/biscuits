// BlueyOS Network Interface Registry - "Jack's Network Snorkel"
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../../include/types.h"
#include "../../lib/stdio.h"
#include "../../lib/string.h"
#include "network.h"

static net_interface_t *ifaces[NET_MAX_INTERFACES];
static int iface_count = 0;

void net_init(void) {
    iface_count = 0;
    for (int i = 0; i < NET_MAX_INTERFACES; i++) ifaces[i] = NULL;
}

void net_register_interface(net_interface_t *iface) {
    if (iface_count >= NET_MAX_INTERFACES) {
        kprintf("[NET]  Too many interfaces! Jack needs a bigger snorkel!\n");
        return;
    }

    // If interface name is empty or already exists, assign a dynamic name
    if (iface->name[0] == '\0' || net_get_interface(iface->name)) {
        // Find next available ethX number
        for (int i = 0; i < NET_MAX_INTERFACES; i++) {
            char candidate[16];
            candidate[0] = 'e'; candidate[1] = 't'; candidate[2] = 'h';

            // Simple integer to string conversion
            if (i < 10) {
                candidate[3] = '0' + i;
                candidate[4] = '\0';
            } else {
                candidate[3] = '0' + (i / 10);
                candidate[4] = '0' + (i % 10);
                candidate[5] = '\0';
            }

            // Check if this name is available
            if (!net_get_interface(candidate)) {
                // Copy the new name to the interface
                int j;
                for (j = 0; candidate[j] && j < 15; j++) {
                    iface->name[j] = candidate[j];
                }
                iface->name[j] = '\0';
                break;
            }
        }
    }

    ifaces[iface_count++] = iface;
}

net_interface_t *net_get_interface(const char *name) {
    for (int i = 0; i < iface_count; i++) {
        if (ifaces[i] && strcmp(ifaces[i]->name, name) == 0) return ifaces[i];
    }
    return NULL;
}

int net_send(const char *ifname, const uint8_t *data, uint16_t len) {
    net_interface_t *iface = net_get_interface(ifname);
    if (!iface || !iface->up || !iface->send) return -1;
    int r = iface->send(data, len);
    if (r == 0) iface->tx_packets++;
    else        iface->tx_errors++;
    return r;
}

int net_recv(const char *ifname, uint8_t *buf, uint16_t *len) {
    net_interface_t *iface = net_get_interface(ifname);
    if (!iface || !iface->up || !iface->recv) return -1;
    int r = iface->recv(buf, len);
    if (r == 0) iface->rx_packets++;
    else        iface->rx_errors++;
    return r;
}

void net_print_interfaces(void) {
    kprintf("[NET]  Interfaces:\n");
    for (int i = 0; i < iface_count; i++) {
        net_interface_t *n = ifaces[i];
        kprintf("  %s  %s  MAC %x:%x:%x:%x:%x:%x  rx=%d tx=%d\n",
                n->name, n->up ? "UP" : "DOWN",
                n->mac[0], n->mac[1], n->mac[2],
                n->mac[3], n->mac[4], n->mac[5],
                n->rx_packets, n->tx_packets);
    }
    if (!iface_count) kprintf("  (none)\n");
}
