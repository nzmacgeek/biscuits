// BlueyOS Kernel Syslog — "Bandit's Homework Diary"
// "Dad! You're supposed to write it down!" — Bluey Heeler
// Episode ref: "Markets" — everything gets recorded eventually
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
#include "../drivers/vga.h"
#include "../fs/vfs.h"
#include "syslog.h"
#include <stdarg.h>
#include "kheap.h"

// ---------------------------------------------------------------------------
// Ring buffer state
// ---------------------------------------------------------------------------

// We store entries in a flat array (not a byte ring) for simplicity.
// With SYSLOG_RING_ENTRIES entries of ~296 bytes each this is ~30 KB —
// acceptable for a kernel research OS with a 1 MB heap.
#define SYSLOG_RING_ENTRIES  128

static syslog_entry_t syslog_ring[SYSLOG_RING_ENTRIES];
static uint32_t       syslog_head  = 0;   /* next slot to write into */
static uint32_t       syslog_count_val = 0;
static uint32_t       syslog_seq   = 0;
static int            syslog_ready = 0;

/* Persistent flush history (records recent callers of syslog_flush_to_fs) */
#define FLUSH_HIST_ENTRIES 64
typedef struct {
    uint32_t seq;    /* syslog sequence number at time of flush */
    void    *caller; /* return address of caller */
} flush_record_t;
static flush_record_t syslog_flush_hist[FLUSH_HIST_ENTRIES];
static uint32_t syslog_flush_hist_head = 0;
static uint32_t syslog_flush_hist_count = 0;

// Path where we write the kernel log on the mounted filesystem
#define KERNEL_LOG_PATH  "/var/log/kernel.log"

// ---------------------------------------------------------------------------
// Minimal vsnprintf for the syslog (uses kprintf-style format strings).
// We rely on the existing lib/stdio kprintf but need a buffer version.
// Since lib/stdio.h only exposes kprintf, we implement our own tiny one.
// ---------------------------------------------------------------------------

static void syslog_itoa(uint32_t v, char *buf, int base, int pad) {
    static const char digits[] = "0123456789abcdef";
    char tmp[32];
    int  i = 0;
    /* Defensive guard: avoid division-by-zero if caller passes base==0.
     * Log caller to VGA (avoid calling syslog to prevent recursion) and
     * fall back to base 10.
     */
    if (base == 0) {
        void *caller = __builtin_return_address(0);
        vga_set_color(VGA_LIGHT_BROWN, VGA_BLACK);
        kprintf("[SYSLOG DBG] itoa called with base=0 from %p\n", caller);
        vga_set_color(VGA_WHITE, VGA_BLACK);
        base = 10;
    }
    if (v == 0) { tmp[i++] = '0'; }
    while (v) { tmp[i++] = digits[v % (uint32_t)base]; v /= (uint32_t)base; }
    while (i < pad) tmp[i++] = '0';
    // reverse
    int j = 0;
    while (--i >= 0) buf[j++] = tmp[i];
    buf[j] = '\0';
}

// Very small vsnprintf that handles %s, %d, %u, %x, %c, %%
static int syslog_vsnprintf(char *out, size_t sz, const char *fmt, va_list ap) {
    size_t pos = 0;
    char tmp[32];

#define PUT(c) do { if (pos + 1 < sz) out[pos++] = (c); } while (0)

    while (*fmt && pos + 1 < sz) {
        if (*fmt != '%') { PUT(*fmt++); continue; }
        fmt++;
        if (*fmt == '\0') break;
        if (*fmt == '%') { PUT('%'); fmt++; continue; }

        // Minimal width / padding (just digits for now)
        int pad = 0;
        while (*fmt >= '0' && *fmt <= '9') { pad = pad * 10 + (*fmt++ - '0'); }

        char spec = *fmt++;
        switch (spec) {
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                while (*s && pos + 1 < sz) out[pos++] = *s++;
                break;
            }
            case 'd': {
                int v = va_arg(ap, int);
                uint32_t uv;
                if (v < 0) {
                    PUT('-');
                    /* compute magnitude in unsigned to avoid UB for INT_MIN */
                    uv = (uint32_t)(0u - (uint32_t)v);
                } else {
                    uv = (uint32_t)v;
                }
                syslog_itoa(uv, tmp, 10, 0);
                for (int i = 0; tmp[i] && pos + 1 < sz; i++) out[pos++] = tmp[i];
                break;
            }
            case 'u': {
                syslog_itoa(va_arg(ap, uint32_t), tmp, 10, 0);
                for (int i = 0; tmp[i] && pos + 1 < sz; i++) out[pos++] = tmp[i];
                break;
            }
            case 'x': case 'X': {
                syslog_itoa(va_arg(ap, uint32_t), tmp, 16, pad);
                for (int i = 0; tmp[i] && pos + 1 < sz; i++) out[pos++] = tmp[i];
                break;
            }
            case 'c':
                PUT((char)va_arg(ap, int));
                break;
            default:
                PUT('%'); PUT(spec);
                break;
        }
    }
    out[pos] = '\0';
    return (int)pos;
#undef PUT
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void syslog_init(void) {
    syslog_head      = 0;
    syslog_count_val = 0;
    syslog_seq       = 0;
    syslog_ready     = 1;
}

static int syslog_snprintf(char *out, size_t sz, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = syslog_vsnprintf(out, sz, fmt, ap);
    va_end(ap);
    return r;
}

void syslog_write(int level, const char *tag, const char *fmt, ...) {
    if (!syslog_ready) return;

    syslog_entry_t *entry = &syslog_ring[syslog_head % SYSLOG_RING_ENTRIES];

    entry->seq   = syslog_seq++;
    entry->level = (uint8_t)level;

    // Copy tag (truncate to 15 chars)
    strncpy(entry->tag, tag ? tag : "KERN", sizeof(entry->tag) - 1);
    entry->tag[sizeof(entry->tag) - 1] = '\0';

    // Format message
    va_list ap;
    va_start(ap, fmt);
    syslog_vsnprintf(entry->msg, sizeof(entry->msg), fmt, ap);
    va_end(ap);

    syslog_head++;
    if (syslog_count_val < SYSLOG_RING_ENTRIES) syslog_count_val++;
}

uint32_t syslog_count(void) {
    return syslog_count_val;
}

// ---------------------------------------------------------------------------
// dmesg — print ring buffer contents to VGA console
// ---------------------------------------------------------------------------

static const char *level_str(int l) {
    switch (l) {
        case LOG_EMERG:   return "EMERG  ";
        case LOG_ALERT:   return "ALERT  ";
        case LOG_CRIT:    return "CRIT   ";
        case LOG_ERR:     return "ERR    ";
        case LOG_WARNING: return "WARNING";
        case LOG_NOTICE:  return "NOTICE ";
        case LOG_INFO:    return "INFO   ";
        case LOG_DEBUG:   return "DEBUG  ";
        default:          return "?      ";
    }
}

void syslog_dmesg(void) {
    uint32_t n = syslog_count_val;
    // The oldest entry is at (syslog_head - n) mod SYSLOG_RING_ENTRIES
    uint32_t start = (syslog_head - n) % SYSLOG_RING_ENTRIES;

    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    kprintf("-- Kernel log (%u entries) --\n", n);
    vga_set_color(VGA_WHITE, VGA_BLACK);

    for (uint32_t i = 0; i < n; i++) {
        syslog_entry_t *e = &syslog_ring[(start + i) % SYSLOG_RING_ENTRIES];
        // Colour-code by severity
        if (e->level <= LOG_ERR)      vga_set_color(VGA_LIGHT_RED,   VGA_BLACK);
        else if (e->level == LOG_WARNING) vga_set_color(VGA_LIGHT_BROWN, VGA_BLACK);
        else if (e->level == LOG_DEBUG)   vga_set_color(VGA_DARK_GREY,   VGA_BLACK);
        else                              vga_set_color(VGA_WHITE,       VGA_BLACK);

        kprintf("[%u] %s [%s] %s\n",
                e->seq, level_str((int)e->level), e->tag, e->msg);
    }
    vga_set_color(VGA_WHITE, VGA_BLACK);
    kprintf("-- end of kernel log --\n");
}

// Lightweight helper to let other subsystems record a caller address into
// the persistent syslog flush history buffer. Kept tiny to avoid heavy
// dependencies or recursion into syslog helpers.
void syslog_record_caller(void *caller) {
    uint32_t fh_idx = syslog_flush_hist_head % FLUSH_HIST_ENTRIES;
    syslog_flush_hist[fh_idx].seq = syslog_seq;
    syslog_flush_hist[fh_idx].caller = caller;
    syslog_flush_hist_head++;
    if (syslog_flush_hist_count < FLUSH_HIST_ENTRIES) syslog_flush_hist_count++;
}

// ---------------------------------------------------------------------------
// syslog_flush_to_fs — write ring buffer to /var/log/kernel.log
// ---------------------------------------------------------------------------

void syslog_flush_to_fs(void) {
    /* Debug: record the direct caller address to help track corruption sources.
     * Avoid calling syslog_* helpers to prevent recursion; print to VGA instead.
     */
    void *caller = __builtin_return_address(0);
    /* Record this flush invocation into the file-scope history */
    uint32_t fh_idx = syslog_flush_hist_head % FLUSH_HIST_ENTRIES;
    syslog_flush_hist[fh_idx].seq = syslog_seq;
    syslog_flush_hist[fh_idx].caller = caller;
    syslog_flush_hist_head++;
    if (syslog_flush_hist_count < FLUSH_HIST_ENTRIES) syslog_flush_hist_count++;

#ifdef DEBUG
    /* Print the caller and a short history for immediate debugging */
    vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
    kprintf("--- SYSLOG_FLUSH called by %p (seq=%u) ---\n", caller, (unsigned)syslog_flush_hist[fh_idx].seq);
    kprintf("Recent flushes (most recent last):\n");
    for (uint32_t i = (syslog_flush_hist_count ? (syslog_flush_hist_head - syslog_flush_hist_count) : 0);
         i < syslog_flush_hist_head; i++) {
        uint32_t j = i % FLUSH_HIST_ENTRIES;
        kprintf("  [%u] caller=%p\n", (unsigned)syslog_flush_hist[j].seq, syslog_flush_hist[j].caller);
    }
    vga_set_color(VGA_WHITE, VGA_BLACK);
#endif
    // Ensure /var and /var/log directories exist (best-effort, ignore errors)
    vfs_mkdir("/var");
    vfs_mkdir("/var/log");

    // Open (or create+truncate) the log file
    int fd = vfs_open(KERNEL_LOG_PATH, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd < 0) {
        // Filesystem may be read-only or not mounted — silently skip
        return;
    }

    uint32_t n = syslog_count_val;
    if (n == 0) {
        vfs_close(fd);
        return;
    }

    uint32_t start = (syslog_head - n) % SYSLOG_RING_ENTRIES;

    /* Try to allocate a temporary buffer to snapshot entries to avoid
     * concurrent writers corrupting reads. If allocation fails, fall back
     * to a conservative direct-write approach. */
    size_t entry_sz = sizeof(syslog_entry_t);
    size_t buf_sz = (size_t)n * entry_sz;
    syslog_entry_t *snapshot = kheap_alloc(buf_sz, 0);
    if (snapshot) {
        /* Copy each entry ensuring seq didn't change during the copy. */
        for (uint32_t i = 0; i < n; i++) {
            syslog_entry_t *src = &syslog_ring[(start + i) % SYSLOG_RING_ENTRIES];
            /* 
             * Attempt to obtain a stable snapshot of the entry. Under very
             * heavy concurrent logging the seq field may change repeatedly,
             * so we bound the number of retries to avoid spinning forever.
             */
            uint32_t attempts = 0;
            for (;;) {
                uint32_t before = src->seq;
                memcpy(&snapshot[i], src, entry_sz);
                uint32_t after = src->seq;
                if (before == after) {
                    /* seq stable during copy: accept this snapshot */
                    break;
                }
                if (++attempts >= 1024) {
                    /*
                     * Give up on a fully stable copy after too many retries.
                     * Use the latest best-effort snapshot to guarantee
                     * forward progress instead of potentially hanging.
                     */
                    memcpy(&snapshot[i], src, entry_sz);
                    break;
                }
            }
        }

        /* Now write out the snapshot without holding locks or disabling IRQs. */
        char line[SYSLOG_MSG_MAX + 64];
        for (uint32_t i = 0; i < n; i++) {
            syslog_entry_t *e = &snapshot[i];
            char lvlbuf[16];
            strncpy(lvlbuf, level_str((int)e->level), sizeof(lvlbuf) - 1);
            lvlbuf[sizeof(lvlbuf) - 1] = '\0';

            int len = syslog_snprintf(line, sizeof(line), "[%u] %s [%s] %s\n",
                                      e->seq, lvlbuf, e->tag, e->msg);

            /* Handle partial writes */
            size_t written = 0;
            while (written < (size_t)len) {
                int r = vfs_write(fd, (const uint8_t *)line + written, (size_t)len - written);
                if (r < 0) break; /* give up on error */
                written += (size_t)r;
            }
        }

        kheap_free(snapshot);
    } else {
        /* Allocation failed: fall back to direct writes but be defensive. */
        char line[SYSLOG_MSG_MAX + 64];
        for (uint32_t i = 0; i < n; i++) {
            syslog_entry_t *e = &syslog_ring[(start + i) % SYSLOG_RING_ENTRIES];
            char lvlbuf[16];
            strncpy(lvlbuf, level_str((int)e->level), sizeof(lvlbuf) - 1);
            lvlbuf[sizeof(lvlbuf) - 1] = '\0';

            int len = syslog_snprintf(line, sizeof(line), "[%u] %s [%s] %s\n",
                                      e->seq, lvlbuf, e->tag, e->msg);

            size_t written = 0;
            while (written < (size_t)len) {
                int r = vfs_write(fd, (const uint8_t *)line + written, (size_t)len - written);
                if (r < 0) break;
                written += (size_t)r;
            }
        }
    }

    vfs_close(fd);
    syslog_info("SYSLOG", "Kernel log flushed to " KERNEL_LOG_PATH);
}
