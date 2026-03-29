// BlueyOS Network Interface Configuration — "Jack's Network Setup"
// "You can't swim without knowing how the water works!" — Jack Russell
// Episode ref: "Swimming Lessons" — Jack finally gets the right snorkel
//
// ⚠️  VIBE CODED RESEARCH PROJECT — NOT FOR PRODUCTION USE ⚠️
//
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project
// with no affiliation to Ludo Studio or the BBC.

#include "../include/types.h"
#include "../include/bluey.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "../fs/vfs.h"
#include "../net/tcpip.h"
#include "netcfg.h"

// ---------------------------------------------------------------------------
// String helpers (local — we can't use libc)
// ---------------------------------------------------------------------------

// Skip whitespace (spaces and tabs); returns pointer to first non-whitespace
static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

// Check if p starts with word (followed by whitespace or '\0')
static int starts_with_word(const char *p, const char *word) {
    size_t n = strlen(word);
    if (strncmp(p, word, n) != 0) return 0;
    return p[n] == '\0' || p[n] == ' ' || p[n] == '\t' || p[n] == '\r' || p[n] == '\n';
}

// Copy a single whitespace-delimited token from src into dst (max dstlen).
// Returns pointer to first character after the token.
static const char *copy_token(const char *src, char *dst, int dstlen) {
    src = skip_ws(src);
    int i = 0;
    while (*src && *src != ' ' && *src != '\t' &&
           *src != '\r' && *src != '\n' && i < dstlen - 1) {
        dst[i++] = *src++;
    }
    dst[i] = '\0';
    return src;
}

// Parse dotted-decimal IPv4 address string into a uint32_t (host byte order).
// Returns 0 on failure.
static uint32_t parse_ip4(const char *s) {
    uint32_t result = 0;
    int      parts  = 0;
    uint32_t octet  = 0;
    while (*s && parts < 4) {
        if (*s >= '0' && *s <= '9') {
            octet = octet * 10 + (uint32_t)(*s - '0');
        } else if (*s == '.') {
            result = (result << 8) | (octet & 0xFF);
            octet  = 0;
            parts++;
        } else {
            break;
        }
        s++;
    }
    // Final octet
    result = (result << 8) | (octet & 0xFF);
    parts++;
    return (parts == 4) ? result : 0;
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

// We read the whole file into a static buffer then parse line by line.
#define NETCFG_FILE_BUF  4096

static char netcfg_buf[NETCFG_FILE_BUF];

int netcfg_parse(netcfg_iface_t *out, int max) {
    int fd = vfs_open(NETCFG_INTERFACES_PATH, VFS_O_RDONLY);
    if (fd < 0) return -1;

    int total = vfs_read(fd, (uint8_t *)netcfg_buf, NETCFG_FILE_BUF - 1);
    vfs_close(fd);
    if (total <= 0) return 0;
    netcfg_buf[total] = '\0';

    int            nfaces = 0;
    netcfg_iface_t *cur   = NULL;   // currently open stanza

    // Iterate line by line
    char *line = netcfg_buf;
    while (*line) {
        // Find end of line
        char *eol = line;
        while (*eol && *eol != '\n') eol++;
        char saved = *eol;
        *eol = '\0';   // temporarily terminate line

        // Strip inline comment
        char *hash = line;
        while (*hash && *hash != '#') hash++;
        *hash = '\0';

        const char *p = skip_ws(line);

        if (*p == '\0') {
            // blank / comment-only line
        } else if (starts_with_word(p, "auto")) {
            // auto <ifname>  — mark interface as auto-up
            char ifname[16] = {0};
            copy_token(p + 4, ifname, sizeof(ifname));
            // Find or create stanza for ifname
            for (int i = 0; i < nfaces; i++) {
                if (strcmp(out[i].ifname, ifname) == 0) {
                    out[i].is_auto = 1;
                    goto next_line;
                }
            }
            if (nfaces < max) {
                memset(&out[nfaces], 0, sizeof(netcfg_iface_t));
                strncpy(out[nfaces].ifname, ifname, sizeof(out[nfaces].ifname) - 1);
                out[nfaces].is_auto = 1;
                out[nfaces].method  = NETCFG_INET_MANUAL;
                nfaces++;
            }
        } else if (starts_with_word(p, "iface")) {
            // iface <ifname> <family> <method>
            char ifname[16] = {0}, family[8] = {0}, method[16] = {0};
            p = copy_token(p + 5, ifname, sizeof(ifname));
            p = copy_token(p,     family, sizeof(family));
            p = copy_token(p,     method, sizeof(method));

            // Find existing stanza or create a new one
            cur = NULL;
            for (int i = 0; i < nfaces; i++) {
                if (strcmp(out[i].ifname, ifname) == 0) {
                    cur = &out[i];
                    break;
                }
            }
            if (!cur && nfaces < max) {
                memset(&out[nfaces], 0, sizeof(netcfg_iface_t));
                strncpy(out[nfaces].ifname, ifname, sizeof(out[nfaces].ifname) - 1);
                cur = &out[nfaces++];
            }

            if (cur) {
                if (strcmp(method, "static")   == 0) cur->method = NETCFG_INET_STATIC;
                else if (strcmp(method, "dhcp") == 0) cur->method = NETCFG_INET_DHCP;
                else if (strcmp(method, "loopback") == 0) cur->method = NETCFG_INET_LOOPBACK;
                else                                      cur->method = NETCFG_INET_MANUAL;
            }
        } else if (cur) {
            // Option line inside an iface stanza (indented)
            char key[32] = {0}, val[64] = {0};
            p = copy_token(p,   key, sizeof(key));
            p = copy_token(p,   val, sizeof(val));

            if (strcmp(key, "address") == 0) {
                cur->address = parse_ip4(val);
            } else if (strcmp(key, "netmask") == 0) {
                cur->netmask = parse_ip4(val);
            } else if (strcmp(key, "gateway") == 0) {
                cur->gateway = parse_ip4(val);
            } else if (strcmp(key, "dns-nameservers") == 0 ||
                       strcmp(key, "dns-nameserver")  == 0) {
                // Use the first nameserver listed
                char first[24] = {0};
                copy_token(val, first, sizeof(first));
                cur->dns = parse_ip4(first);
            }
            // Other options (network, broadcast, metric, …) are silently ignored.
        }

next_line:
        // Restore and advance past the newline
        if (saved == '\n') { *eol = saved; line = eol + 1; }
        else                {               line = eol;     }
    }

    return nfaces;
}

// ---------------------------------------------------------------------------
// Apply parsed configuration to the TCP/IP stack
// ---------------------------------------------------------------------------

void netcfg_apply(void) {
    netcfg_iface_t ifaces[NETCFG_MAX_IFACES];
    int n = netcfg_parse(ifaces, NETCFG_MAX_IFACES);

    if (n < 0) {
        kprintf("[NCFG] %s not found — using compiled-in defaults\n",
                NETCFG_INTERFACES_PATH);
        return;
    }

    if (n == 0) {
        kprintf("[NCFG] %s is empty — using compiled-in defaults\n",
                NETCFG_INTERFACES_PATH);
        return;
    }

    kprintf("[NCFG] Read %d interface stanza(s) from %s\n",
            n, NETCFG_INTERFACES_PATH);

    for (int i = 0; i < n; i++) {
        netcfg_iface_t *iface = &ifaces[i];

        if (iface->method == NETCFG_INET_LOOPBACK) {
            kprintf("[NCFG] %s: loopback (skipped)\n", iface->ifname);
            continue;
        }

        if (iface->method == NETCFG_INET_DHCP) {
            kprintf("[NCFG] %s: inet dhcp — no DHCP client available\n",
                    iface->ifname);
            kprintf("[NCFG]   See docs/network-setup.md for DHCP guidance.\n");
            continue;
        }

        if (iface->method == NETCFG_INET_STATIC) {
            char ips[20], masks[20], gws[20], dnss[20];
            ip_to_str(htonl(iface->address), ips);
            ip_to_str(htonl(iface->netmask), masks);
            ip_to_str(htonl(iface->gateway), gws);
            ip_to_str(htonl(iface->dns),     dnss);

            kprintf("[NCFG] %s: static  IP=%s  mask=%s  gw=%s  dns=%s\n",
                    iface->ifname, ips, masks, gws, dnss);

            // Apply to TCP/IP stack (addresses stored in network byte order)
            tcpip_set_config(
                htonl(iface->address),
                htonl(iface->gateway),
                htonl(iface->netmask),
                htonl(iface->dns)
            );
            // Only configure the first static interface found
            return;
        }
    }
}
