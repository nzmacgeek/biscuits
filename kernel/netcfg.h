#pragma once
// BlueyOS Network Interface Configuration — "Jack's Network Setup"
// "You can't swim without knowing how the water works!" — Jack Russell
// Episode ref: "Swimming Lessons" — Jack learns the right configuration
//
// Parses /etc/interfaces (Debian-style network interface configuration)
// and applies the settings to the TCP/IP stack.
//
// Supported /etc/interfaces syntax:
//
//   # comment
//   auto eth0
//   iface eth0 inet static
//       address 192.168.1.100
//       netmask 255.255.255.0
//       gateway 192.168.1.1
//       dns-nameservers 8.8.8.8
//
//   # DHCP (placeholder — BlueyOS does not have a DHCP client yet;
//   #        see docs/network-setup.md for guidance)
//   auto eth0
//   iface eth0 inet dhcp
//
// ⚠️  VIBE CODED RESEARCH PROJECT — NOT FOR PRODUCTION USE ⚠️
//
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project
// with no affiliation to Ludo Studio or the BBC.

#include "../include/types.h"

// Path to the interfaces file on the mounted filesystem
#define NETCFG_INTERFACES_PATH  "/etc/interfaces"

// Maximum number of interface stanzas in /etc/interfaces
#define NETCFG_MAX_IFACES  4

// Address family
typedef enum {
    NETCFG_INET_STATIC = 0,
    NETCFG_INET_DHCP,
    NETCFG_INET_LOOPBACK,
    NETCFG_INET_MANUAL,
} netcfg_method_t;

// Parsed configuration for one interface
typedef struct {
    char            ifname[16];
    int             is_auto;        /* 'auto' stanza found */
    netcfg_method_t method;
    uint32_t        address;        /* host byte order */
    uint32_t        netmask;        /* host byte order */
    uint32_t        gateway;        /* host byte order */
    uint32_t        dns;            /* host byte order (first server) */
} netcfg_iface_t;

// Parse /etc/interfaces and populate out[] with up to max entries.
// Returns the number of stanzas found, or -1 if the file cannot be opened.
int netcfg_parse(netcfg_iface_t *out, int max);

// Apply parsed configuration to the TCP/IP stack.
// Calls tcpip_set_config() for the first non-loopback static interface.
// Prints a warning if a DHCP interface is found (no client yet).
void netcfg_apply(void);
