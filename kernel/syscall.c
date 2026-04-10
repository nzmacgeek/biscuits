// BlueyOS Syscall Dispatcher - int 0x80 handler
// "Daddy Daughter Day" - Bandit always answers when Bluey calls!
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../include/bluey.h"
#include "../include/version.h"
#include "../include/ports.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "../drivers/keyboard.h"
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
#include "rtc.h"
#include "kheap.h"
#include "paging.h"
#include "gdt.h"
#include "poll.h"
#include "devev.h"
#include "netctl.h"
#include "socket.h"
#include "module.h"
#include "../net/tcpip.h"
#include "../fs/vfs.h"

extern void syscall_stub(void);
extern void syscall_enter_user_frame(const registers_t *regs);

/* Per-entry saved user segment registers. Interrupts are disabled (cli) when
 * these are written and are only accessed on the single boot CPU, so no
 * locking is required. If SMP support is added, these must become per-CPU. */
uint32_t syscall_saved_es = 0;
uint32_t syscall_saved_fs = 0;
uint32_t syscall_saved_gs = 0;

#define BLUEY_ENOSYS 38
#define BLUEY_EPERM   1
#define BLUEY_ENOENT  2
#define BLUEY_ESRCH   3
#define BLUEY_EFAULT 14
#define BLUEY_EINVAL 22
#define BLUEY_E2BIG   7
#define BLUEY_EAGAIN 11
#define BLUEY_ENOTDIR 20
#define BLUEY_EBADF   9
#define BLUEY_EISDIR 21
#define BLUEY_ERANGE 34
#define BLUEY_EPIPE  32
#define BLUEY_EIO     5
#define BLUEY_EMFILE 24
#define BLUEY_ENOTSOCK 88
#define BLUEY_EADDRINUSE 98
#define BLUEY_EAFNOSUPPORT 97
#define BLUEY_EPROTONOSUPPORT 93
#define BLUEY_EOPNOTSUPP 95
#define BLUEY_ECONNREFUSED 111

#define BLUEY_ENOMEM 12

#define EXEC_COPY_MAX_STRINGS    64u
#define EXEC_COPY_MAX_STRING_LEN 256u

#define PAGE_ALIGN_UP(value) (((value) + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u))

#define BLUEY_PROT_READ   0x1
#define BLUEY_PROT_WRITE  0x2
#define BLUEY_PROT_EXEC   0x4
#define BLUEY_MAP_PRIVATE 0x02
#define BLUEY_MAP_SHARED  0x01
#define BLUEY_MAP_FIXED   0x10
#define BLUEY_MAP_ANON    0x20

#define BLUEY_CLONE_VM          0x00000100u
#define BLUEY_CLONE_VFORK       0x00004000u
#define BLUEY_CLONE_EXIT_SIGNAL 0x000000ffu

static int syscall_is_kernel_mode(const registers_t *regs) {
    return regs && ((regs->cs & 0x3u) == 0);
}

static int32_t sys_write(uint32_t fd, const char *buf, size_t len);

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
    process_t *current;
    int32_t fork_error = -BLUEY_EAGAIN;

    if (!regs) {
        kprintf("[SYS] fork denied: null regs\n");
        return -BLUEY_EPERM;
    }
    current = process_current();
    if (!current) {
        kprintf("[SYS] fork denied: no current process\n");
        return -BLUEY_EPERM;
    }
    if (!(current->flags & PROC_FLAG_USER_MODE)) {
        kprintf("[SYS] fork denied: current process is not user mode (pid=%u flags=0x%x cs=0x%x)\n",
                current->pid, current->flags, regs->cs);
        return -BLUEY_EPERM;
    }
    if (syscall_is_kernel_mode(regs)) {
        kprintf("[SYS] fork warning: user-mode process pid=%u entered with kernel cs=0x%x; allowing based on process flags\n",
                current->pid, regs->cs);
    }

    child = process_fork_current(regs, &fork_error);
    if (!child) {
        if (fork_error == 0) fork_error = -BLUEY_EAGAIN;
        kprintf("[SYS] fork failed for pid=%u: returning %d\n", current->pid, -fork_error);
        return fork_error;
    }

    scheduler_add(child);
    kprintf("[SYS]  Forked pid=%u from pid=%u\n", child->pid, current->pid);
    return (int32_t)child->pid;
}

static int32_t sys_vfork(const registers_t *regs) {
    process_t *child;
    process_t *current;
    int32_t fork_error = -BLUEY_EAGAIN;

    if (!regs) {
        kprintf("[SYS] vfork denied: null regs\n");
        return -BLUEY_EPERM;
    }
    current = process_current();
    if (!current) {
        kprintf("[SYS] vfork denied: no current process\n");
        return -BLUEY_EPERM;
    }
    if (!(current->flags & PROC_FLAG_USER_MODE)) {
        kprintf("[SYS] vfork denied: current process is not user mode (pid=%u flags=0x%x cs=0x%x)\n",
                current->pid, current->flags, regs->cs);
        return -BLUEY_EPERM;
    }
    if (syscall_is_kernel_mode(regs)) {
        kprintf("[SYS] vfork warning: user-mode process pid=%u entered with kernel cs=0x%x; allowing based on process flags\n",
                current->pid, regs->cs);
    }

    child = process_vfork_current(regs, &fork_error);
    if (!child) {
        if (fork_error == 0) fork_error = -BLUEY_EAGAIN;
        kprintf("[SYS] vfork failed for pid=%u: returning %d\n", current->pid, -fork_error);
        return fork_error;
    }

    scheduler_add(child);
    current->saved_regs = *regs;
    current->saved_regs.eax = (uint32_t)child->pid;
    current->eip = regs->eip;
    current->esp = regs->useresp;
    child->state = PROC_RUNNING;
    scheduler_set_current(child);
    process_set_current(child);
    paging_switch_directory(child->page_dir);
    gdt_set_tls_base(child->tls_base);
    kprintf("[SYS]  vforked pid=%u from pid=%u\n", child->pid, current->pid);
    syscall_enter_user_frame(&child->saved_regs);
    return (int32_t)child->pid;
}

static int32_t sys_clone(const registers_t *regs) {
    uint32_t flags;
    uint32_t exit_signal;

    if (!regs) return -BLUEY_EPERM;

    flags = regs->ebx;
    exit_signal = flags & BLUEY_CLONE_EXIT_SIGNAL;

    if ((flags & ~(BLUEY_CLONE_VM | BLUEY_CLONE_VFORK | BLUEY_CLONE_EXIT_SIGNAL)) != 0u) {
        kprintf("[SYS] clone unsupported flags=0x%x\n", flags);
        return -BLUEY_ENOSYS;
    }

    if (exit_signal != SIGCHLD && exit_signal != 0u) {
        kprintf("[SYS] clone unsupported exit signal=%u flags=0x%x\n", exit_signal, flags);
        return -BLUEY_ENOSYS;
    }

    if ((flags & (BLUEY_CLONE_VM | BLUEY_CLONE_VFORK)) == (BLUEY_CLONE_VM | BLUEY_CLONE_VFORK)) {
        return sys_vfork(regs);
    }

    if ((flags & (BLUEY_CLONE_VM | BLUEY_CLONE_VFORK)) == 0u) {
        return sys_fork(regs);
    }

    kprintf("[SYS] clone unsupported flags=0x%x stack=0x%x\n", flags, regs->ecx);
    return -BLUEY_ENOSYS;
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
        if (result != 0) {
            kprintf("[SYS] brk map failed pid=%d name=%s map_start=0x%x map_end=0x%x result=%d\n",
                    process->pid, process->name, map_start, map_end, result);
            return (int32_t)process->brk_current;
        }
    }

    process->brk_current = addr;
    return (int32_t)process->brk_current;
}

/* getrlimit/setrlimit: use struct rlimit in user memory
 * sys_setrlimit: regs->ebx = resource, regs->ecx = user pointer to rlimit
 * sys_getrlimit: regs->ebx = resource, regs->ecx = user pointer to rlimit
 */
#include "../include/rlimit.h"

static int32_t sys_setrlimit(uint32_t resource, uint32_t user_rlim_ptr) {
    process_t *p = process_current();
    rlimit_t rlim;
    uint32_t old_dir;

    if (!p) return -BLUEY_EPERM;
    if (resource != RLIMIT_STACK) return -BLUEY_EINVAL;
    if (!user_rlim_ptr) return -BLUEY_EFAULT;

    /* Copy from user memory */
    old_dir = paging_current_directory();
    paging_switch_directory(p->page_dir);
    rlimit_t *u = (rlimit_t*)(uintptr_t)user_rlim_ptr;
    rlim.rlim_cur = u->rlim_cur;
    rlim.rlim_max = u->rlim_max;
    paging_switch_directory(old_dir);

    /* Basic validation */
    if (rlim.rlim_cur > rlim.rlim_max) return -BLUEY_EINVAL;

    p->rlimit_stack_cur = rlim.rlim_cur;
    p->rlimit_stack_max = rlim.rlim_max;
    return 0;
}

static int32_t sys_getrlimit(uint32_t resource, uint32_t user_rlim_ptr) {
    process_t *p = process_current();
    uint32_t old_dir;

    if (!p) return -BLUEY_EPERM;
    if (resource != RLIMIT_STACK) return -BLUEY_EINVAL;
    if (!user_rlim_ptr) return -BLUEY_EFAULT;

    old_dir = paging_current_directory();
    paging_switch_directory(p->page_dir);
    rlimit_t *u = (rlimit_t*)(uintptr_t)user_rlim_ptr;
    u->rlim_cur = p->rlimit_stack_cur;
    u->rlim_max = p->rlimit_stack_max;
    paging_switch_directory(old_dir);
    return 0;
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
    if (!(map_flags & (BLUEY_MAP_PRIVATE | BLUEY_MAP_SHARED))) return -BLUEY_EINVAL;

    aligned_len = PAGE_ALIGN_UP(len);
    if (prot & BLUEY_PROT_WRITE) page_flags |= PAGE_WRITABLE;

    /* If mapping requests executable pages, be conservative and remove
     * writable permission from the page flags unless the caller explicitly
     * requested writable + executable (rare). */
    if (prot & BLUEY_PROT_EXEC) page_flags &= ~PAGE_WRITABLE;

    if ((map_flags & BLUEY_MAP_FIXED) && addr) {
        map_addr = addr & ~(PAGE_SIZE - 1u);
    } else {
        map_addr = PAGE_ALIGN_UP(process->mmap_base);
        process->mmap_base = map_addr + aligned_len + PAGE_SIZE;
    }

    if (map_flags & BLUEY_MAP_ANON) {
        if (fd != -1) return -BLUEY_EINVAL;
        result = syscall_map_user_pages(process, map_addr, map_addr + aligned_len, page_flags);
        if (result != 0) return result;
        return (int32_t)map_addr;
    }

    /* File-backed mapping (minimal): read file contents into newly allocated
     * user pages. We allocate pages, then copy from kernel buffer using vfs_read.
     * Supports MAP_PRIVATE semantics only (MAP_SHARED treated as private).
     */
    if (fd < 0) return -BLUEY_EINVAL;

    result = syscall_map_user_pages(process, map_addr, map_addr + aligned_len, page_flags);
    if (result != 0) return result;

    /* Adjust writeable flag for executable mappings: executable mappings
     * should not be writable on many platforms. Respect PROT_EXEC by
     * clearing the writable bit if set unless MAP_SHARED requests it.
     */
    if (prot & BLUEY_PROT_WRITE) {
        if (prot & 0x4) {
            /* placeholder: if additional exec flag present - keep writable */
        }
    }

    /* If mapping requests executable pages, be conservative and remove
     * writable permission from the page flags unless MAP_SHARED explicitly
     * requests shared writable mapping. */
    if ((prot & 0x100) /* proxy for PROT_EXEC not defined separately */) {
        page_flags &= ~PAGE_WRITABLE;
    }

    uint8_t *kbuf = (uint8_t*)kheap_alloc(PAGE_SIZE, 0);
    if (!kbuf) return -BLUEY_ENOMEM;

    uint32_t old_dir = paging_current_directory();
    uint32_t remaining = len;
    uint32_t off = 0;
    while (remaining) {
        uint32_t toread = remaining > PAGE_SIZE ? PAGE_SIZE : remaining;
        int r = vfs_read_at(fd, kbuf, toread, off);
        if (r <= 0) {
            /* Short read or error: leave rest zeroed */
            break;
        }

        /* Copy into user mapping */
        paging_switch_directory(process->page_dir);
        memcpy((void*)(map_addr + off), kbuf, (size_t)r);
        paging_switch_directory(old_dir);

        remaining -= (uint32_t)r;
        off += (uint32_t)r;
        if ((uint32_t)r < toread) break; /* EOF */
    }

    kheap_free(kbuf);
    return (int32_t)map_addr;
}

static int32_t sys_munmap(registers_t *regs) {
    process_t *process = process_current();
    uint32_t addr = regs->ebx;
    uint32_t len = regs->ecx;
    uint32_t end;

    if (!process || !(process->flags & PROC_FLAG_USER_MODE)) return -BLUEY_EPERM;
    if (len == 0) return -BLUEY_EINVAL;

    end = (addr + len + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);
    for (uint32_t a = addr & ~(PAGE_SIZE - 1u); a < end; a += PAGE_SIZE) {
        paging_unmap_in_directory(process->page_dir, a);
    }
    return 0;
}

static int32_t sys_mprotect(registers_t *regs) {
    process_t *process = process_current();
    uint32_t addr = regs->ebx;
    uint32_t len = regs->ecx;
    uint32_t prot = regs->edx;
    uint32_t end;
    uint32_t page_flags;

    if (!process || !(process->flags & PROC_FLAG_USER_MODE)) return -BLUEY_EPERM;
    if (len == 0) return -BLUEY_EINVAL;

    page_flags = PAGE_PRESENT | PAGE_USER;
    if (prot & BLUEY_PROT_WRITE) page_flags |= PAGE_WRITABLE;

    end = (addr + len + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);
    for (uint32_t a = addr & ~(PAGE_SIZE - 1u); a < end; a += PAGE_SIZE) {
        paging_set_page_flags_in_directory(process->page_dir, a, page_flags);
    }
    return 0;
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

// Minimal kernel-side representation of timespec for userland
typedef struct {
    uint32_t tv_sec;
    uint32_t tv_nsec;
} k_timespec_t;

typedef struct {
    int32_t tv_sec;
    int32_t tv_nsec;
} compat_timespec32_t;

typedef struct {
    uint64_t tv_sec;
    uint64_t tv_nsec;
} k_timespec64_t;

typedef struct {
    uint64_t rlim_cur;
    uint64_t rlim_max;
} compat_rlimit64_t;

typedef struct {
    uint32_t sigmask;
    uint32_t sigsetsize;
} compat_pselect6_data_t;

typedef struct {
    uint32_t iov_base;
    uint32_t iov_len;
} k_iovec_t;

typedef struct {
    uint64_t st_dev;
    uint32_t __st_dev_padding;
    uint32_t __st_ino_truncated;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    uint32_t __st_rdev_padding;
    uint32_t __pad0;
    uint64_t st_size;
    uint32_t st_blksize;
    uint32_t __pad1;
    uint64_t st_blocks;
    int32_t st_atime_sec;
    int32_t st_atime_nsec;
    int32_t st_mtime_sec;
    int32_t st_mtime_nsec;
    int32_t st_ctime_sec;
    int32_t st_ctime_nsec;
    uint64_t st_ino;
} k_stat64_t;

typedef struct {
    uint32_t stx_mask;
    uint32_t stx_blksize;
    uint64_t stx_attributes;
    uint32_t stx_nlink;
    uint32_t stx_uid;
    uint32_t stx_gid;
    uint16_t stx_mode;
    uint16_t __pad0;
    uint64_t stx_ino;
    uint64_t stx_size;
    uint64_t stx_blocks;
    uint64_t stx_attributes_mask;
    struct {
        int64_t tv_sec;
        uint32_t tv_nsec;
        int32_t __pad;
    } stx_atime, stx_btime, stx_ctime, stx_mtime;
    uint32_t stx_rdev_major;
    uint32_t stx_rdev_minor;
    uint32_t stx_dev_major;
    uint32_t stx_dev_minor;
    uint64_t stx_mnt_id;
    uint32_t stx_dio_mem_align;
    uint32_t stx_dio_offset_align;
    uint64_t stx_subvol;
    uint32_t stx_atomic_write_unit_min;
    uint32_t stx_atomic_write_unit_max;
    uint32_t stx_atomic_write_segments_max;
    uint32_t __pad1;
    uint64_t __pad2[9];
} k_statx_t;

#define SYS_IOV_MAX 1024u
#define STATX_BASIC_STATS 0x7ffu
#define AT_FDCWD ((int32_t)-100)
#define AT_EMPTY_PATH 0x1000
#define AT_SYMLINK_NOFOLLOW 0x100

#define BLUEY_CLOCK_REALTIME           0
#define BLUEY_CLOCK_MONOTONIC          1
#define BLUEY_CLOCK_PROCESS_CPUTIME_ID 2
#define BLUEY_CLOCK_THREAD_CPUTIME_ID  3
#define BLUEY_CLOCK_MONOTONIC_RAW      4
#define BLUEY_CLOCK_REALTIME_COARSE    5
#define BLUEY_CLOCK_MONOTONIC_COARSE   6
#define BLUEY_CLOCK_BOOTTIME           7
#define BLUEY_CLOCK_REALTIME_ALARM     8
#define BLUEY_CLOCK_BOOTTIME_ALARM     9
#define BLUEY_CLOCK_TAI               11

static void sys_fill_monotonic_timespec(k_timespec_t *tp) {
    uint32_t ticks = timer_get_ticks();
    uint32_t freq = timer_get_freq();
    uint32_t sec = ticks / freq;
    uint32_t rem = ticks % freq;
    uint32_t scale = 1000000000u / (freq ? freq : 1000u);

    tp->tv_sec = sec;
    tp->tv_nsec = rem * scale;
}

static void sys_fill_realtime_timespec(k_timespec_t *tp) {
    uint32_t unix_secs;

    sys_fill_monotonic_timespec(tp);
    if (rtc_get_unix_time(&unix_secs)) {
        tp->tv_sec = unix_secs;
    }
}

static int32_t sys_clock_gettime(int clk_id, k_timespec_t *tp) {
    if (!tp) return -BLUEY_EFAULT;

    switch (clk_id) {
        case BLUEY_CLOCK_REALTIME:
        case BLUEY_CLOCK_REALTIME_COARSE:
        case BLUEY_CLOCK_REALTIME_ALARM:
        case BLUEY_CLOCK_TAI:
            sys_fill_realtime_timespec(tp);
            return 0;
        case BLUEY_CLOCK_MONOTONIC:
        case BLUEY_CLOCK_MONOTONIC_RAW:
        case BLUEY_CLOCK_MONOTONIC_COARSE:
        case BLUEY_CLOCK_BOOTTIME:
        case BLUEY_CLOCK_BOOTTIME_ALARM:
        case BLUEY_CLOCK_PROCESS_CPUTIME_ID:
        case BLUEY_CLOCK_THREAD_CPUTIME_ID:
            /* CPU clocks are approximated with monotonic uptime until the
             * scheduler tracks per-process/per-thread CPU usage. */
            sys_fill_monotonic_timespec(tp);
            return 0;
        default:
            return -BLUEY_EINVAL;
    }
}

static int32_t sys_clock_gettime64(int clk_id, k_timespec64_t *tp) {
    k_timespec_t ts32;
    int32_t ret;

    if (!tp) return -BLUEY_EFAULT;
    ret = sys_clock_gettime(clk_id, &ts32);
    if (ret != 0) return ret;

    tp->tv_sec = (uint64_t)ts32.tv_sec;
    tp->tv_nsec = (uint64_t)ts32.tv_nsec;
    return 0;
}

static void sys_fill_stat64(vfs_stat_t *st, k_stat64_t *out) {
    uint32_t ticks = timer_get_ticks();

    memset(out, 0, sizeof(*out));
    out->st_dev = 1;
    out->st_mode = st->mode;
    out->st_nlink = st->is_dir ? 2u : 1u;
    out->st_uid = st->uid;
    out->st_gid = st->gid;
    out->st_size = st->size;
    out->st_blksize = 4096u;
    out->st_blocks = ((uint64_t)st->size + 511u) / 512u;
    out->st_atime_sec = (int32_t)ticks;
    out->st_mtime_sec = (int32_t)ticks;
    out->st_ctime_sec = (int32_t)ticks;
}

static int32_t sys_fstat64(int fd, k_stat64_t *buf) {
    vfs_stat_t st;

    if (fd < 0) return -BLUEY_EBADF;
    if (!buf) return -BLUEY_EFAULT;
    if (vfs_fstat(fd, &st) != 0) return -BLUEY_EBADF;

    sys_fill_stat64(&st, buf);
    return 0;
}

static int32_t sys_statx(int dirfd, const char *path, int flags, uint32_t mask, k_statx_t *buf) {
    vfs_stat_t st;
    int rc;

    (void)mask;
    if (!buf) return -BLUEY_EFAULT;

    memset(buf, 0, sizeof(*buf));

    if (flags & AT_EMPTY_PATH) {
        if (!path || !path[0]) {
            if (dirfd < 0) return -BLUEY_EBADF;
            rc = vfs_fstat(dirfd, &st);
            if (rc != 0) return -BLUEY_EBADF;
        } else {
            rc = vfs_stat(path, &st);
            if (rc != 0) return -BLUEY_ENOENT;
        }
    } else {
        if (!path) return -BLUEY_EFAULT;
        if (path[0] != '/' && dirfd != AT_FDCWD) return -BLUEY_ENOSYS;
        if ((flags & AT_SYMLINK_NOFOLLOW) != 0) {
            /* BlueyFS has no distinct lstat path yet; report regular metadata. */
        }
        rc = vfs_stat(path, &st);
        if (rc != 0) return -BLUEY_ENOENT;
    }

    buf->stx_mask = STATX_BASIC_STATS;
    buf->stx_blksize = 4096u;
    buf->stx_nlink = st.is_dir ? 2u : 1u;
    buf->stx_uid = st.uid;
    buf->stx_gid = st.gid;
    buf->stx_mode = st.mode;
    buf->stx_size = st.size;
    buf->stx_blocks = ((uint64_t)st.size + 511u) / 512u;
    return 0;
}

static int32_t sys_writev(int fd, const k_iovec_t *iov, uint32_t iovcnt) {
    int32_t total = 0;

    if (!iov) return -BLUEY_EFAULT;
    if (iovcnt > SYS_IOV_MAX) return -BLUEY_EINVAL;

    for (uint32_t index = 0; index < iovcnt; index++) {
        const char *base = (const char *)(uintptr_t)iov[index].iov_base;
        size_t len = (size_t)iov[index].iov_len;
        int32_t written;

        if (len == 0) continue;
        written = sys_write((uint32_t)fd, base, len);
        if (written < 0) {
            if (total > 0) return total;
            return written;
        }
        total += written;
        if ((size_t)written < len) break;
    }

    return total;
}

static int32_t sys_fstat(int fd, void *buf) {
    if (fd < 0) return -BLUEY_EBADF;
    if (!buf) return -BLUEY_EFAULT;
    vfs_stat_t st;
    if (vfs_fstat(fd, &st) != 0) return -BLUEY_EBADF;
    /* Layout filled after vfs_stat_to_buf is defined below; forward-call via
     * inline copy to avoid forward-declaration dependency. */
    uint32_t ticks = timer_get_ticks();
    uint32_t *u = (uint32_t *)buf;
    memset(u, 0, 64);
    u[0]  = 1;
    u[1]  = 1;
    u[2]  = (uint32_t)st.mode | ((uint32_t)1 << 16);
    u[3]  = (uint32_t)st.uid | ((uint32_t)st.gid << 16);
    u[4]  = 0;
    u[5]  = st.size;
    u[6]  = 512;
    u[7]  = (st.size + 511) / 512;
    u[8]  = ticks;
    u[9]  = 0;
    u[10] = ticks;
    u[11] = 0;
    u[12] = ticks;
    u[13] = 0;
    return 0;
}

static int32_t sys_sigreturn(registers_t *regs, void *frame_ptr) {
    process_t *process = process_current();
    if (!process || !regs) return -BLUEY_EPERM;
    return signal_sigreturn(process, regs, frame_ptr);
}

/* ---- stat / lstat / access / unlink / mkdir / rmdir -------------------- */

/*
 * Linux-compatible stat buffer layout for i386 (struct stat, 64-byte):
 *   u32 st_dev, u32 st_ino, u16 st_mode, u16 st_nlink, u16 st_uid, u16 st_gid
 *   u32 st_rdev, u32 st_size, u32 st_blksize, u32 st_blocks
 *   u32 st_atime, u32 st_atime_ns, u32 st_mtime, u32 st_mtime_ns
 *   u32 st_ctime, u32 st_ctime_ns
 */
static void vfs_stat_to_buf(const vfs_stat_t *st, uint32_t *u) {
    uint32_t ticks = timer_get_ticks();
    memset(u, 0, 64);
    u[0]  = 1;               /* st_dev */
    u[1]  = 1;               /* st_ino (placeholder) */
    /* st_mode (16-bit) in low half of u[2], st_nlink (16-bit) in high */
    u[2]  = (uint32_t)st->mode | ((uint32_t)1 << 16); /* nlink = 1 */
    /* st_uid / st_gid packed into u[3] */
    u[3]  = (uint32_t)st->uid | ((uint32_t)st->gid << 16);
    u[4]  = 0;               /* st_rdev */
    u[5]  = st->size;        /* st_size */
    u[6]  = 512;             /* st_blksize */
    u[7]  = (st->size + 511) / 512; /* st_blocks */
    u[8]  = ticks;           /* st_atime (ticks as proxy) */
    u[9]  = 0;               /* st_atime_ns */
    u[10] = ticks;           /* st_mtime */
    u[11] = 0;               /* st_mtime_ns */
    u[12] = ticks;           /* st_ctime */
    u[13] = 0;               /* st_ctime_ns */
}

static int32_t sys_stat(const char *path, void *buf) {
    if (!path) return -BLUEY_EFAULT;
    if (!buf)  return -BLUEY_EFAULT;
    vfs_stat_t st;
    if (vfs_stat(path, &st) != 0) return -BLUEY_ENOENT;
    vfs_stat_to_buf(&st, (uint32_t *)buf);
    return 0;
}

static int32_t sys_lstat(const char *path, void *buf) {
    /* No symlinks — lstat is identical to stat */
    return sys_stat(path, buf);
}

static int32_t sys_access(const char *path, int mode) {
    if (!path) return -BLUEY_EFAULT;
    vfs_stat_t st;
    if (vfs_stat(path, &st) != 0) return -BLUEY_ENOENT;
    if (mode == 0) return 0; /* F_OK */
    uint8_t need = 0;
    if (mode & 4) need |= VFS_ACCESS_READ;
    if (mode & 2) need |= VFS_ACCESS_WRITE;
    if (mode & 1) need |= VFS_ACCESS_EXEC;
    return vfs_access(path, need) == 0 ? 0 : -BLUEY_EPERM;
}

static int32_t sys_unlink(const char *path) {
    if (!path) return -BLUEY_EFAULT;
    /* First check whether the path exists so we can distinguish ENOENT
     * from other unlink failures (e.g., permissions or wrong type). */
    vfs_stat_t st;
    if (vfs_stat(path, &st) != 0) {
        return -BLUEY_ENOENT;
    }
    /* Path exists but unlink failed: report a generic permission error
     * rather than incorrectly mapping everything to ENOENT. */
    return vfs_unlink(path) == 0 ? 0 : -BLUEY_EPERM;
}

static int32_t sys_mkdir(const char *path, uint32_t mode) {
    (void)mode;
    if (!path) return -BLUEY_EFAULT;
    return vfs_mkdir(path);
}

static int32_t sys_rmdir(const char *path) {
    if (!path) return -BLUEY_EFAULT;
    int32_t res = vfs_rmdir(path);
    return res;
}

typedef struct {
    int32_t tv_sec;
    int32_t tv_usec;
} k_timeval_t;

static int32_t sys_gettimeofday(k_timeval_t *tv, void *tz) {
    k_timespec_t ts;

    (void)tz;
    if (!tv) return 0;

    if (sys_clock_gettime(BLUEY_CLOCK_REALTIME, &ts) != 0) {
        return -BLUEY_EINVAL;
    }

    tv->tv_sec = (int32_t)ts.tv_sec;
    tv->tv_usec = (int32_t)(ts.tv_nsec / 1000u);
    return 0;
}

/* Allow privileged users to set the RTC-backed wall clock. Minimal
 * implementation: only accept updates to CLOCK_REALTIME via either
 * settimeofday(2) or clock_settime(2). */
static int32_t sys_settimeofday(const k_timeval_t *tv, void *tz) {
    process_t *p = process_current();
    if (!p) return -BLUEY_EPERM;
    /* Only allow privileged processes to set the wall clock. */
    if (p->euid != 0) return -BLUEY_EPERM;
    if (!tv) return -BLUEY_EINVAL;
    if (tv->tv_usec < 0 || tv->tv_usec >= 1000000) return -BLUEY_EINVAL;
    if ((uint64_t)tv->tv_sec > 0xFFFFFFFFull) return -BLUEY_EINVAL;

    rtc_set_unix_time((uint32_t)tv->tv_sec);
    return 0;
}

static int32_t sys_clock_settime64(int clk_id, const k_timespec64_t *ts) {
    if (!ts) return -BLUEY_EFAULT;

    /* Validate nanoseconds range */
    if (ts->tv_nsec >= 1000000000ull) return -BLUEY_EINVAL;
    if (ts->tv_sec > 0xFFFFFFFFull) return -BLUEY_EINVAL;

    switch (clk_id) {
        case BLUEY_CLOCK_REALTIME:
        case BLUEY_CLOCK_REALTIME_COARSE:
        case BLUEY_CLOCK_REALTIME_ALARM:
        case BLUEY_CLOCK_TAI:
            /* Only privileged callers may set wall clock */
            if (process_current() == NULL || process_current()->euid != 0) return -BLUEY_EPERM;
            rtc_set_unix_time((uint32_t)ts->tv_sec);
            return 0;

        case BLUEY_CLOCK_MONOTONIC:
        case BLUEY_CLOCK_MONOTONIC_RAW:
        case BLUEY_CLOCK_MONOTONIC_COARSE:
        case BLUEY_CLOCK_BOOTTIME:
        case BLUEY_CLOCK_BOOTTIME_ALARM:
        case BLUEY_CLOCK_PROCESS_CPUTIME_ID:
        case BLUEY_CLOCK_THREAD_CPUTIME_ID:
            /* Setting these clocks is not permitted from userspace. */
            return -BLUEY_EPERM;

        default:
            return -BLUEY_EINVAL;
    }
}

typedef struct {
    uint16_t sa_family;
    char     sa_data[14];
} k_sockaddr_t;

typedef struct {
    uint16_t sun_family;
    char     sun_path[108];
} k_sockaddr_un_t;

// IPv4 sockaddr (basic): family, port (network order), addr (network order)
typedef struct {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
} k_sockaddr_in_t;

// sockaddr for AF_BLUEY_NETCTL: bind() uses this to subscribe to multicast groups
typedef struct {
    uint16_t sn_family;   // BLUEY_AF_NETCTL
    uint16_t sn_pad;      // padding
    uint32_t sn_groups;   // Multicast group bitmask (NETCTL_GROUP_*)
} k_sockaddr_netctl_t;

#define SOCKETCALL_SOCKET   1
#define SOCKETCALL_BIND     2
#define SOCKETCALL_CONNECT  3
#define SOCKETCALL_LISTEN   4
#define SOCKETCALL_ACCEPT   5

// Simple message header structure for sendmsg/recvmsg
struct bluey_msghdr {
    void    *msg_name;        // Optional address
    uint32_t msg_namelen;     // Size of address
    void    *msg_iov;         // Scatter/gather array (struct bluey_iovec*)
    uint32_t msg_iovlen;      // Number of elements in msg_iov
    void    *msg_control;     // Ancillary data (not used)
    uint32_t msg_controllen;  // Ancillary data buffer length
    int      msg_flags;       // Flags on received message
};

struct bluey_iovec {
    void    *iov_base;  // Starting address
    uint32_t iov_len;   // Number of bytes
};

static int sys_socket_extract_path(const void *addr, uint32_t addrlen,
                                   char *path, size_t path_size) {
    const k_sockaddr_un_t *un = (const k_sockaddr_un_t*)addr;
    size_t max_path;
    size_t path_len = 0;

    if (!addr || !path || path_size == 0) return -BLUEY_EFAULT;
    if (addrlen < sizeof(uint16_t)) return -BLUEY_EINVAL;
    if (un->sun_family != BLUEY_AF_UNIX) return -BLUEY_EAFNOSUPPORT;

    max_path = addrlen > sizeof(uint16_t) ? (size_t)(addrlen - sizeof(uint16_t)) : 0;
    if (max_path > sizeof(un->sun_path)) max_path = sizeof(un->sun_path);
    if (max_path == 0) return -BLUEY_EINVAL;

    while (path_len < max_path && un->sun_path[path_len]) path_len++;
    if (path_len == 0 || path_len >= path_size) return -BLUEY_EINVAL;

    memcpy(path, un->sun_path, path_len);
    path[path_len] = '\0';
    return 0;
}

static int32_t sys_socket_open(int domain, int type, int protocol) {
    int socket_id;
    int fd;

    // Support both AF_UNIX and AF_NETCTL families
    if (domain == BLUEY_AF_UNIX) {
        if (type != BLUEY_SOCK_STREAM && type != BLUEY_SOCK_DGRAM) return -BLUEY_EPROTONOSUPPORT;
        if (protocol != 0) return -BLUEY_EPROTONOSUPPORT;
    } else if (domain == BLUEY_AF_NETCTL) {
        if (type != BLUEY_SOCK_NETCTL) return -BLUEY_EPROTONOSUPPORT;
        // protocol is passed to netctl for future extensibility
    } else if (domain == BLUEY_AF_INET) {
        if (type != BLUEY_SOCK_DGRAM) return -BLUEY_EPROTONOSUPPORT;
        // protocol 0 = UDP
    } else {
        return -BLUEY_EAFNOSUPPORT;
    }

    socket_id = socket_create(domain, type, protocol);
    if (socket_id < 0) return -BLUEY_ENOMEM;

    fd = vfs_socket_open(socket_id);
    if (fd < 0) {
        socket_close(socket_id);
        return -BLUEY_EMFILE;
    }

    return fd;
}

static int32_t sys_socket_bind(int fd, const void *addr, uint32_t addrlen) {
    int socket_id;
    int rc;

    if (!vfs_fd_is_socket(fd)) return -BLUEY_ENOTSOCK;
    socket_id = vfs_socket_id(fd);
    if (socket_id < 0) return -BLUEY_EBADF;

    // NETCTL sockets bind to multicast group subscriptions via sockaddr_netctl
    if (socket_is_netctl(socket_id)) {
        const k_sockaddr_netctl_t *sn = (const k_sockaddr_netctl_t *)addr;
        if (!addr || addrlen < sizeof(*sn)) return -BLUEY_EINVAL;
        if (sn->sn_family != BLUEY_AF_NETCTL) return -BLUEY_EAFNOSUPPORT;
        // Pass groups via socket_bind using reinterpreted pointer convention
        return socket_bind(socket_id, (const char *)&sn->sn_groups);
    }

    // AF_INET datagram bind
    if (socket_is_inet(socket_id)) {
        const k_sockaddr_in_t *in = (const k_sockaddr_in_t *)addr;
        if (!addr || addrlen < sizeof(*in)) return -BLUEY_EINVAL;
        if (in->sin_family != BLUEY_AF_INET) return -BLUEY_EAFNOSUPPORT;
        uint16_t port = ntohs(in->sin_port);
        uint32_t ip   = ntohl(in->sin_addr);
        rc = socket_inet_bind(socket_id, ip, port);
        return rc == 0 ? 0 : -BLUEY_EINVAL;
    }

    // UNIX domain sockets bind to a path
    char path[VFS_PATH_LEN];
    rc = sys_socket_extract_path(addr, addrlen, path, sizeof(path));
    if (rc != 0) return rc;
    rc = socket_bind(socket_id, path);
    if (rc == 0) return 0;
    if (rc < 0) return rc;
    return -BLUEY_EINVAL;
}

static int32_t sys_socket_connect(int fd, const void *addr, uint32_t addrlen) {
    char path[VFS_PATH_LEN];
    int rc;

    if (!vfs_fd_is_socket(fd)) return -BLUEY_ENOTSOCK;
    rc = sys_socket_extract_path(addr, addrlen, path, sizeof(path));
    if (rc != 0) return rc;
    rc = socket_connect(vfs_socket_id(fd), path);
    if (rc == 0) return 0;
    if (rc == -BLUEY_EINVAL || rc == -BLUEY_EAGAIN || rc == -BLUEY_ECONNREFUSED) return rc;
    return -BLUEY_ECONNREFUSED;
}

static int32_t sys_socket_listen(int fd, int backlog) {
    if (!vfs_fd_is_socket(fd)) return -BLUEY_ENOTSOCK;
    return socket_listen(vfs_socket_id(fd), backlog) == 0 ? 0 : -BLUEY_EOPNOTSUPP;
}

static int32_t sys_socket_accept4(int fd, void *addr, uint32_t *addrlen, int flags) {
    int socket_id;
    int newfd;

    (void)addr;
    (void)flags;

    if (addrlen) *addrlen = 0;

    if (!vfs_fd_is_socket(fd)) return -BLUEY_ENOTSOCK;
    socket_id = socket_accept(vfs_socket_id(fd));
    if (socket_id < 0) return -BLUEY_EAGAIN;

    newfd = vfs_socket_open(socket_id);
    if (newfd < 0) {
        socket_close(socket_id);
        return -BLUEY_EMFILE;
    }
    return newfd;
}

static int32_t sys_sendmsg(int fd, const struct bluey_msghdr *msg, int flags) {
    const struct bluey_iovec *iov;
    int socket_id;

    (void)flags;  // Not used for netctl

    if (!msg || !msg->msg_iov || msg->msg_iovlen == 0) return -BLUEY_EINVAL;
    if (!vfs_fd_is_socket(fd)) return -BLUEY_ENOTSOCK;

    socket_id = vfs_socket_id(fd);
    if (socket_id < 0) return -BLUEY_EBADF;
    // Dispatch based on socket domain
    if (socket_is_netctl(socket_id)) {
        // Only single-iovec supported for netctl messages
        if (msg->msg_iovlen != 1) return -BLUEY_EINVAL;
        iov = (const struct bluey_iovec *)msg->msg_iov;
        if (!iov->iov_base || iov->iov_len == 0) return -BLUEY_EINVAL;
        int bytes_sent = socket_netctl_send(socket_id, iov->iov_base, iov->iov_len);
        return bytes_sent < 0 ? -BLUEY_EIO : bytes_sent;
    }

    if (socket_is_unix_dgram(socket_id)) {
        // msg_name must point to sockaddr_un describing destination path
        if (!msg->msg_name || msg->msg_namelen == 0) return -BLUEY_EINVAL;
        const k_sockaddr_un_t *un = (const k_sockaddr_un_t *)msg->msg_name;
        if (un->sun_family != BLUEY_AF_UNIX) return -BLUEY_EAFNOSUPPORT;
        // single iovec only
        if (msg->msg_iovlen != 1) return -BLUEY_EINVAL;
        iov = (const struct bluey_iovec *)msg->msg_iov;
        if (!iov->iov_base || iov->iov_len == 0) return -BLUEY_EINVAL;
        int rc = socket_unix_sendto(socket_id, un->sun_path, iov->iov_base, iov->iov_len);
        return rc < 0 ? -BLUEY_EIO : rc;
    }

    if (socket_is_inet(socket_id)) {
        if (!msg->msg_name || msg->msg_namelen < sizeof(k_sockaddr_in_t)) return -BLUEY_EINVAL;
        const k_sockaddr_in_t *in = (const k_sockaddr_in_t *)msg->msg_name;
        if (in->sin_family != BLUEY_AF_INET) return -BLUEY_EAFNOSUPPORT;
        if (msg->msg_iovlen != 1) return -BLUEY_EINVAL;
        iov = (const struct bluey_iovec *)msg->msg_iov;
        if (!iov->iov_base || iov->iov_len == 0) return -BLUEY_EINVAL;
        uint16_t port = ntohs(in->sin_port);
        uint32_t ip   = ntohl(in->sin_addr);
        int rc = socket_inet_sendto(socket_id, ip, port, iov->iov_base, iov->iov_len);
        return rc < 0 ? -BLUEY_EIO : rc;
    }

    return -BLUEY_EPROTONOSUPPORT;
}

static int32_t sys_recvmsg(int fd, struct bluey_msghdr *msg, int flags) {
    struct bluey_iovec *iov;
    int socket_id;

    (void)flags;  // Not used for netctl

    if (!msg || !msg->msg_iov || msg->msg_iovlen == 0) return -BLUEY_EINVAL;
    if (!vfs_fd_is_socket(fd)) return -BLUEY_ENOTSOCK;

    socket_id = vfs_socket_id(fd);
    if (socket_id < 0) return -BLUEY_EBADF;
    // Dispatch based on socket domain
    if (socket_is_netctl(socket_id)) {
        // Only single-iovec supported for netctl messages
        if (msg->msg_iovlen != 1) return -BLUEY_EINVAL;
        iov = (struct bluey_iovec *)msg->msg_iov;
        if (!iov->iov_base || iov->iov_len == 0) return -BLUEY_EINVAL;
        int bytes_received = socket_netctl_recv(socket_id, iov->iov_base, iov->iov_len);
        return bytes_received < 0 ? -BLUEY_EIO : bytes_received;
    }

    if (socket_is_unix_dgram(socket_id)) {
        if (msg->msg_iovlen != 1) return -BLUEY_EINVAL;
        iov = (struct bluey_iovec *)msg->msg_iov;
        if (!iov->iov_base || iov->iov_len == 0) return -BLUEY_EINVAL;
        // msg_name may point to a buffer to receive source address
        char *namebuf = NULL;
        size_t namebuf_sz = 0;
        if (msg->msg_name && msg->msg_namelen > 0) {
            namebuf = (char*)msg->msg_name;
            namebuf_sz = msg->msg_namelen;
        }
        int rc = socket_unix_recvfrom(socket_id, iov->iov_base, iov->iov_len, namebuf, namebuf_sz);
        return rc < 0 ? -BLUEY_EIO : rc;
    }

    if (socket_is_inet(socket_id)) {
        if (msg->msg_iovlen != 1) return -BLUEY_EINVAL;
        iov = (struct bluey_iovec *)msg->msg_iov;
        if (!iov->iov_base || iov->iov_len == 0) return -BLUEY_EINVAL;
        uint32_t src_ip = 0;
        uint16_t src_port = 0;
        int rc = socket_inet_recvfrom(socket_id, iov->iov_base, iov->iov_len, &src_ip, &src_port);
        if (rc < 0) return -BLUEY_EIO;
        // If caller provided msg_name buffer, fill sockaddr_in
        if (msg->msg_name && msg->msg_namelen >= sizeof(k_sockaddr_in_t)) {
            k_sockaddr_in_t *in = (k_sockaddr_in_t *)msg->msg_name;
            in->sin_family = BLUEY_AF_INET;
            in->sin_port = htons(src_port);
            in->sin_addr = htonl(src_ip);
        }
        return rc;
    }

    return -BLUEY_EPROTONOSUPPORT;
}

static int32_t sys_socketcall(int call, uint32_t *args) {
    if (!args) return -BLUEY_EFAULT;

    switch (call) {
        case SOCKETCALL_SOCKET:
            return sys_socket_open((int)args[0], (int)args[1], (int)args[2]);
        case SOCKETCALL_BIND:
            return sys_socket_bind((int)args[0], (const void*)(uintptr_t)args[1], args[2]);
        case SOCKETCALL_CONNECT:
            return sys_socket_connect((int)args[0], (const void*)(uintptr_t)args[1], args[2]);
        case SOCKETCALL_LISTEN:
            return sys_socket_listen((int)args[0], (int)args[1]);
        case SOCKETCALL_ACCEPT:
            return sys_socket_accept4((int)args[0], (void*)(uintptr_t)args[1],
                                      (uint32_t*)(uintptr_t)args[2], 0);
        default:
            return -BLUEY_ENOSYS;
    }
}

static int32_t sys_rename(const char *oldpath, const char *newpath) {
    vfs_stat_t old_st;
    vfs_stat_t new_st;

    if (!oldpath || !newpath) return -BLUEY_EFAULT;
    if (strcmp(oldpath, newpath) == 0) return 0;
    if (vfs_stat(oldpath, &old_st) != 0) return -BLUEY_ENOENT;
    if (old_st.is_dir) return -BLUEY_EPERM;

    if (vfs_stat(newpath, &new_st) == 0) {
        if (new_st.is_dir) return -BLUEY_EISDIR;
        if (vfs_unlink(newpath) != 0) return -BLUEY_EPERM;
    }

    if (vfs_link(oldpath, newpath) != 0) return -BLUEY_EPERM;
    if (vfs_unlink(oldpath) != 0) {
        vfs_unlink(newpath);
        return -BLUEY_EPERM;
    }

    return 0;
}

static int32_t sys_select(int nfds,
                          void *readfds,
                          void *writefds,
                          void *exceptfds,
                          const k_timeval_t *timeout) {
    (void)exceptfds;
    uint32_t ready = 0;
    uint32_t *rfds = (uint32_t*)readfds;
    uint32_t *wfds = (uint32_t*)writefds;
    uint32_t ready_read[32];
    uint32_t ready_write[32];

    memset(ready_read, 0, sizeof(ready_read));
    memset(ready_write, 0, sizeof(ready_write));

    if (nfds < 0) return -BLUEY_EINVAL;

    if (nfds > (int)(ARRAY_SIZE(ready_read) * 32u)) return -BLUEY_EINVAL;

    for (int fd = 0; fd < nfds; fd++) {
        uint32_t mask = 1u << (fd & 31);
        uint32_t word = (uint32_t)fd >> 5;
        int want_read = rfds && (rfds[word] & mask);
        int want_write = wfds && (wfds[word] & mask);
        int fd_counted = 0;

        if (want_read) {
            int is_ready = 0;
            if (fd == 0) is_ready = keyboard_available();
            else if (vfs_fd_is_devev(fd)) is_ready = devev_pending();
            else if (vfs_fd_is_tty(fd)) is_ready = 1;
            else if (vfs_fd_is_socket(fd)) is_ready = socket_is_readable(vfs_socket_id(fd));
            else if (fd >= 3 && fd < VFS_MAX_OPEN) is_ready = 1;

            if (is_ready) {
                ready_read[word] |= mask;
                if (!fd_counted) { ready++; fd_counted = 1; }
            }
        }

        if (want_write) {
            int is_ready = 0;
            if (fd == 1 || fd == 2) is_ready = 1;
            else if (vfs_fd_is_tty(fd)) is_ready = 1;
            else if (vfs_fd_is_socket(fd)) is_ready = socket_is_writable(vfs_socket_id(fd));
            else if (fd >= 3 && fd < VFS_MAX_OPEN) is_ready = 1;

            if (is_ready) {
                ready_write[word] |= mask;
                if (!fd_counted) { ready++; fd_counted = 1; }
            }
        }
    }

    if (rfds) memcpy(rfds, ready_read, sizeof(ready_read));
    if (wfds) memcpy(wfds, ready_write, sizeof(ready_write));

    if (ready > 0) return (int32_t)ready;

    /* claw currently uses select(0, NULL, NULL, NULL, &tv) as a timed sleep. */
    if (nfds == 0) {
        if (!timeout) {
            process_set_waiting(process_current());
            return 0;
        }

        if (timeout->tv_sec < 0 || timeout->tv_usec < 0 || timeout->tv_usec >= 1000000) {
            return -BLUEY_EINVAL;
        }

        uint32_t ms = (uint32_t)timeout->tv_sec * 1000u;
        ms += (uint32_t)timeout->tv_usec / 1000u;
        if (ms) process_sleep(ms);
        return 0;
    }

    if (!timeout) {
        process_set_waiting(process_current());
        return 0;
    }

    if (timeout->tv_sec < 0 || timeout->tv_usec < 0 || timeout->tv_usec >= 1000000) {
        return -BLUEY_EINVAL;
    }

    {
        uint32_t ms = (uint32_t)timeout->tv_sec * 1000u;
        ms += (uint32_t)timeout->tv_usec / 1000u;
        if (ms) process_sleep(ms);
    }

    if (rfds) memset(rfds, 0, sizeof(ready_read));
    if (wfds) memset(wfds, 0, sizeof(ready_write));
    return 0;
}

/* ---- link / symlink / readlink ----------------------------------------- */


static int syscall_id_change_allowed(const process_t *process,
                                     uint32_t current_real,
                                     uint32_t current_effective,
                                     uint32_t requested) {
    if (!process) return 0;
    if (requested == 0xFFFFFFFFu) return 1;
    if (process->euid == 0) return 1;
    return requested == current_real || requested == current_effective;
}

static void syscall_refresh_groups(process_t *process) {
    size_t count;

    if (!process) return;

    count = multiuser_get_groups(process->uid, process->gid,
                                 process->groups, PROC_MAX_GROUPS);
    process->group_count = (uint32_t)count;
    if (process->group_count == 0 && PROC_MAX_GROUPS > 0) {
        process->groups[0] = process->gid;
        process->group_count = 1;
    }
}

static int32_t sys_setresuid32(uint32_t ruid, uint32_t euid, uint32_t suid) {
    process_t *process = process_current();

    if (!process) return -BLUEY_EPERM;

    if (!syscall_id_change_allowed(process, process->uid, process->euid, ruid) ||
        !syscall_id_change_allowed(process, process->uid, process->euid, euid) ||
        !syscall_id_change_allowed(process, process->uid, process->euid, suid)) {
        return -BLUEY_EPERM;
    }

    if (ruid != 0xFFFFFFFFu) process->uid = ruid;
    if (euid != 0xFFFFFFFFu) process->euid = euid;

    /* BlueyOS does not yet track a separate saved uid. */
    syscall_refresh_groups(process);
    return 0;
}

static int32_t sys_setresgid32(uint32_t rgid, uint32_t egid, uint32_t sgid) {
    process_t *process = process_current();

    if (!process) return -BLUEY_EPERM;

    if (!syscall_id_change_allowed(process, process->gid, process->egid, rgid) ||
        !syscall_id_change_allowed(process, process->gid, process->egid, egid) ||
        !syscall_id_change_allowed(process, process->gid, process->egid, sgid)) {
        return -BLUEY_EPERM;
    }

    if (rgid != 0xFFFFFFFFu) process->gid = rgid;
    if (egid != 0xFFFFFFFFu) process->egid = egid;

    /* BlueyOS does not yet track a separate saved gid. */
    syscall_refresh_groups(process);
    return 0;
}

static int32_t sys_setuid32(uint32_t uid) {
    return sys_setresuid32(uid, uid, 0xFFFFFFFFu);
}

static int32_t sys_setgid32(uint32_t gid) {
    return sys_setresgid32(gid, gid, 0xFFFFFFFFu);
}

static int32_t sys_setreuid32(uint32_t ruid, uint32_t euid) {
    return sys_setresuid32(ruid, euid, 0xFFFFFFFFu);
}

static int32_t sys_setregid32(uint32_t rgid, uint32_t egid) {
    return sys_setresgid32(rgid, egid, 0xFFFFFFFFu);
}

static int32_t sys_getgroups32(int size, uint32_t *list) {
    process_t *process = process_current();

    if (!process) return -BLUEY_EPERM;
    if (size < 0) return -BLUEY_EINVAL;
    if (size == 0) return (int32_t)process->group_count;
    if (!list) return -BLUEY_EFAULT;
    if ((uint32_t)size < process->group_count) return -BLUEY_EINVAL;

    for (uint32_t index = 0; index < process->group_count; index++) {
        list[index] = process->groups[index];
    }
    return (int32_t)process->group_count;
}

static int32_t sys_setgroups32(size_t size, const uint32_t *list) {
    process_t *process = process_current();

    if (!process) return -BLUEY_EPERM;
    if (!multiuser_is_root()) return -BLUEY_EPERM;
    if (size > PROC_MAX_GROUPS) return -BLUEY_EINVAL;
    if (size > 0 && !list) return -BLUEY_EFAULT;

    process->group_count = (uint32_t)size;
    for (uint32_t index = 0; index < process->group_count; index++) {
        process->groups[index] = list[index];
    }
    return 0;
}

static int32_t sys_getresuid32(uint32_t *ruid, uint32_t *euid, uint32_t *suid) {
    process_t *process = process_current();

    if (!process) return -BLUEY_EPERM;
    if (!ruid || !euid || !suid) return -BLUEY_EFAULT;

    *ruid = process->uid;
    *euid = process->euid;
    *suid = process->euid;
    return 0;
}

static int32_t sys_getresgid32(uint32_t *rgid, uint32_t *egid, uint32_t *sgid) {
    process_t *process = process_current();

    if (!process) return -BLUEY_EPERM;
    if (!rgid || !egid || !sgid) return -BLUEY_EFAULT;

    *rgid = process->gid;
    *egid = process->egid;
    *sgid = process->egid;
    return 0;
}

static int32_t sys_setfsuid32(uint32_t uid) {
    process_t *process = process_current();

    (void)uid;
    if (!process) return -BLUEY_EPERM;

    /* BlueyOS does not yet track a separate fsuid; report the current euid. */
    return (int32_t)process->euid;
}

static int32_t sys_setfsgid32(uint32_t gid) {
    process_t *process = process_current();

    (void)gid;
    if (!process) return -BLUEY_EPERM;

    /* BlueyOS does not yet track a separate fsgid; report the current egid. */
    return (int32_t)process->egid;
}

static int32_t sys_pselect6(int nfds,
                            void *readfds,
                            void *writefds,
                            void *exceptfds,
                            const compat_timespec32_t *timeout_ts,
                            const compat_pselect6_data_t *data) {
    k_timeval_t timeout_tv;
    k_timeval_t *timeout_ptr = NULL;

    (void)data;

    if (timeout_ts) {
        if (timeout_ts->tv_sec < 0 || timeout_ts->tv_nsec < 0 || timeout_ts->tv_nsec >= 1000000000) {
            return -BLUEY_EINVAL;
        }

        timeout_tv.tv_sec = timeout_ts->tv_sec;
        timeout_tv.tv_usec = timeout_ts->tv_nsec / 1000;
        timeout_ptr = &timeout_tv;
    }

    return sys_select(nfds, readfds, writefds, exceptfds, timeout_ptr);
}

static int32_t sys_prlimit64(uint32_t pid,
                             uint32_t resource,
                             uint32_t new_limit_ptr,
                             uint32_t old_limit_ptr) {
    process_t *caller = process_current();
    process_t *target;
    uint32_t old_dir;

    if (!caller) return -BLUEY_EPERM;
    if (resource != RLIMIT_STACK) return -BLUEY_EINVAL;

    if (pid == 0 || pid == caller->pid) {
        target = caller;
    } else {
        target = process_get_by_pid(pid);
        if (!target) return -BLUEY_ESRCH;
        if (caller->euid != 0) return -BLUEY_EPERM;
    }

    if (new_limit_ptr) {
        compat_rlimit64_t requested;
        uint32_t new_cur;
        uint32_t new_max;

        old_dir = paging_current_directory();
        paging_switch_directory(caller->page_dir);
        requested = *(compat_rlimit64_t*)(uintptr_t)new_limit_ptr;
        paging_switch_directory(old_dir);

        if (requested.rlim_cur > requested.rlim_max) return -BLUEY_EINVAL;

        if (requested.rlim_cur == ~0ull) new_cur = 0xFFFFFFFFu;
        else if (requested.rlim_cur <= 0xFFFFFFFFull) new_cur = (uint32_t)requested.rlim_cur;
        else return -BLUEY_EINVAL;

        if (requested.rlim_max == ~0ull) new_max = 0xFFFFFFFFu;
        else if (requested.rlim_max <= 0xFFFFFFFFull) new_max = (uint32_t)requested.rlim_max;
        else return -BLUEY_EINVAL;

        target->rlimit_stack_cur = new_cur;
        target->rlimit_stack_max = new_max;
    }

    if (old_limit_ptr) {
        compat_rlimit64_t current;

        current.rlim_cur = (target->rlimit_stack_cur == 0xFFFFFFFFu)
            ? ~0ull : (uint64_t)target->rlimit_stack_cur;
        current.rlim_max = (target->rlimit_stack_max == 0xFFFFFFFFu)
            ? ~0ull : (uint64_t)target->rlimit_stack_max;

        old_dir = paging_current_directory();
        paging_switch_directory(caller->page_dir);
        *(compat_rlimit64_t*)(uintptr_t)old_limit_ptr = current;
        paging_switch_directory(old_dir);
    }

    return 0;
}
static int32_t sys_link(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath) return -BLUEY_EFAULT;
    vfs_stat_t st;
    if (vfs_stat(oldpath, &st) != 0) return -BLUEY_ENOENT;
    int r = vfs_link(oldpath, newpath);
    return (r == 0) ? 0 : -BLUEY_EPERM;
}

static int32_t sys_symlink(const char *target, const char *linkpath) {
    if (!target || !linkpath) return -BLUEY_EFAULT;
    int r = vfs_symlink(target, linkpath);
    return (r == 0) ? 0 : -BLUEY_EPERM;
}

static int32_t sys_readlink(const char *path, char *buf, uint32_t bufsize) {
    if (!path || !buf) return -BLUEY_EFAULT;
    if (bufsize == 0) return -BLUEY_EINVAL;
    vfs_stat_t st;
    if (vfs_stat(path, &st) != 0) return -BLUEY_ENOENT;
    /* readlink(2) requires the target to be a symlink */
    if ((st.mode & 0xF000u) != 0xA000u) return -BLUEY_EINVAL;
    int r = vfs_readlink(path, buf, bufsize);
    return (r >= 0) ? r : -BLUEY_EPERM;
}

/* ---- chmod / fchmod ----------------------------------------------------- */

static int32_t sys_chmod(const char *path, uint32_t mode) {
    if (!path) return -BLUEY_EFAULT;
    vfs_stat_t st;
    if (vfs_stat(path, &st) != 0) return -BLUEY_ENOENT;
    int r = vfs_chmod(path, (uint16_t)mode);
    return (r == 0) ? 0 : -BLUEY_EPERM;
}

static int32_t sys_fchmod(int fd, uint32_t mode) {
    if (fd < 0) return -BLUEY_EBADF;
    vfs_stat_t fst;
    /* Verify the fd is valid before attempting to change mode */
    if (vfs_fstat(fd, &fst) != 0) return -BLUEY_EBADF;
    int r = vfs_fchmod(fd, (uint16_t)mode);
    return (r == 0) ? 0 : -BLUEY_EPERM;
}

/* ---- chown / lchown / fchown ------------------------------------------- */

static int32_t sys_chown(const char *path, uint32_t uid, uint32_t gid) {
    if (!path) return -BLUEY_EFAULT;
    vfs_stat_t st;
    if (vfs_stat(path, &st) != 0) return -BLUEY_ENOENT;
    int r = vfs_chown(path, uid, gid);
    return (r == 0) ? 0 : -BLUEY_EPERM;
}

static int32_t sys_lchown(const char *path, uint32_t uid, uint32_t gid) {
    if (!path) return -BLUEY_EFAULT;
    vfs_stat_t st;
    if (vfs_stat(path, &st) != 0) return -BLUEY_ENOENT;
    int r = vfs_lchown(path, uid, gid);
    return (r == 0) ? 0 : -BLUEY_EPERM;
}

static int32_t sys_fchown(int fd, uint32_t uid, uint32_t gid) {
    if (fd < 0) return -BLUEY_EBADF;
    vfs_stat_t fst;
    if (vfs_fstat(fd, &fst) != 0) return -BLUEY_EBADF;
    int r = vfs_fchown(fd, uid, gid);
    return (r == 0) ? 0 : -BLUEY_EPERM;
}

/* ---- lseek -------------------------------------------------------------- */

static int32_t sys_lseek(int fd, int32_t offset, int whence) {
    if (fd < 0) return -BLUEY_EBADF;

    /* Validate whence for POSIX-like semantics. Only VFS_SEEK_SET/VFS_SEEK_CUR/
     * VFS_SEEK_END are valid. Invalid whence should yield -EINVAL. */
    if (whence != VFS_SEEK_SET && whence != VFS_SEEK_CUR && whence != VFS_SEEK_END) {
        return -BLUEY_EINVAL;
    }

    /* For VFS_SEEK_SET, a negative offset is always invalid and should be EINVAL. */
    if (whence == VFS_SEEK_SET && offset < 0) {
        return -BLUEY_EINVAL;
    }

    /* Delegate to VFS and return its result (or error code). */
    return vfs_lseek(fd, offset, whence);
}

/* Legacy _llseek syscall (i386 ABI):
 * args: ebx=fd, ecx=offset_high, edx=offset_low, esi=result_ptr, edi=whence
 * On success writes the (32-bit) resulting offset to *result_ptr and returns 0.
 */
static int32_t sys__llseek(uint32_t fd, uint32_t offset_hi, uint32_t offset_lo,
                          uint32_t result_ptr, int whence) {
    uint64_t off = ((uint64_t)offset_hi << 32) | (uint64_t)offset_lo;
    uint32_t old_dir;

    /* Validate fd. */
    if ((int)fd < 0) return -BLUEY_EBADF;

    /* Validate whence. */
    if (whence != VFS_SEEK_SET && whence != VFS_SEEK_CUR && whence != VFS_SEEK_END)
        return -BLUEY_EINVAL;

    /* This kernel currently only supports 32-bit file offsets. Reject larger */
    if (off > 0xFFFFFFFFull) return -BLUEY_EINVAL;

    /* For VFS_SEEK_SET, a negative offset is always invalid. */
    if (whence == VFS_SEEK_SET && (int32_t)off < 0) return -BLUEY_EINVAL;

    if (result_ptr == 0) return -BLUEY_EFAULT;

    int32_t newoff = vfs_lseek((int)fd, (int32_t)off, whence);
    if (newoff < 0) return newoff;  /* preserve real errno from vfs_lseek */

    /* Write back the 32-bit result to the user-supplied pointer using the
     * caller's page directory (same safe pattern as sys_prlimit64). */
    process_t *caller = process_current();
    if (!caller) return -BLUEY_EPERM;
    old_dir = paging_current_directory();
    paging_switch_directory(caller->page_dir);
    *(uint32_t*)(uintptr_t)result_ptr = (uint32_t)newoff;
    paging_switch_directory(old_dir);
    return 0;
}

/* ---- dup / dup2 / pipe / fcntl / ioctl ---------------------------------- */

static int32_t sys_dup(int oldfd) {
    if (oldfd < 0) return -BLUEY_EBADF;
    int r = vfs_dup(oldfd);
    return r < 0 ? -BLUEY_EBADF : r;
}

static int32_t sys_dup2_impl(int oldfd, int newfd) {
    if (oldfd < 0 || newfd < 0) return -BLUEY_EBADF;
    int r = vfs_dup2(oldfd, newfd);
    return r < 0 ? -BLUEY_EBADF : r;
}

static int32_t sys_pipe_impl(int *fds) {
    if (!fds) return -BLUEY_EFAULT;
    int r = vfs_pipe(fds);
    return r < 0 ? r : 0;
}

/* pipe2(fds, flags) — identical to pipe() for our purposes.
 * We accept but ignore O_CLOEXEC (0x80000) and O_NONBLOCK (0x800).
 * Bash uses pipe2 when it is available; the kernel simply creates a
 * plain pipe and the flags are a best-effort advisory only. */
static int32_t sys_pipe2(int *fds, int flags) {
    (void)flags;   /* O_CLOEXEC / O_NONBLOCK: accepted, not yet enforced */
    if (!fds) return -BLUEY_EFAULT;
    int r = vfs_pipe(fds);
    return r < 0 ? r : 0;
}

/* faccessat2(dirfd, path, mode, flags) — check file accessibility.
 * We map this to a simple vfs_stat existence check; full permission
 * checking against uid/gid is a future enhancement.
 * Bash calls this to probe PATH entries during command lookup. */
static int32_t sys_faccessat2(int dirfd, const char *path, int mode, int flags) {
    (void)dirfd;   /* AT_FDCWD assumed; relative paths not yet supported */
    (void)flags;
    (void)mode;
    if (!path) return -BLUEY_EFAULT;
    vfs_stat_t st;
    /* F_OK (0) just checks existence; X_OK (1) / R_OK (4) / W_OK (2):
     * we return success whenever the path exists in the VFS. */
    if (vfs_stat(path, &st) != 0) return -BLUEY_ENOENT;
    return 0;
}

/* Minimal fcntl: F_DUPFD, F_GETFD, F_SETFD, F_GETFL, F_SETFL */
#define FCNTL_F_DUPFD   0
#define FCNTL_F_GETFD   1
#define FCNTL_F_SETFD   2
#define FCNTL_F_GETFL   3
#define FCNTL_F_SETFL   4

static int32_t sys_fcntl(int fd, int cmd, int arg) {
    switch (cmd) {
        case FCNTL_F_DUPFD: {
            /* Duplicate to the lowest *free* fd >= arg */
            if (arg < 0) return -BLUEY_EINVAL;
            int r = vfs_dup_above(fd, arg);
            if (r < 0) {
                /* Preserve EBADF for invalid input fds; map other failures to EMFILE */
                if (r == -BLUEY_EBADF) {
                    return r;
                }
                return -BLUEY_EMFILE;
            }
            return r;
        }
        case FCNTL_F_GETFD:
            /* Return close-on-exec flag; we don't track it, so return 0 */
            return 0;
        case FCNTL_F_SETFD:
            /* Set close-on-exec flag; accept but ignore */
            return 0;
        case FCNTL_F_GETFL:
            /* Return O_RDONLY placeholder */
            return 0;
        case FCNTL_F_SETFL:
            /* Accept flag updates (O_NONBLOCK etc.) but ignore for now */
            (void)arg;
            return 0;
        default:
            return -BLUEY_EINVAL;
    }
}

/* Minimal ioctl: support terminal size and basic termios queries */
#define IOCTL_TIOCGWINSZ 0x5413
#define IOCTL_TCGETS     0x5401
#define IOCTL_TCSETSW    0x5403
#define IOCTL_TCSETSF    0x5404
#define IOCTL_TCSETS     0x5402
#define IOCTL_TIOCGPGRP  0x540F
#define IOCTL_TIOCSPGRP  0x5410

typedef struct { uint16_t ws_row; uint16_t ws_col;
                 uint16_t ws_xpixel; uint16_t ws_ypixel; } k_winsize_t;

static int32_t sys_ioctl(int fd, uint32_t request, void *arg) {
    if (fd == 0 || fd == 1 || fd == 2 || vfs_fd_is_tty(fd)) {
        if ((request == IOCTL_TIOCGWINSZ || request == IOCTL_TCGETS ||
             request == IOCTL_TCSETS || request == IOCTL_TCSETSW ||
             request == IOCTL_TCSETSF || request == IOCTL_TIOCGPGRP ||
             request == IOCTL_TIOCSPGRP) && !arg) {
            return -BLUEY_EFAULT;
        }
        return tty_ioctl(request, arg) == 0 ? 0 : -BLUEY_EINVAL;
    }

    switch (request) {
        case IOCTL_TIOCGWINSZ: {
            if (!arg) return -BLUEY_EFAULT;
            k_winsize_t *ws = (k_winsize_t *)arg;
            ws->ws_row    = 25;
            ws->ws_col    = 80;
            ws->ws_xpixel = 0;
            ws->ws_ypixel = 0;
            return 0;
        }
        case IOCTL_TCGETS:
            /* Return a zeroed termios struct (minimal; callers tolerate this) */
            if (arg) memset(arg, 0, 60); /* sizeof(struct termios) ~ 60 bytes */
            return 0;
        case IOCTL_TCSETS:
        case IOCTL_TCSETSW:
        case IOCTL_TCSETSF:
            /* Accept but ignore termios updates */
            return 0;
        case IOCTL_TIOCGPGRP: {
            if (!arg) return -BLUEY_EFAULT;
            process_t *p = process_current();
            *(uint32_t *)arg = p ? p->pgid : 0;
            return 0;
        }
        case IOCTL_TIOCSPGRP:
            /* Accept but ignore setting foreground process group */
            return 0;
        default:
            return -BLUEY_EINVAL;
    }
}

/* ---- chdir / getcwd ----------------------------------------------------- */

/* Resolve a user-supplied path against the current working directory and
 * normalize '.', '..' and redundant slashes. The result is written into
 * out, which is guaranteed to be NUL-terminated on success. */
static int32_t resolve_normalize_path(const char *path, char *out, size_t out_size) {
    if (!path || !out || out_size == 0) return -BLUEY_EFAULT;

    const char *cwd = NULL;
    char temp[512];

    if (path[0] == '/') {
        /* Already absolute: copy directly into temp for normalization. */
        size_t len = strlen(path);
        if (len + 1 > sizeof(temp)) return -BLUEY_ERANGE;
        memcpy(temp, path, len + 1);
    } else {
        /* Relative: prepend current working directory. */
        cwd = process_get_cwd();
        if (!cwd || cwd[0] == '\0') {
            cwd = "/";
        }
        size_t cwd_len  = strlen(cwd);
        size_t path_len = strlen(path);
        int need_slash  = (cwd_len > 1 && cwd[cwd_len - 1] != '/');
        size_t total    = cwd_len + (need_slash ? 1u : 0u) + path_len + 1u;
        if (total > sizeof(temp)) return -BLUEY_ERANGE;

        size_t pos = 0;
        memcpy(temp + pos, cwd, cwd_len);
        pos += cwd_len;
        if (need_slash) {
            temp[pos++] = '/';
        }
        memcpy(temp + pos, path, path_len);
        pos += path_len;
        temp[pos] = '\0';
    }

    /* Normalize temp into out using a simple component stack algorithm. */
    size_t out_pos = 0;

    /* Ensure leading slash. */
    if (temp[0] != '/') {
        if (out_size < 2) return -BLUEY_ERANGE;
        out[out_pos++] = '/';
    }

    const char *p = temp;
    while (*p == '/') p++; /* skip leading slashes */

    while (*p != '\0') {
        /* Extract next component. */
        const char *start = p;
        while (*p != '/' && *p != '\0') p++;
        size_t comp_len = (size_t)(p - start);

        if (comp_len == 0 || (comp_len == 1 && start[0] == '.')) {
            /* Skip empty or '.' components. */
        } else if (comp_len == 2 && start[0] == '.' && start[1] == '.') {
            /* Handle '..': remove previous component if possible. */
            if (out_pos > 1) {
                /* Remove trailing slash first if present (but keep root). */
                if (out[out_pos - 1] == '/' && out_pos > 1) {
                    out_pos--;
                }
                /* Walk back to previous slash (or root). */
                while (out_pos > 1 && out[out_pos - 1] != '/') {
                    out_pos--;
                }
            }
        } else {
            /* Append separator if needed (avoid duplicate leading '/'). */
            if (out_pos == 0) {
                if (out_size < 2) return -BLUEY_ERANGE;
                out[out_pos++] = '/';
            } else if (out[out_pos - 1] != '/') {
                if (out_pos + 1 >= out_size) return -BLUEY_ERANGE;
                out[out_pos++] = '/';
            }
            /* Append component. */
            if (out_pos + comp_len + 1 >= out_size) return -BLUEY_ERANGE;
            memcpy(out + out_pos, start, comp_len);
            out_pos += comp_len;
        }

        /* Skip consecutive slashes. */
        while (*p == '/') p++;
    }

    /* Ensure we have at least "/" */
    if (out_pos == 0) {
        if (out_size < 2) return -BLUEY_ERANGE;
        out[out_pos++] = '/';
    }

    out[out_pos] = '\0';
    return 0;
}

static int32_t sys_chdir(const char *path) {
    if (!path) return -BLUEY_EFAULT;

    char abs_path[512];
    int32_t rc = resolve_normalize_path(path, abs_path, sizeof(abs_path));
    if (rc != 0) return rc;

    vfs_stat_t st;
    if (vfs_stat(abs_path, &st) != 0) return -BLUEY_ENOENT;
    if (!st.is_dir) return -BLUEY_ENOTDIR;
    process_set_cwd(abs_path);
    return 0;
}

static int32_t sys_fchdir(int fd) {
    vfs_stat_t st;
    const char *path;

    if (fd < 0) return -BLUEY_EBADF;
    if (vfs_fstat(fd, &st) != 0) return -BLUEY_EBADF;
    if (!st.is_dir) return -BLUEY_ENOTDIR;

    path = vfs_fd_get_path(fd);
    if (!path || path[0] == '\0') return -BLUEY_EBADF;

    process_set_cwd(path);
    return 0;
}

static int32_t sys_getcwd(char *buf, size_t size) {
    const char *cwd = process_get_cwd();
    size_t len = strlen(cwd);

    /* Linux getcwd error priority: EFAULT (bad pointer) before EINVAL (bad
     * size), so check buf first.  buf==NULL is always EFAULT regardless of
     * size.  size==0 with a valid buf is EINVAL (Linux behaviour).
     * When both are violated (buf==NULL, size==0) we return EFAULT. */
    if (!buf) return -BLUEY_EFAULT;
    if (size == 0) return -BLUEY_EINVAL;
    if (len + 1 > size) return -BLUEY_ERANGE;
    strncpy(buf, cwd, size - 1);
    buf[size - 1] = '\0';
    return (int32_t)(len + 1);
}

/* ---- getdents ----------------------------------------------------------- */

/* Linux-compatible dirent structure (32-bit) */
typedef struct {
    uint32_t d_ino;
    uint32_t d_off;
    uint16_t d_reclen;
    char     d_name[1]; /* variable length */
} k_dirent_t;

static int32_t sys_getdents(int fd, void *buf, uint32_t count) {
    if (!buf) return -BLUEY_EFAULT;
    if (fd < 0) return -BLUEY_EBADF;

    /* Verify fd refers to a directory */
    vfs_stat_t fst;
    if (vfs_fstat(fd, &fst) != 0) return -BLUEY_EBADF;
    if (!fst.is_dir) return -BLUEY_ENOTDIR;

    /* Use the path stored for the fd if available; fall back to cwd */
    const char *fdpath = vfs_fd_get_path(fd);
    const char *dirpath = fdpath ? fdpath : process_get_cwd();
    vfs_dirent_t entries[32];
    int nent = vfs_readdir(dirpath, entries, 32);
    if (nent < 0) return -BLUEY_EINVAL;

    uint8_t *out = (uint8_t *)buf;
    uint32_t written = 0;

    for (int i = 0; i < nent; i++) {
        size_t namelen = strlen(entries[i].name);
        /* dirent record: d_ino(4) + d_off(4) + d_reclen(2) + name + NUL, aligned to 4 */
        uint16_t reclen = (uint16_t)((sizeof(uint32_t) + sizeof(uint32_t) +
                                      sizeof(uint16_t) + namelen + 1 + 3) & ~3u);
        if (written + reclen > count) break;

        k_dirent_t *de = (k_dirent_t *)out;
        de->d_ino    = entries[i].inode ? entries[i].inode : (uint32_t)(i + 1);
        de->d_off    = written + reclen;
        de->d_reclen = reclen;
        memcpy(de->d_name, entries[i].name, namelen + 1);

        out     += reclen;
        written += reclen;
    }

    return (int32_t)written;
}

/* ---- getppid ------------------------------------------------------------ */

static int32_t sys_getppid(void) {
    return (int32_t)process_getppid();
}

/* ---- wait4 -------------------------------------------------------------- */

static int32_t sys_wait4(int32_t pid, int *status, int options, void *rusage) {
    (void)rusage; /* rusage not implemented */
    return process_waitpid(pid, status, options);
}

/* ---- sched_yield -------------------------------------------------------- */

static int32_t sys_sched_yield(void) {
    scheduler_yield();
    return 0;
}

/* ---- nanosleep ---------------------------------------------------------- */

typedef struct { uint32_t tv_sec; uint32_t tv_nsec; } k_timespec_req_t;

static int32_t sys_nanosleep(const k_timespec_req_t *req, void *rem) {
    (void)rem;
    if (!req) return -BLUEY_EFAULT;
    /* Guard against overflow: cap seconds at floor(UINT32_MAX / 1000) */
    uint32_t ms;
    if (req->tv_sec > 4294967u) {
        ms = 0xFFFFFFFFu;
    } else {
        ms = req->tv_sec * 1000u + req->tv_nsec / 1000000u;
    }
    if (ms) process_sleep(ms);
    return 0;
}

/* ---- exit_group --------------------------------------------------------- */

static int32_t sys_exit_group(int code) {
    process_exit(code);
    return 0;
}

/* ---- set_tid_address ---------------------------------------------------- */

static int32_t sys_set_tid_address(void *tidptr) {
    (void)tidptr; /* single-threaded: no thread pointer tracking needed */
    return (int32_t)process_getpid();
}

/* ---- set_robust_list --------------------------------------------------- */

static int32_t sys_set_robust_list(void *head, size_t len) {
    process_t *process = process_current();

    if (!process) return -BLUEY_EINVAL;
    /* Linux i386 userspace passes sizeof(struct robust_list_head). We do not
     * implement futex cleanup yet, but storing the registration makes musl and
     * other modern runtimes happy and gives us the right ABI shape. */
    process->robust_list_head = (uint32_t)(uintptr_t)head;
    process->robust_list_len = (uint32_t)len;
    return 0;
}

/* ---- getrandom ---------------------------------------------------------- */

static int32_t sys_getrandom(void *buf, size_t buflen, uint32_t flags) {
    (void)buf;
    (void)buflen;
    (void)flags;
    /* getrandom(2) is expected to provide cryptographically secure randomness.
     * Until a real entropy source and secure PRNG are available, report the
     * syscall as not implemented rather than returning weak, predictable data. */
    return -BLUEY_ENOSYS;
}

/* ---- rseq --------------------------------------------------------------- */

static int32_t sys_rseq(void *rseq, uint32_t rseq_len, int flags, uint32_t sig) {
    process_t *process = process_current();

    if (!process) return -BLUEY_EINVAL;
    /* Real rseq support needs scheduler/preemption integration so the kernel
     * can keep cpu_id fields coherent and trigger abort handlers on migration.
     * For now, record the registration for debugging/forward compatibility and
     * report ENOSYS so libc falls back to non-rseq code paths. */
    process->rseq_area = (uint32_t)(uintptr_t)rseq;
    process->rseq_len = rseq_len;
    process->rseq_sig = sig;
    (void)flags;
    return -BLUEY_ENOSYS;
}

void syscall_init(void) {
    // Register int 0x80 as a gate accessible from ring 3 (DPL=3 = 0x60|0x8E = 0xEE)
    idt_set_gate(0x80, (uint32_t)syscall_stub, 0x08, 0xEE);
    kprintf("%s\n", MSG_SYSCALL_INIT);
}

// SYS_WRITE (1): fd=1/2 -> stdout/stderr -> TTY; fd>=3 -> VFS
static int32_t sys_write(uint32_t fd, const char *buf, size_t len) {
    if (fd == 1 || fd == 2) {
        if (!buf) return -BLUEY_EFAULT;
        if (len == 0) return 0;
        // Bounds check: refuse oversized writes to avoid flooding
        if (len > 4096) len = 4096;
        tty_write(buf, len);
        tty_flush();
        return (int32_t)len;
    }
    if (fd < VFS_MAX_OPEN && vfs_fd_is_tty((int)fd)) {
        if (!buf) return -BLUEY_EFAULT;
        if (len == 0) return 0;
        if (len > 4096) len = 4096;
        return vfs_write((int)fd, (const uint8_t *)buf, len);
    }
    if (fd >= 3) {
        if (!buf) return -BLUEY_EFAULT;
        if (len == 0) return 0;
        int r = vfs_write((int)fd, (const uint8_t *)buf, len);
        return r;
    }
    return -BLUEY_EBADF;
}

static int32_t sys_open(const char *path, int flags) {
    vfs_stat_t stat;
    int access_mode;
    int vfs_flags;
    int fd;

    if (!path) return -BLUEY_EFAULT;

    access_mode = flags & 0x3;
    vfs_flags = access_mode | (flags & (VFS_O_CREAT | VFS_O_TRUNC | VFS_O_APPEND));

    if (vfs_stat(path, &stat) != 0) {
        if (!(vfs_flags & VFS_O_CREAT)) return -BLUEY_ENOENT;
    }

    fd = vfs_open(path, vfs_flags);
    if (fd >= 0) return fd;

    if (vfs_stat(path, &stat) != 0) return -BLUEY_ENOENT;
    if (stat.is_dir) return -BLUEY_EISDIR;
    return -BLUEY_EPERM;
}

static int32_t sys_close(int fd) {
    return vfs_close(fd);
}

static int32_t sys_kill(int32_t pid, int sig) {
    process_t *current;

    if (pid > 0) return signal_send_pid((uint32_t)pid, sig);
    if (pid == 0) {
        /* Send to own process group */
        current = process_current();
        if (!current) return -BLUEY_EPERM;
        return signal_send_pgrp(current->pgid, sig);
    }
    /* pid < 0: send to process group -pid */
    return signal_send_pgrp((uint32_t)(-pid), sig);
}

static int32_t sys_execve(registers_t *regs,
                          const char *path,
                          const char *const *argv,
                          const char *const *envp) {
    process_t *process = process_current();
    elf_image_t image;
    vfs_stat_t stat;
    char *path_copy = NULL;
    char **argv_copy = NULL;
    char **envp_copy = NULL;
    uint32_t old_page_dir;
    int32_t result = -1;

    if (!process) return -BLUEY_EPERM;
    if (!path) return -BLUEY_EFAULT;
    if (!(process->flags & PROC_FLAG_USER_MODE)) return -BLUEY_EPERM;

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

    if (vfs_stat(path_copy, &stat) != 0) {
        kprintf("[SYS] execve missing path: %s\n", path_copy);
        result = -BLUEY_ENOENT;
        goto cleanup;
    }
    // Execute permission is still required even for setuid/setgid binaries.
    if (vfs_access(path_copy, VFS_ACCESS_EXEC) != 0 || stat.is_dir) {
        kprintf("[SYS] execve denied path=%s is_dir=%u mode=0%o\n",
                path_copy, stat.is_dir, stat.mode);
        result = -BLUEY_EPERM;
        goto cleanup;
    }

    if (elf_load_image(path_copy, (const char *const *)argv_copy,
                       (const char *const *)envp_copy, &image) != 0) {
        kprintf("[SYS] execve load failed path=%s\n", path_copy);
        result = -1;
        goto cleanup;
    }

    old_page_dir = process->page_dir;
    /* Remember whether this process was using a vfork-shared VM. If so,
     * do not destroy the old_page_dir after exec: it may still be the
     * parent's address space. See: vfork semantics where parent and
     * child share the same page_dir until the child execs or exits. */
    int had_vfork_shared_vm = (process->flags & PROC_FLAG_VFORK_SHARED_VM) != 0;
    process_exec_replace(process, image.name, image.entry, image.stack_pointer,
                         image.stack_base, image.stack_top, image.page_dir);
    process_set_memory_layout(process, image.image_end);
    paging_switch_directory(image.page_dir);
    process_set_current(process);
    syscall_prepare_user_return(regs, process);
    if (stat.mode & VFS_S_ISUID || stat.mode & VFS_S_ISGID) {
        uint32_t new_euid = process->euid;
        uint32_t new_egid = process->egid;
        if (stat.mode & VFS_S_ISUID) new_euid = stat.uid;
        if (stat.mode & VFS_S_ISGID) new_egid = stat.gid;
        process_set_effective_ids(process, new_euid, new_egid);
    }
    result = 0;

    if (old_page_dir && old_page_dir != image.page_dir) {
        if (!had_vfork_shared_vm) {
            paging_destroy_address_space(old_page_dir);
        } else {
            kprintf("[SYS] execve: preserving shared vfork address space 0x%08x\n", old_page_dir);
        }
    }

cleanup:
    if (path_copy) kheap_free(path_copy);
    if (argv_copy) syscall_free_string_vector(argv_copy);
    if (envp_copy) syscall_free_string_vector(envp_copy);
    /* If execve failed before process_exec_replace() ran, the vfork parent is
     * still blocked in PROC_WAITING.  Release it now so it is not stuck
     * indefinitely while the child calls _exit(127). */
    if (result != 0 && process && (process->flags & PROC_FLAG_VFORK_SHARED_VM)) {
        process_vfork_execve_failed(process);
    }
    return result;
}

// SYS_READ (0): fd=0 -> stdin -> TTY; fd>=3 -> VFS
static int32_t sys_read(uint32_t fd, char *buf, size_t len) {
    if (fd == 0) {
        if (!buf) return -BLUEY_EFAULT;
        if (len == 0) return 0;
        return tty_read(buf, len);
    }
    if (fd < VFS_MAX_OPEN && vfs_fd_is_tty((int)fd)) {
        if (!buf) return -BLUEY_EFAULT;
        if (len == 0) return 0;
        return vfs_read((int)fd, (uint8_t *)buf, len);
    }
    if (fd >= 3) {
        if (!buf) return -BLUEY_EFAULT;
        if (len == 0) return 0;
        int r = vfs_read((int)fd, (uint8_t *)buf, len);
        return r;
    }
    return -BLUEY_EBADF;
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

/* ---- Process groups ---------------------------------------------------- */

static int32_t sys_setpgid(uint32_t pid, uint32_t pgid) {
    return process_setpgid(pid, pgid);
}

static int32_t sys_getpgid(uint32_t pid) {
    uint32_t pgid = process_getpgid(pid);
    return pgid ? (int32_t)pgid : -BLUEY_EPERM;
}

static int32_t sys_getpgrp(void) {
    process_t *p = process_current();
    return p ? (int32_t)p->pgid : -BLUEY_EPERM;
}

/* ---- Mount / umount ---------------------------------------------------- */

static int32_t sys_mount(const char *source, const char *target,
                         const char *fstype, uint32_t flags,
                         const void *data) {
    (void)source; (void)flags; (void)data;
    if (!target || !fstype) return -BLUEY_EFAULT;
    return vfs_mount(target, fstype, 0);
}

static int32_t sys_umount2(const char *target, uint32_t flags) {
    (void)flags;
    if (!target) return -BLUEY_EFAULT;
    return vfs_umount(target);
}

/* ---- Poll -------------------------------------------------------------- */

static int32_t sys_poll(pollfd_t *fds, uint32_t nfds, int32_t timeout_ms) {
    if (!fds) return -BLUEY_EFAULT;
    return kernel_poll(fds, nfds, timeout_ms);
}

/* ---- Device event channel --------------------------------------------- */

static int32_t sys_devev_open(void) {
    return vfs_devev_open();
}

/* ---- Reboot / poweroff ------------------------------------------------- */

/*
 * sys_reboot - reboot or power off the machine.
 *
 * To avoid accidents the caller must supply two magic numbers:
 *   magic1 = 0xfee1dead  ("feel dead" — you're about to shut down)
 *   magic2 = 0x28121969  (28 Dec 1969 — the day before Unix epoch; a nod to
 *                         the Linux reboot(2) API which uses the same values)
 * cmd selects the action:
 *   REBOOT_CMD_RESTART   (0x01234567) = reset the CPU
 *   REBOOT_CMD_POWER_OFF (0x4321fedc) = power off (default)
 */
#define REBOOT_MAGIC1        0xfee1deadU
#define REBOOT_MAGIC2        0x28121969U
#define REBOOT_CMD_RESTART   0x01234567U
#define REBOOT_CMD_POWER_OFF 0x4321fedcU

static int32_t sys_reboot(uint32_t magic1, uint32_t magic2, uint32_t cmd) {
    if (magic1 != REBOOT_MAGIC1 || magic2 != REBOOT_MAGIC2) return -BLUEY_EINVAL;

    kprintf("[SYS]  Reboot/poweroff requested (cmd=0x%x) - bye bye!\n", cmd);
    __asm__ volatile("cli");

    if (cmd == REBOOT_CMD_RESTART) {
        /* Keyboard controller CPU reset (works on real PC and QEMU) */
        uint8_t val;
        do { val = inb(0x64); } while (val & 0x02u);
        outb(0x64, 0xFE);
        /* Fallback: port 0xCF9 hard reset */
        outb(0xCF9, 0x06u);
    } else {
        /* ACPI S5 poweroff — tried in order of most-to-least likely */
        outw(0x604, 0x2000);
        outw(0xB004, 0x2000);  /* older QEMU / Bochs */
        outw(0x4004, 0x3400);  /* SeaBIOS */
    }

    /* Should never reach here */
    for (;;) __asm__ volatile("hlt");
    return 0;
}

// Module loading syscalls
#define MODULE_IMAGE_MAX (1024 * 1024)  // 1 MB max module image size

static int32_t sys_init_module(const void *module_image, uint32_t len, const char *param_values) {
    (void)param_values; // Parameters not yet implemented

    if (!module_image || len == 0) return -BLUEY_EINVAL;

    // Enforce sane maximum module image size
    if (len > MODULE_IMAGE_MAX) return -BLUEY_E2BIG;

    // For security, only allow root to load modules
    if (multiuser_current_uid() != 0) return -BLUEY_EPERM;

    // Allocate temporary buffer to copy from userspace
    uint8_t *buffer = (uint8_t*)kheap_alloc(len, 0);
    if (!buffer) return -BLUEY_ENOMEM;

    memcpy(buffer, module_image, len);

    kprintf("[SYS]  Loading module from memory (len=%u)\n", len);

    // Use a placeholder name; module_load_from_memory will update it from module_info
    int result = module_load_from_memory(NULL, buffer, len);

    kheap_free(buffer);

    if (result < 0) return -BLUEY_EINVAL;
    return 0;
}

static int32_t sys_delete_module(const char *name, uint32_t flags) {
    (void)flags; // Flags not yet implemented

    if (!name) return -BLUEY_EINVAL;

    // For security, only allow root to unload modules
    if (multiuser_current_uid() != 0) return -BLUEY_EPERM;

    int result = module_unload(name);
    if (result < 0) return -BLUEY_ENOENT;
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
        case SYS_STAT:
            ret = sys_stat((const char*)regs->ebx, (void*)regs->ecx);
            break;
        case SYS_LSTAT:
            ret = sys_lstat((const char*)regs->ebx, (void*)regs->ecx);
            break;
        case SYS_LSEEK:
            ret = sys_lseek((int)regs->ebx, (int32_t)regs->ecx, (int)regs->edx);
            break;
        case SYS_UNLINK:
            ret = sys_unlink((const char*)regs->ebx);
            break;
        case SYS_MKDIR:
            ret = sys_mkdir((const char*)regs->ebx, regs->ecx);
            break;
        case SYS_RMDIR:
            ret = sys_rmdir((const char*)regs->ebx);
            break;
        case SYS_LINK:
            ret = sys_link((const char*)regs->ebx, (const char*)regs->ecx);
            break;
        case SYS_SOCKETCALL:
            ret = sys_socketcall((int)regs->ebx, (uint32_t*)(uintptr_t)regs->ecx);
            break;
        case SYS_RENAME:
            ret = sys_rename((const char*)regs->ebx, (const char*)regs->ecx);
            break;
        case SYS_SYMLINK:
            ret = sys_symlink((const char*)regs->ebx, (const char*)regs->ecx);
            break;
        case SYS_READLINK:
            ret = sys_readlink((const char*)regs->ebx, (char*)regs->ecx, regs->edx);
            break;
        case SYS_CHMOD:
            ret = sys_chmod((const char*)regs->ebx, regs->ecx);
            break;
        case SYS_FCHMOD:
            ret = sys_fchmod((int)regs->ebx, regs->ecx);
            break;
        case SYS_CHOWN:
        case SYS_CHOWN32:
            ret = sys_chown((const char*)regs->ebx, regs->ecx, regs->edx);
            break;
        case SYS_LCHOWN:
        case SYS_LCHOWN32:
            ret = sys_lchown((const char*)regs->ebx, regs->ecx, regs->edx);
            break;
        case SYS_FCHOWN:
        case SYS_FCHOWN32:
            ret = sys_fchown((int)regs->ebx, regs->ecx, regs->edx);
            break;
        case SYS_DUP:
            ret = sys_dup((int)regs->ebx);
            break;
        case SYS_DUP2:
            ret = sys_dup2_impl((int)regs->ebx, (int)regs->ecx);
            break;
        case SYS_PIPE:
            ret = sys_pipe_impl((int*)regs->ebx);
            break;
        case SYS_FCNTL:
        case SYS_FCNTL64:
            ret = sys_fcntl((int)regs->ebx, (int)regs->ecx, (int)regs->edx);
            break;
        case SYS_IOCTL:
            ret = sys_ioctl((int)regs->ebx, regs->ecx, (void*)regs->edx);
            break;
        case SYS_CHDIR:
            ret = sys_chdir((const char*)regs->ebx);
            break;
        case SYS_GETCWD:
            ret = sys_getcwd((char*)regs->ebx, (size_t)regs->ecx);
            break;
        case SYS_ACCESS:
            ret = sys_access((const char*)regs->ebx, (int)regs->ecx);
            break;
        case SYS_GETDENTS:
            ret = sys_getdents((int)regs->ebx, (void*)regs->ecx, regs->edx);
            break;
        case SYS_KILL:
            ret = sys_kill((int32_t)regs->ebx, (int)regs->ecx);
            break;
        case SYS_EXIT:
            process_exit((int)regs->ebx);
            ret = 0;
            break;
        case SYS_EXIT_GROUP:
            ret = sys_exit_group((int)regs->ebx);
            break;
        case SYS_GETPID:
            ret = (int32_t)process_getpid();
            break;
        case SYS_GETPPID:
            ret = sys_getppid();
            break;
        case SYS_GETUID:
            ret = (int32_t)process_get_uid();
            break;
        case SYS_GETUID32:
            ret = (int32_t)process_get_uid();
            break;
        case SYS_GETGID:
            ret = (int32_t)process_get_gid();
            break;
        case SYS_GETGID32:
            ret = (int32_t)process_get_gid();
            break;
        case SYS_GETEUID32:
            ret = (int32_t)process_get_euid();
            break;
        case SYS_GETEGID32:
            ret = (int32_t)process_get_egid();
            break;
        case SYS_UNAME:
            ret = sys_uname((utsname_t*)regs->ebx);
            break;
        case SYS_GETHOSTNAME:
            ret = sys_gethostname((char*)regs->ebx, (size_t)regs->ecx);
            break;
        case SYS_GETTIMEOFDAY:
            ret = sys_gettimeofday((k_timeval_t*)regs->ebx, (void*)regs->ecx);
            break;
        case SYS_SETTIMEOFDAY:
            ret = sys_settimeofday((const k_timeval_t*)regs->ebx, (void*)regs->ecx);
            break;
        case SYS_CLOCK_GETTIME:
            ret = sys_clock_gettime((int)regs->ebx, (k_timespec_t*)regs->ecx);
            break;
        case SYS_NANOSLEEP:
            ret = sys_nanosleep((const k_timespec_req_t*)regs->ebx, (void*)regs->ecx);
            break;
        case SYS_EXECVE:
            ret = sys_execve(regs,
                             (const char*)regs->ebx,
                             (const char *const*)regs->ecx,
                             (const char *const*)regs->edx);
            break;
        case SYS_SETRLIMIT:
            ret = sys_setrlimit((uint32_t)regs->ebx, (uint32_t)regs->ecx);
            break;
        case SYS_GETRLIMIT:
        case SYS_UGETRLIMIT:
            ret = sys_getrlimit((uint32_t)regs->ebx, (uint32_t)regs->ecx);
            break;
        case SYS_PRLIMIT64:
            ret = sys_prlimit64((uint32_t)regs->ebx,
                                (uint32_t)regs->ecx,
                                (uint32_t)regs->edx,
                                (uint32_t)regs->esi);
            break;
        case SYS_WAITPID:
            ret = process_waitpid((int32_t)regs->ebx, (int*)regs->ecx, (int)regs->edx);
            break;
        case SYS_WAIT4:
            ret = sys_wait4((int32_t)regs->ebx, (int*)regs->ecx,
                            (int)regs->edx, (void*)regs->esi);
            break;
        case SYS_FORK:
            ret = sys_fork(regs);
            break;
        case SYS_VFORK:
            ret = sys_vfork(regs);
            break;
        case SYS_CLONE:
            ret = sys_clone(regs);
            break;
        case SYS_SCHED_YIELD:
            ret = sys_sched_yield();
            break;
        case SYS_BRK:
            ret = sys_brk(regs->ebx);
            break;
        case SYS_MMAP:
        case SYS_MMAP2:
            ret = sys_mmap(regs);
            break;
        case SYS_MUNMAP:
            ret = sys_munmap(regs);
            break;
        case SYS_MPROTECT:
            ret = sys_mprotect(regs);
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
        case SYS_FSTAT:
            ret = sys_fstat((int)regs->ebx, (void*)regs->ecx);
            break;
        case SYS_FSTAT64:
            ret = sys_fstat64((int)regs->ebx, (k_stat64_t*)regs->ecx);
            break;
        case SYS__llseek:
            ret = sys__llseek((uint32_t)regs->ebx,
                              (uint32_t)regs->ecx,
                              (uint32_t)regs->edx,
                              (uint32_t)regs->esi,
                              (int)regs->edi);
            break;
        case SYS_WRITEV:
            ret = sys_writev((int)regs->ebx, (const k_iovec_t*)regs->ecx, regs->edx);
            break;
        case SYS_FCHDIR:
            ret = sys_fchdir((int)regs->ebx);
            break;
        case SYS_SET_TID_ADDRESS:
            ret = sys_set_tid_address((void*)regs->ebx);
            break;
        case SYS_SET_ROBUST_LIST:
            ret = sys_set_robust_list((void*)regs->ebx, (size_t)regs->ecx);
            break;
        case SYS_GETRANDOM:
            ret = sys_getrandom((void*)regs->ebx, (size_t)regs->ecx, regs->edx);
            break;
        case SYS_CLOCK_GETTIME64:
            ret = sys_clock_gettime64((int)regs->ebx, (k_timespec64_t*)regs->ecx);
            break;
        case SYS_CLOCK_SETTIME64:
            ret = sys_clock_settime64((int)regs->ebx, (const k_timespec64_t*)regs->ecx);
            break;
        case SYS_STATX:
            ret = sys_statx((int)regs->ebx, (const char*)regs->ecx, (int)regs->edx,
                            regs->esi, (k_statx_t*)regs->edi);
            break;
        case SYS_RSEQ:
            ret = sys_rseq((void*)regs->ebx, regs->ecx, (int)regs->edx, regs->esi);
            break;
        /* ---- Process groups -------------------------------------------- */
        case SYS_SETPGID:
            ret = sys_setpgid(regs->ebx, regs->ecx);
            break;
        case SYS_GETPGID:
            ret = sys_getpgid(regs->ebx);
            break;
        case SYS_GETPGRP:
            ret = sys_getpgrp();
            break;
        /* ---- Mount / umount -------------------------------------------- */
        case SYS_MOUNT:
            ret = sys_mount((const char*)regs->ebx, (const char*)regs->ecx,
                            (const char*)regs->edx, 0, NULL);
            break;
        case SYS_UMOUNT2:
            ret = sys_umount2((const char*)regs->ebx, regs->ecx);
            break;
        /* ---- Poll ------------------------------------------------------- */
        case SYS_POLL:
            ret = sys_poll((pollfd_t*)regs->ebx, regs->ecx, (int32_t)regs->edx);
            break;
        case SYS_SELECT:
            ret = sys_select((int)regs->ebx,
                             (void*)regs->ecx,
                             (void*)regs->edx,
                             (void*)regs->esi,
                             (const k_timeval_t*)regs->edi);
            break;
        case SYS_PSELECT6:
            ret = sys_pselect6((int)regs->ebx,
                               (void*)regs->ecx,
                               (void*)regs->edx,
                               (void*)regs->esi,
                               (const compat_timespec32_t*)regs->edi,
                               (const compat_pselect6_data_t*)regs->ebp);
            break;
        case SYS_SOCKET:
            ret = sys_socket_open((int)regs->ebx, (int)regs->ecx, (int)regs->edx);
            break;
        case SYS_BIND:
            ret = sys_socket_bind((int)regs->ebx, (const void*)regs->ecx, regs->edx);
            break;
        case SYS_CONNECT:
            ret = sys_socket_connect((int)regs->ebx, (const void*)regs->ecx, regs->edx);
            break;
        case SYS_LISTEN:
            ret = sys_socket_listen((int)regs->ebx, (int)regs->ecx);
            break;
        case SYS_ACCEPT4:
            ret = sys_socket_accept4((int)regs->ebx, (void*)regs->ecx,
                                     (uint32_t*)regs->edx, (int)regs->esi);
            break;
        case SYS_SENDMSG:
            ret = sys_sendmsg((int)regs->ebx,
                              (const struct bluey_msghdr*)(uintptr_t)regs->ecx,
                              (int)regs->edx);
            break;
        /* Compatibility fallbacks for alternate musl i386 syscall numbering
         * (some musl builds use 369-372 for sendto/sendmsg/recvfrom/recvmsg).
         */
        case 369: /* sendto -> treat as sendmsg */
            ret = sys_sendmsg((int)regs->ebx,
                              (const struct bluey_msghdr*)(uintptr_t)regs->ecx,
                              (int)regs->edx);
            break;
        case 370: /* sendmsg */
            ret = sys_sendmsg((int)regs->ebx,
                              (const struct bluey_msghdr*)(uintptr_t)regs->ecx,
                              (int)regs->edx);
            break;
        
        case SYS_RECVMSG:
            ret = sys_recvmsg((int)regs->ebx,
                              (struct bluey_msghdr*)(uintptr_t)regs->ecx,
                              (int)regs->edx);
            break;
        case 371: /* recvfrom -> treat as recvmsg */
            ret = sys_recvmsg((int)regs->ebx,
                              (struct bluey_msghdr*)(uintptr_t)regs->ecx,
                              (int)regs->edx);
            break;
        case 372: /* recvmsg */
            ret = sys_recvmsg((int)regs->ebx,
                              (struct bluey_msghdr*)(uintptr_t)regs->ecx,
                              (int)regs->edx);
            break;
        /* ---- Device event channel --------------------------------------- */
        case SYS_DEVEV_OPEN:
            ret = sys_devev_open();
            break;
        case SYS_SETREUID32:
            ret = sys_setreuid32((uint32_t)regs->ebx, (uint32_t)regs->ecx);
            break;
        case SYS_SETREGID32:
            ret = sys_setregid32((uint32_t)regs->ebx, (uint32_t)regs->ecx);
            break;
        case SYS_GETGROUPS32:
            ret = sys_getgroups32((int)regs->ebx, (uint32_t*)regs->ecx);
            break;
        case SYS_SETGROUPS32:
            ret = sys_setgroups32((size_t)regs->ebx, (const uint32_t*)regs->ecx);
            break;
        case SYS_SETRESUID32:
            ret = sys_setresuid32((uint32_t)regs->ebx,
                                  (uint32_t)regs->ecx,
                                  (uint32_t)regs->edx);
            break;
        case SYS_GETRESUID32:
            ret = sys_getresuid32((uint32_t*)regs->ebx,
                                  (uint32_t*)regs->ecx,
                                  (uint32_t*)regs->edx);
            break;
        case SYS_SETUID32:
            ret = sys_setuid32((uint32_t)regs->ebx);
            break;
        case SYS_SETRESGID32:
            ret = sys_setresgid32((uint32_t)regs->ebx,
                                  (uint32_t)regs->ecx,
                                  (uint32_t)regs->edx);
            break;
        case SYS_GETRESGID32:
            ret = sys_getresgid32((uint32_t*)regs->ebx,
                                  (uint32_t*)regs->ecx,
                                  (uint32_t*)regs->edx);
            break;
        case SYS_SETGID32:
            ret = sys_setgid32((uint32_t)regs->ebx);
            break;
        case SYS_SETFSUID32:
            ret = sys_setfsuid32((uint32_t)regs->ebx);
            break;
        case SYS_SETFSGID32:
            ret = sys_setfsgid32((uint32_t)regs->ebx);
            break;
        /* ---- Reboot / poweroff ----------------------------------------- */
        case SYS_REBOOT:
            ret = sys_reboot(regs->ebx, regs->ecx, regs->edx);
            break;
        /* ---- Thread-local storage --------------------------------------- */
        case SYS_SET_THREAD_AREA: {
            /* struct user_desc { u32 entry_number, base_addr, limit, flags; } */
            uint32_t *ud = (uint32_t *)(uintptr_t)regs->ebx;
            if (!ud) { ret = -BLUEY_EINVAL; break; }
            uint32_t base = ud[1];          /* base_addr is 2nd dword */
            process_t *cur = process_current();
            if (!cur) { ret = -BLUEY_EINVAL; break; }
            cur->tls_base = base;
            gdt_set_tls_base(base);
            /* Update GS in syscall frame and saved context so the TLS selector
             * survives scheduler context switches via the IRQ stub. */
            regs->gs = GDT_TLS_SEL;
            cur->saved_regs.gs = GDT_TLS_SEL;
            syscall_saved_gs = GDT_TLS_SEL;
            /* Write back the allocated entry number (6) so musl caches it */
            ud[0] = 6;
            ret = 0;
            break;
        }
        case SYS_MODIFY_LDT:
            /* Return success so musl falls through to the GS-load path. */
            ret = 0;
            break;
        case SYS_INIT_MODULE:
            ret = sys_init_module((const void*)regs->ebx, regs->ecx, (const char*)regs->edx);
            break;
        case SYS_DELETE_MODULE:
            ret = sys_delete_module((const char*)regs->ebx, regs->ecx);
            break;
        case SYS_PIPE2:
            ret = sys_pipe2((int*)regs->ebx, (int)regs->ecx);
            break;
        case SYS_FACCESSAT2:
            ret = sys_faccessat2((int)regs->ebx, (const char*)regs->ecx,
                                 (int)regs->edx, (int)regs->esi);
            break;
        default: {
            /* Unknown syscall - don't crash. Log caller info to help mapping. */
            process_t *caller = process_current();
            if (caller) {
                kprintf("[SYS] Unknown syscall %d from pid=%d name=%s eip=0x%x args=0x%x,0x%x,0x%x\n",
                        regs->eax, caller->pid, caller->name, regs->eip,
                        regs->ebx, regs->ecx, regs->edx);
            } else {
                kprintf("[SYS] Unknown syscall %d (no process) eip=0x%x args=0x%x,0x%x,0x%x\n",
                        regs->eax, regs->eip, regs->ebx, regs->ecx, regs->edx);
            }
            ret = -BLUEY_ENOSYS;
            break;
        }
    }
    regs->eax = ret;
    scheduler_handle_trap(regs, 0);
    return ret;
}
