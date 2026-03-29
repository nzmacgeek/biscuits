#pragma once
// BlueyOS Kernel Syslog — "Bandit's Homework Diary"
// "Dad! You're supposed to write it down!" — Bluey Heeler
// Episode ref: "Dad Baby" — Bandit's notes become legendary
//
// Ring-buffer syslog with severity levels (POSIX-compatible naming).
// Messages are held in RAM and optionally flushed to /var/log/kernel.log
// on the mounted filesystem (BiscuitFS or FAT16).
//
// ⚠️  VIBE CODED RESEARCH PROJECT — NOT FOR PRODUCTION USE ⚠️
//
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project
// with no affiliation to Ludo Studio or the BBC.
#include "../include/types.h"

// ---------------------------------------------------------------------------
// Severity levels — same numeric values as POSIX syslog(3)
// ---------------------------------------------------------------------------
#define LOG_EMERG    0   /* System is unusable (kernel panic imminent)  */
#define LOG_ALERT    1   /* Action must be taken immediately             */
#define LOG_CRIT     2   /* Critical conditions                          */
#define LOG_ERR      3   /* Error conditions                             */
#define LOG_WARNING  4   /* Warning conditions                           */
#define LOG_NOTICE   5   /* Normal but significant condition             */
#define LOG_INFO     6   /* Informational message                        */
#define LOG_DEBUG    7   /* Debug-level message                          */

// ---------------------------------------------------------------------------
// Entry sizing
// ---------------------------------------------------------------------------
// Maximum length of the message payload stored in each ring entry.
// The total number of entries in the ring is managed internally by syslog.c.
#define SYSLOG_MSG_MAX      256   /* maximum length of a single log line       */

// ---------------------------------------------------------------------------
// Log entry (stored in the ring buffer as a packed record)
// ---------------------------------------------------------------------------
typedef struct {
    uint32_t seq;       /* monotonically increasing sequence number */
    uint8_t  level;     /* LOG_* severity */
    char     tag[16];   /* subsystem tag, e.g. "KERN", "NET", "FS" */
    char     msg[SYSLOG_MSG_MAX];
} syslog_entry_t;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Initialise the ring buffer (called very early — before heap is ready).
void syslog_init(void);

// Write a log entry.  level = LOG_*  tag = short subsystem name.
void syslog_write(int level, const char *tag, const char *fmt, ...);

// Convenience macros — mirrors the Linux pr_* family.
#define syslog_emerg(tag, ...)   syslog_write(LOG_EMERG,   tag, __VA_ARGS__)
#define syslog_alert(tag, ...)   syslog_write(LOG_ALERT,   tag, __VA_ARGS__)
#define syslog_crit(tag, ...)    syslog_write(LOG_CRIT,    tag, __VA_ARGS__)
#define syslog_err(tag, ...)     syslog_write(LOG_ERR,     tag, __VA_ARGS__)
#define syslog_warn(tag, ...)    syslog_write(LOG_WARNING, tag, __VA_ARGS__)
#define syslog_notice(tag, ...)  syslog_write(LOG_NOTICE,  tag, __VA_ARGS__)
#define syslog_info(tag, ...)    syslog_write(LOG_INFO,    tag, __VA_ARGS__)
#define syslog_debug(tag, ...)   syslog_write(LOG_DEBUG,   tag, __VA_ARGS__)

// Flush the in-RAM ring buffer to /var/log/kernel.log on the mounted VFS.
// Call this after vfs_mount() succeeds.  Safe to call multiple times.
void syslog_flush_to_fs(void);

// Print all buffered log entries to the VGA console (implements dmesg).
void syslog_dmesg(void);

// Return the number of entries currently in the ring buffer.
uint32_t syslog_count(void);
