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
#include "rtc.h"
#include <stdarg.h>
#include "kheap.h"

// ---------------------------------------------------------------------------
// Ring buffer state
// ---------------------------------------------------------------------------

// We store entries in a flat array (not a byte ring) for simplicity.
// With SYSLOG_RING_ENTRIES entries this is acceptable for a kernel research OS.
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

static int syslog_snprintf(char *out, size_t sz, const char *fmt, ...);

/* Return the basename portion of a file path (the part after the last '/'). */
static const char *syslog_basename(const char *path) {
    const char *last = path;
    if (!path) return "";
    while (*path) {
        if (*path == '/') last = path + 1;
        path++;
    }
    return last;
}

static int syslog_snprintf(char *out, size_t sz, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = syslog_vsnprintf(out, sz, fmt, ap);
    va_end(ap);
    return r;
}

// ---------------------------------------------------------------------------
// Verbosity level (set from boot args via syslog_set_verbose)
// ---------------------------------------------------------------------------
static int g_verbose_level = VERBOSE_QUIET;

void syslog_set_verbose(int level) {
    if (level < VERBOSE_QUIET) level = VERBOSE_QUIET;
    if (level > VERBOSE_DEBUG) level = VERBOSE_DEBUG;
    g_verbose_level = level;
}

int syslog_get_verbose(void) {
    return g_verbose_level;
}

// Determine the minimum LOG_* level printed to the VGA console based on
// the current verbosity level.
//   verbose=0 (QUIET) : LOG_ERR and more severe (0–3)
//   verbose=1 (INFO)  : LOG_INFO and more severe (0–6)
//   verbose=2 (DEBUG) : everything (0–7)
static int verbose_console_threshold(void) {
    switch (g_verbose_level) {
        case VERBOSE_INFO:  return LOG_INFO;
        case VERBOSE_DEBUG: return LOG_DEBUG;
        default:            return LOG_ERR;
    }
}

static void syslog_write_loc_va(int level, const char *tag, const char *file,
                                const char *func, const char *fmt, va_list ap) {
    if (!syslog_ready) return;

    syslog_entry_t *entry = &syslog_ring[syslog_head % SYSLOG_RING_ENTRIES];

    entry->seq       = syslog_seq++;
    entry->timestamp = rtc_get_uptime_seconds();
    entry->level     = (uint8_t)level;

    // Copy tag (truncate to 15 chars)
    strncpy(entry->tag, tag ? tag : "KERN", sizeof(entry->tag) - 1);
    entry->tag[sizeof(entry->tag) - 1] = '\0';

    // Store source location (basename only to save space)
    if (file && file[0]) {
        const char *base = syslog_basename(file);
        strncpy(entry->src_file, base, sizeof(entry->src_file) - 1);
        entry->src_file[sizeof(entry->src_file) - 1] = '\0';
    } else {
        entry->src_file[0] = '\0';
    }
    if (func && func[0]) {
        strncpy(entry->src_func, func, sizeof(entry->src_func) - 1);
        entry->src_func[sizeof(entry->src_func) - 1] = '\0';
    } else {
        entry->src_func[0] = '\0';
    }

    // Format message
    syslog_vsnprintf(entry->msg, sizeof(entry->msg), fmt, ap);

    syslog_head++;
    if (syslog_count_val < SYSLOG_RING_ENTRIES) syslog_count_val++;

    // Echo to VGA console if the message meets the verbosity threshold.
    if (level <= verbose_console_threshold()) {
        if (level <= LOG_ERR)           vga_set_color(VGA_LIGHT_RED,   VGA_BLACK);
        else if (level == LOG_WARNING)  vga_set_color(VGA_LIGHT_BROWN, VGA_BLACK);
        else if (level == LOG_DEBUG)    vga_set_color(VGA_DARK_GREY,   VGA_BLACK);
        else                            vga_set_color(VGA_WHITE,       VGA_BLACK);
        if (g_verbose_level >= VERBOSE_INFO && entry->src_file[0] != '\0') {
            kprintf_direct("[%4u][%s](%s:%s) %s\n",
                    entry->timestamp, entry->tag,
                    entry->src_file, entry->src_func, entry->msg);
        } else {
            kprintf_direct("[%4u][%s] %s\n", entry->timestamp, entry->tag, entry->msg);
        }
        vga_set_color(VGA_WHITE, VGA_BLACK);
    }
}

void syslog_write(int level, const char *tag, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    syslog_write_loc_va(level, tag, NULL, NULL, fmt, ap);
    va_end(ap);
}

void syslog_write_loc(int level, const char *tag, const char *file,
                      const char *func, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    syslog_write_loc_va(level, tag, file, func, fmt, ap);
    va_end(ap);
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
    kprintf_direct("-- Kernel log (%u entries) --\n", n);
    vga_set_color(VGA_WHITE, VGA_BLACK);

    for (uint32_t i = 0; i < n; i++) {
        syslog_entry_t *e = &syslog_ring[(start + i) % SYSLOG_RING_ENTRIES];
        // Colour-code by severity
        if (e->level <= LOG_ERR)      vga_set_color(VGA_LIGHT_RED,   VGA_BLACK);
        else if (e->level == LOG_WARNING) vga_set_color(VGA_LIGHT_BROWN, VGA_BLACK);
        else if (e->level == LOG_DEBUG)   vga_set_color(VGA_DARK_GREY,   VGA_BLACK);
        else                              vga_set_color(VGA_WHITE,       VGA_BLACK);

        kprintf_direct("[%u] [T+%us] %s [%s] %s\n",
                       e->seq, e->timestamp, level_str((int)e->level), e->tag, e->msg);
        if (g_verbose_level >= VERBOSE_INFO && e->src_file[0] != '\0') {
            kprintf_direct("         src: %s:%s\n", e->src_file, e->src_func);
        }
    }
    vga_set_color(VGA_WHITE, VGA_BLACK);
    kprintf_direct("-- end of kernel log --\n");
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
// kprintf hook — capture all kprintf output into the ring buffer
// ---------------------------------------------------------------------------

// When the hook is active it REPLACES VGA output, so each char must be
// forwarded to the VGA backend (via kprintf_putchar) and also buffered into
// a line buffer.  On newline (or overflow) the accumulated line is committed
// to the ring buffer as a LOG_INFO "KERN" entry without going back through
// kprintf (which would recurse).

#define KPRINTF_HOOK_BUF 256
static char   kprintf_hook_buf[KPRINTF_HOOK_BUF];
static int    kprintf_hook_pos = 0;
static int    kprintf_hook_in  = 0; // recursion guard

static void kprintf_syslog_hook(char c, void *ctx) {
    (void)ctx;

    // Always emit to VGA (hook replaces the normal VGA path)
    kprintf_putchar(c);

    // Guard against re-entrancy (syslog -> kprintf_direct is fine, but any
    // path that called kprintf would recurse back here)
    if (kprintf_hook_in) return;

    // Buffer the character
    if (kprintf_hook_pos < KPRINTF_HOOK_BUF - 1) {
        kprintf_hook_buf[kprintf_hook_pos++] = c;
    }

    // Flush on newline or when the buffer is nearly full
    if (c == '\n' || kprintf_hook_pos >= KPRINTF_HOOK_BUF - 1) {
        kprintf_hook_buf[kprintf_hook_pos] = '\0';
        // Strip trailing newline for cleaner storage
        int end = kprintf_hook_pos - 1;
        while (end >= 0 && (kprintf_hook_buf[end] == '\n' || kprintf_hook_buf[end] == '\r'))
            kprintf_hook_buf[end--] = '\0';

        if (kprintf_hook_buf[0] != '\0') {
            kprintf_hook_in = 1;
            syslog_write(LOG_INFO, "KERN", "%s", kprintf_hook_buf);
            kprintf_hook_in = 0;
        }
        kprintf_hook_pos = 0;
    }
}

void syslog_install_kprintf_hook(void) {
    kprintf_hook_pos = 0;
    kprintf_hook_in  = 0;
    kprintf_set_output_hook(kprintf_syslog_hook, NULL);
}

// ---------------------------------------------------------------------------
// syslog_read_entries — copy ring buffer as text to a userspace buffer
// ---------------------------------------------------------------------------
// Used by the sys_syslog syscall (type=3 READ_ALL).
// Returns the number of bytes written (not including NUL), or -1 on error.

int syslog_read_entries(char *buf, int bufsize) {
    if (!buf || bufsize <= 0) return -1;

    uint32_t flags = 0;
    uint32_t alloc_n = SYSLOG_RING_ENTRIES;
    uint32_t snap_head = 0;
    uint32_t snap_count = 0;
    uint32_t start = 0;
    int pos = 0;
    syslog_entry_t *snapshot;

    snapshot = kheap_alloc((size_t)alloc_n * sizeof(syslog_entry_t), 0);
    if (!snapshot) {
        return -1;
    }

    __asm__ volatile("pushf; cli; pop %0" : "=r"(flags) : : "memory", "cc");
    snap_head = syslog_head;
    snap_count = syslog_count_val;
    if (snap_count > SYSLOG_RING_ENTRIES) snap_count = SYSLOG_RING_ENTRIES;
    if (snap_count > alloc_n) snap_count = alloc_n;
    start = (snap_head + SYSLOG_RING_ENTRIES - snap_count) % SYSLOG_RING_ENTRIES;
    for (uint32_t i = 0; i < snap_count; i++) {
        memcpy(&snapshot[i], &syslog_ring[(start + i) % SYSLOG_RING_ENTRIES],
               sizeof(syslog_entry_t));
    }
    __asm__ volatile("push %0; popf" : : "r"(flags) : "memory", "cc");

    if (snap_count == 0) {
        kheap_free(snapshot);
        return 0;
    }

    for (uint32_t i = 0; i < snap_count && pos < bufsize - 1; i++) {
        const syslog_entry_t *e = &snapshot[i];
        char line[SYSLOG_MSG_MAX + SYSLOG_FMT_OVERHEAD];
        int len;
        if (e->src_file[0] != '\0') {
            len = syslog_snprintf(line, sizeof(line),
                                  "[%u] [T+%us] %s [%s](%s:%s) %s\n",
                                  e->seq, e->timestamp,
                                  level_str((int)e->level), e->tag,
                                  e->src_file, e->src_func, e->msg);
        } else {
            len = syslog_snprintf(line, sizeof(line),
                                  "[%u] [T+%us] %s [%s] %s\n",
                                  e->seq, e->timestamp,
                                  level_str((int)e->level), e->tag, e->msg);
        }
        int copy = len;
        if (pos + copy > bufsize - 1) copy = bufsize - 1 - pos;
        memcpy(buf + pos, line, (size_t)copy);
        pos += copy;
    }

    buf[pos] = '\0';
    kheap_free(snapshot);
    return pos;
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
    // Ensure /var, /var/log, /var/run, and /tmp directories exist (best-effort, ignore errors)
    vfs_mkdir("/var");
    vfs_mkdir("/var/log");
    vfs_mkdir("/var/run");
    vfs_mkdir("/tmp");

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
        char line[SYSLOG_MSG_MAX + SYSLOG_FMT_OVERHEAD];
        for (uint32_t i = 0; i < n; i++) {
            syslog_entry_t *e = &snapshot[i];
            char lvlbuf[16];
            strncpy(lvlbuf, level_str((int)e->level), sizeof(lvlbuf) - 1);
            lvlbuf[sizeof(lvlbuf) - 1] = '\0';

            int len;
            if (e->src_file[0] != '\0') {
                len = syslog_snprintf(line, sizeof(line), "[%u] [T+%us] %s [%s](%s:%s) %s\n",
                                      e->seq, e->timestamp, lvlbuf, e->tag,
                                      e->src_file, e->src_func, e->msg);
            } else {
                len = syslog_snprintf(line, sizeof(line), "[%u] [T+%us] %s [%s] %s\n",
                                      e->seq, e->timestamp, lvlbuf, e->tag, e->msg);
            }
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
        char line[SYSLOG_MSG_MAX + SYSLOG_FMT_OVERHEAD];
        for (uint32_t i = 0; i < n; i++) {
            syslog_entry_t *e = &syslog_ring[(start + i) % SYSLOG_RING_ENTRIES];
            char lvlbuf[16];
            strncpy(lvlbuf, level_str((int)e->level), sizeof(lvlbuf) - 1);
            lvlbuf[sizeof(lvlbuf) - 1] = '\0';

            int len;
            if (e->src_file[0] != '\0') {
                len = syslog_snprintf(line, sizeof(line), "[%u] [T+%us] %s [%s](%s:%s) %s\n",
                                      e->seq, e->timestamp, lvlbuf, e->tag,
                                      e->src_file, e->src_func, e->msg);
            } else {
                len = syslog_snprintf(line, sizeof(line), "[%u] [T+%us] %s [%s] %s\n",
                                      e->seq, e->timestamp, lvlbuf, e->tag, e->msg);
            }

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
