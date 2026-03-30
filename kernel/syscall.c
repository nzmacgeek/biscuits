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
#include "elf.h"
#include "syscall.h"
#include "process.h"
#include "scheduler.h"
#include "multiuser.h"
#include "signal.h"
#include "sysinfo.h"
#include "timer.h"
#include "kheap.h"
#include "paging.h"
#include "gdt.h"
#include "../fs/vfs.h"

extern void syscall_stub(void);

#define BLUEY_ENOSYS 38
#define BLUEY_EPERM   1
#define BLUEY_EFAULT 14
#define BLUEY_EINVAL 22
#define BLUEY_E2BIG   7
#define BLUEY_EAGAIN 11

#define BLUEY_ENOMEM 12

#define EXEC_COPY_MAX_STRINGS    64u
#define EXEC_COPY_MAX_STRING_LEN 256u

#define PAGE_ALIGN_UP(value) (((value) + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u))

#define BLUEY_PROT_READ   0x1
#define BLUEY_PROT_WRITE  0x2
#define BLUEY_MAP_PRIVATE 0x02
#define BLUEY_MAP_FIXED   0x10
#define BLUEY_MAP_ANON    0x20

static int syscall_is_kernel_mode(const registers_t *regs) {
    return regs && ((regs->cs & 0x3u) == 0);
}

static int syscall_map_user_pages(process_t *process,
                                  uint32_t start,
                                  uint32_t end,
                                  uint32_t flags) {
    uint32_t old_page_dir;

    if (!process || !process->page_dir) return -BLUEY_EINVAL;

    old_page_dir = paging_current_directory();
    paging_switch_directory(process->page_dir);

    for (uint32_t addr = start; addr < end; addr += PAGE_SIZE) {
        uint32_t phys;
        if (paging_virt_to_phys(addr)) continue;

        phys = pmm_alloc_frame();
        if (!phys) {
            paging_switch_directory(old_page_dir);
            return -BLUEY_ENOMEM;
        }

        paging_map_in_directory(process->page_dir, addr, phys, flags);
        memset((void*)addr, 0, PAGE_SIZE);
    }

    paging_switch_directory(old_page_dir);
    return 0;
}

static size_t syscall_copy_strlen(const char *src, size_t limit) {
    size_t len = 0;

    if (!src) return 0;
    while (len < limit && src[len]) len++;
    return len;
}

static char *syscall_copy_string(const char *src) {
    size_t len;
    char *copy;

    if (!src) return NULL;

    len = syscall_copy_strlen(src, EXEC_COPY_MAX_STRING_LEN);
    if (len >= EXEC_COPY_MAX_STRING_LEN) return NULL;

    copy = (char*)kheap_alloc(len + 1u, 0);
    if (!copy) return NULL;

    memcpy(copy, src, len);
    copy[len] = '\0';
    return copy;
}

static void syscall_free_string_vector(char **vec) {
    size_t index;

    if (!vec) return;
    for (index = 0; vec[index]; index++) kheap_free(vec[index]);
    kheap_free(vec);
}

static char **syscall_copy_string_vector(const char *const *user_vec, const char *fallback0) {
    size_t count = 0;
    char **vec;

    if (user_vec) {
        while (count < EXEC_COPY_MAX_STRINGS && user_vec[count]) count++;
        if (count == EXEC_COPY_MAX_STRINGS && user_vec[count]) return NULL;
    }

    if (count == 0 && fallback0) count = 1;

    vec = (char**)kheap_alloc(sizeof(char*) * (count + 1u), 0);
    if (!vec) return NULL;
    memset(vec, 0, sizeof(char*) * (count + 1u));

    if (user_vec) {
        for (size_t index = 0; index < count; index++) {
            vec[index] = syscall_copy_string(user_vec[index]);
            if (!vec[index]) {
                syscall_free_string_vector(vec);
                return NULL;
            }
        }
    } else if (fallback0) {
        vec[0] = syscall_copy_string(fallback0);
        if (!vec[0]) {
            syscall_free_string_vector(vec);
            return NULL;
        }
    }

    return vec;
}

static void syscall_prepare_user_return(registers_t *regs, process_t *process) {
    if (!regs || !process) return;

    regs->ds = GDT_USER_DATA;
    regs->eip = process->eip;
    regs->cs = GDT_USER_CODE;
    regs->eflags |= 0x200u;
    regs->useresp = process->esp;
    regs->ss = GDT_USER_DATA;
}

static int32_t sys_fork(const registers_t *regs) {
    process_t *child;

    if (!regs || !process_current()) return -BLUEY_EPERM;
    if (syscall_is_kernel_mode(regs)) return -BLUEY_EPERM;

    child = process_fork_current(regs);
    if (!child) return -1;

    scheduler_add(child);
    kprintf("[SYS]  Forked pid=%u from pid=%u\n", child->pid, process_current()->pid);
    return (int32_t)child->pid;
}

static int32_t sys_brk(uint32_t addr) {
    process_t *process = process_current();
    uint32_t old_break;
    uint32_t map_start;
    uint32_t map_end;
    int result;

    if (!process || !(process->flags & PROC_FLAG_USER_MODE)) return -BLUEY_EPERM;
    if (addr == 0) return (int32_t)process->brk_current;
    if (addr < process->brk_base) return (int32_t)process->brk_current;

    old_break = process->brk_current;
    map_start = PAGE_ALIGN_UP(old_break);
    map_end = PAGE_ALIGN_UP(addr);

    if (map_end > map_start) {
        result = syscall_map_user_pages(process, map_start, map_end,
                                        PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE);
        if (result != 0) return (int32_t)process->brk_current;
    }

    process->brk_current = addr;
    return (int32_t)process->brk_current;
}

static int32_t sys_mmap(registers_t *regs) {
    process_t *process = process_current();
    uint32_t addr = regs->ebx;
    uint32_t len = regs->ecx;
    uint32_t prot = regs->edx;
    uint32_t map_flags = regs->esi;
    int fd = (int)regs->edi;
    uint32_t map_addr;
    uint32_t aligned_len;
    uint32_t page_flags = PAGE_PRESENT | PAGE_USER;
    int result;

    if (!process || !(process->flags & PROC_FLAG_USER_MODE)) return -BLUEY_EPERM;
    if (len == 0) return -BLUEY_EINVAL;
    if (!(map_flags & BLUEY_MAP_PRIVATE)) return -BLUEY_EINVAL;
    if (!(map_flags & BLUEY_MAP_ANON) || fd != -1) return -BLUEY_EINVAL;

    aligned_len = PAGE_ALIGN_UP(len);
    if (prot & BLUEY_PROT_WRITE) page_flags |= PAGE_WRITABLE;

    if ((map_flags & BLUEY_MAP_FIXED) && addr) {
        map_addr = addr & ~(PAGE_SIZE - 1u);
    } else {
        map_addr = PAGE_ALIGN_UP(process->mmap_base);
        process->mmap_base = map_addr + aligned_len + PAGE_SIZE;
    }

    result = syscall_map_user_pages(process, map_addr, map_addr + aligned_len, page_flags);
    if (result != 0) return result;
    return (int32_t)map_addr;
}

static int32_t sys_rt_sigaction(int sig, const bluey_sigaction_t *act, bluey_sigaction_t *oldact) {
    process_t *process = process_current();
    if (!process) return -BLUEY_EPERM;
    return signal_sigaction(process, sig, act, oldact);
}

static int32_t sys_rt_sigprocmask(int how, const uint32_t *set, uint32_t *oldset) {
    process_t *process = process_current();
    if (!process) return -BLUEY_EPERM;
    return signal_sigprocmask(process, how, set, oldset);
}

static int32_t sys_sigreturn(registers_t *regs, void *frame_ptr) {
    process_t *process = process_current();
    if (!process || !regs) return -BLUEY_EPERM;
    return signal_sigreturn(process, regs, frame_ptr);
}

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

static int32_t sys_open(const char *path, int flags) {
    if (!path) return -1;
    return vfs_open(path, flags);
}

static int32_t sys_close(int fd) {
    return vfs_close(fd);
}

static int32_t sys_kill(uint32_t pid, int sig) {
    return signal_send_pid(pid, sig);
}

static int32_t sys_execve(registers_t *regs,
                          const char *path,
                          const char *const *argv,
                          const char *const *envp) {
    process_t *process = process_current();
    elf_image_t image;
    char *path_copy = NULL;
    char **argv_copy = NULL;
    char **envp_copy = NULL;
    uint32_t old_page_dir;
    int32_t result = -1;

    if (!process) return -BLUEY_EPERM;
    if (!path) return -BLUEY_EFAULT;
    if (syscall_is_kernel_mode(regs)) return -BLUEY_EPERM;

    path_copy = syscall_copy_string(path);
    if (!path_copy || !path_copy[0]) {
        result = path_copy ? -BLUEY_EINVAL : -BLUEY_E2BIG;
        goto cleanup;
    }

    argv_copy = syscall_copy_string_vector(argv, path_copy);
    envp_copy = syscall_copy_string_vector(envp, NULL);
    if (!argv_copy || (envp && !envp_copy)) {
        result = -BLUEY_E2BIG;
        goto cleanup;
    }

    if (elf_load_image(path_copy, (const char *const *)argv_copy,
                       (const char *const *)envp_copy, &image) != 0) {
        result = -1;
        goto cleanup;
    }

    old_page_dir = process->page_dir;
    process_exec_replace(process, image.name, image.entry, image.stack_pointer,
                         image.stack_base, image.stack_top, image.page_dir);
    process_set_memory_layout(process, image.image_end);
    paging_switch_directory(image.page_dir);
    process_set_current(process);
    syscall_prepare_user_return(regs, process);
    result = 0;

    if (old_page_dir && old_page_dir != image.page_dir) {
        paging_destroy_address_space(old_page_dir);
    }

cleanup:
    if (path_copy) kheap_free(path_copy);
    if (argv_copy) syscall_free_string_vector(argv_copy);
    if (envp_copy) syscall_free_string_vector(envp_copy);
    return result;
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
int32_t syscall_dispatch(registers_t *regs) {
    int32_t ret = -1;
    if (!regs) return -1;

    switch (regs->eax) {
        case SYS_READ:
            ret = sys_read(regs->ebx, (char*)regs->ecx, (size_t)regs->edx);
            break;
        case SYS_WRITE:
            ret = sys_write(regs->ebx, (const char*)regs->ecx, (size_t)regs->edx);
            break;
        case SYS_OPEN:
            ret = sys_open((const char*)regs->ebx, (int)regs->ecx);
            break;
        case SYS_CLOSE:
            ret = sys_close((int)regs->ebx);
            break;
        case SYS_KILL:
            ret = sys_kill(regs->ebx, (int)regs->ecx);
            break;
        case SYS_EXIT:
            process_exit((int)regs->ebx);
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
            ret = sys_uname((utsname_t*)regs->ebx);
            break;
        case SYS_GETHOSTNAME:
            ret = sys_gethostname((char*)regs->ebx, (size_t)regs->ecx);
            break;
        case SYS_GETTIMEOFDAY:
            // Return ticks as a proxy for time
            if (regs->ebx) *(uint32_t*)regs->ebx = timer_get_ticks();
            ret = 0;
            break;
        case SYS_EXECVE:
            ret = sys_execve(regs,
                             (const char*)regs->ebx,
                             (const char *const*)regs->ecx,
                             (const char *const*)regs->edx);
            break;
        case SYS_WAITPID:
            ret = process_waitpid((int32_t)regs->ebx, (int*)regs->ecx, (int)regs->edx);
            break;
        case SYS_FORK:
        case SYS_CLONE:
            ret = sys_fork(regs);
            break;
        case SYS_BRK:
            ret = sys_brk(regs->ebx);
            break;
        case SYS_MMAP:
        case SYS_MMAP2:
            ret = sys_mmap(regs);
            break;
        case SYS_RT_SIGACTION:
            ret = sys_rt_sigaction((int)regs->ebx,
                                   (const bluey_sigaction_t*)regs->ecx,
                                   (bluey_sigaction_t*)regs->edx);
            break;
        case SYS_RT_SIGPROCMASK:
            ret = sys_rt_sigprocmask((int)regs->ebx,
                                     (const uint32_t*)regs->ecx,
                                     (uint32_t*)regs->edx);
            break;
        case SYS_SIGRETURN:
            ret = sys_sigreturn(regs, (void*)regs->ebx);
            break;
        case SYS_IOCTL:
        case SYS_CHDIR:
        case SYS_GETCWD:
        case SYS_PIPE:
        case SYS_DUP2:
        case SYS_STAT:
        case SYS_GETDENTS:
            ret = -BLUEY_ENOSYS;
            break;
        default:
            // Unknown syscall - don't crash, just return -1
            kprintf("[SYS]  Unknown syscall %d (Bluey doesn't know that game yet!)\n",
                    regs->eax);
            ret = -BLUEY_ENOSYS;
            break;
    }
    regs->eax = ret;
    scheduler_handle_trap(regs, 0);
    return ret;
}
