// BlueyOS Syscall Dispatcher - int 0x80 handler
// "Daddy Daughter Day" - Bandit always answers when Bluey calls!
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../include/bluey.h"
#include "../include/version.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "../drivers/vga.h"
#include "tty.h"
#include "idt.h"
#include "syscall.h"
#include "process.h"
#include "multiuser.h"
#include "sysinfo.h"
#include "timer.h"

extern void syscall_stub(void);

void syscall_init(void) {
    // Register int 0x80 as a gate accessible from ring 3 (DPL=3 = 0x60|0x8E = 0xEE)
    idt_set_gate(0x80, (uint32_t)syscall_stub, 0x08, 0xEE);
    kprintf("%s\n", MSG_SYSCALL_INIT);
}

// SYS_WRITE (1): fd=1 -> stdout -> TTY
static int32_t sys_write(uint32_t fd, const char *buf, size_t len) {
    if (fd == 1 || fd == 2) {
        if (!buf) return -1;
        if (len == 0) return 0;
        // Bounds check: refuse oversized writes to avoid flooding
        if (len > 4096) len = 4096;
        tty_write(buf, len);
        tty_flush();
        return (int32_t)len;
    }
    return -1; // other fds not yet implemented
}

// SYS_READ (0): fd=0 -> stdin -> TTY
static int32_t sys_read(uint32_t fd, char *buf, size_t len) {
    if (fd == 0) {
        if (!buf) return -1;
        if (len == 0) return 0;
        return tty_read(buf, len);
    }
    return -1;
}

// uname structure (mirrors Linux utsname)
typedef struct {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
} utsname_t;

static int32_t sys_uname(utsname_t *buf) {
    if (!buf) return -1;
    strncpy(buf->sysname,    "BlueyOS",               64);
    strncpy(buf->nodename,   sysinfo_get_hostname(),  64);
    strncpy(buf->release,    "0.1.0",                 64);
    strncpy(buf->version,    BLUEYOS_VERSION_STRING,  64);
    strncpy(buf->machine,    "i386",                  64);
    strncpy(buf->domainname, sysinfo_get_domainname(), 64);
    return 0;
}

static int32_t sys_gethostname(char *buf, size_t len) {
    if (!buf || len == 0) return -1;
    strncpy(buf, sysinfo_get_hostname(), len - 1);
    buf[len - 1] = '\0';
    return 0;
}

// Main syscall dispatch function - called from syscall.asm
// regs.eax = syscall number, regs.ebx = arg1, regs.ecx = arg2, regs.edx = arg3
int32_t syscall_dispatch(registers_t regs) {
    int32_t ret = -1;
    switch (regs.eax) {
        case SYS_READ:
            ret = sys_read(regs.ebx, (char*)regs.ecx, (size_t)regs.edx);
            break;
        case SYS_WRITE:
            ret = sys_write(regs.ebx, (const char*)regs.ecx, (size_t)regs.edx);
            break;
        case SYS_OPEN:
            // TODO: VFS integration
            ret = -1;
            break;
        case SYS_CLOSE:
            ret = 0;
            break;
        case SYS_EXIT:
            process_exit((int)regs.ebx);
            ret = 0;
            break;
        case SYS_GETPID:
            ret = (int32_t)process_getpid();
            break;
        case SYS_GETUID:
            ret = (int32_t)multiuser_current_uid();
            break;
        case SYS_GETGID:
            ret = (int32_t)multiuser_current_gid();
            break;
        case SYS_UNAME:
            ret = sys_uname((utsname_t*)regs.ebx);
            break;
        case SYS_GETHOSTNAME:
            ret = sys_gethostname((char*)regs.ebx, (size_t)regs.ecx);
            break;
        case SYS_GETTIMEOFDAY:
            // Return ticks as a proxy for time
            if (regs.ebx) *(uint32_t*)regs.ebx = timer_get_ticks();
            ret = 0;
            break;
        default:
            // Unknown syscall - don't crash, just return -1
            kprintf("[SYS]  Unknown syscall %d (Bluey doesn't know that game yet!)\n",
                    regs.eax);
            ret = -1;
            break;
    }
    return ret;
}
