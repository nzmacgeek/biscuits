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
#include "poll.h"
#include "devev.h"
#include "../fs/vfs.h"

extern void syscall_stub(void);

/* Per-entry saved user segment registers. Interrupts are disabled (cli) when
 * these are written and are only accessed on the single boot CPU, so no
 * locking is required. If SMP support is added, these must become per-CPU. */
uint32_t syscall_saved_es = 0;
uint32_t syscall_saved_fs = 0;
uint32_t syscall_saved_gs = 0;

#define BLUEY_ENOSYS 38
#define BLUEY_EPERM   1
#define BLUEY_ENOENT  2
#define BLUEY_EFAULT 14
#define BLUEY_EINVAL 22
#define BLUEY_E2BIG   7
#define BLUEY_EAGAIN 11
#define BLUEY_ENOTDIR 20
#define BLUEY_EBADF   9
#define BLUEY_EISDIR 21
#define BLUEY_ERANGE 34
#define BLUEY_EPIPE  32
#define BLUEY_EMFILE 24

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

static int32_t sys_clock_gettime(int clk_id, k_timespec_t *tp) {
    (void)clk_id;
    if (!tp) return -BLUEY_EFAULT;
    uint32_t ticks = timer_get_ticks();
    uint32_t freq = timer_get_freq();
    uint32_t sec = ticks / freq;
    uint32_t rem = ticks % freq;
    /* Avoid 64-bit division helper (`__udivdi3`) by scaling in 32-bit when
     * timer frequency is small (typical PIT=1000). Compute nsec = rem *
     * (1_000_000_000 / freq). This truncates sub-tick precision but
     * avoids pulling in libgcc helpers. */
    uint32_t scale = 1000000000u / (freq ? freq : 1000u);
    uint32_t nsec = rem * scale;
    tp->tv_sec = sec;
    tp->tv_nsec = nsec;
    return 0;
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
    return vfs_unlink(path) == 0 ? 0 : -BLUEY_ENOENT;
}

static int32_t sys_mkdir(const char *path, uint32_t mode) {
    (void)mode;
    if (!path) return -BLUEY_EFAULT;
    return vfs_mkdir(path) == 0 ? 0 : -BLUEY_ENOENT;
}

static int32_t sys_rmdir(const char *path) {
    if (!path) return -BLUEY_EFAULT;
    return vfs_rmdir(path) == 0 ? 0 : -BLUEY_ENOENT;
}

/* ---- lseek -------------------------------------------------------------- */

static int32_t sys_lseek(int fd, int32_t offset, int whence) {
    if (fd < 0) return -BLUEY_EBADF;
    int32_t r = vfs_lseek(fd, offset, whence);
    if (r < 0) return -BLUEY_EINVAL;
    return r;
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
            return r < 0 ? -BLUEY_EMFILE : r;
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
    (void)fd;
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

static int32_t sys_chdir(const char *path) {
    if (!path) return -BLUEY_EFAULT;
    vfs_stat_t st;
    if (vfs_stat(path, &st) != 0) return -BLUEY_ENOENT;
    if (!st.is_dir) return -BLUEY_ENOTDIR;
    process_set_cwd(path);
    return 0;
}

static int32_t sys_getcwd(char *buf, size_t size) {
    if (!buf || size == 0) return -BLUEY_EFAULT;
    const char *cwd = process_get_cwd();
    size_t len = strlen(cwd);
    if (len + 1 > size) return -BLUEY_ERANGE;
    strncpy(buf, cwd, size - 1);
    buf[size - 1] = '\0';
    return (int32_t)(uintptr_t)buf;
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
    if (vfs_fstat(fd, &fst) == 0 && !fst.is_dir) return -BLUEY_ENOTDIR;

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

/* ---- getrandom ---------------------------------------------------------- */

static int32_t sys_getrandom(void *buf, size_t buflen, uint32_t flags) {
    (void)flags;
    if (!buf) return -BLUEY_EFAULT;
    /* NOTE: This is a minimal pseudo-random implementation seeded from timer
     * ticks using an LCG. It is NOT cryptographically secure and should be
     * treated as a placeholder until a proper entropy source is available. */
    uint32_t state = timer_get_ticks() ^ 0xDEADBEEFu;
    uint8_t *out = (uint8_t *)buf;
    for (size_t i = 0; i < buflen; i++) {
        state = state * 1664525u + 1013904223u; /* LCG */
        out[i] = (uint8_t)(state >> 16);
    }
    return (int32_t)buflen;
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
    if (fd >= 3) {
        if (!buf) return -BLUEY_EFAULT;
        if (len == 0) return 0;
        int r = vfs_write((int)fd, (const uint8_t *)buf, len);
        if (r < 0) return -BLUEY_EBADF;
        return r;
    }
    return -BLUEY_EBADF;
}

static int32_t sys_open(const char *path, int flags) {
    if (!path) return -1;
    return vfs_open(path, flags);
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

    if (vfs_stat(path_copy, &stat) != 0) {
        result = -BLUEY_ENOENT;
        goto cleanup;
    }
    // Execute permission is still required even for setuid/setgid binaries.
    if (vfs_access(path_copy, VFS_ACCESS_EXEC) != 0 || stat.is_dir) {
        result = -BLUEY_EPERM;
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
    if (stat.mode & VFS_S_ISUID || stat.mode & VFS_S_ISGID) {
        uint32_t new_euid = process->euid;
        uint32_t new_egid = process->egid;
        if (stat.mode & VFS_S_ISUID) new_euid = stat.uid;
        if (stat.mode & VFS_S_ISGID) new_egid = stat.gid;
        process_set_effective_ids(process, new_euid, new_egid);
    }
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

// SYS_READ (0): fd=0 -> stdin -> TTY; fd>=3 -> VFS
static int32_t sys_read(uint32_t fd, char *buf, size_t len) {
    if (fd == 0) {
        if (!buf) return -BLUEY_EFAULT;
        if (len == 0) return 0;
        return tty_read(buf, len);
    }
    if (fd >= 3) {
        if (!buf) return -BLUEY_EFAULT;
        if (len == 0) return 0;
        int r = vfs_read((int)fd, (uint8_t *)buf, len);
        if (r < 0) return -BLUEY_EBADF;
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
            ret = (int32_t)process_get_euid();
            break;
        case SYS_GETGID:
            ret = (int32_t)process_get_egid();
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
        case SYS_WAITPID:
            ret = process_waitpid((int32_t)regs->ebx, (int*)regs->ecx, (int)regs->edx);
            break;
        case SYS_WAIT4:
            ret = sys_wait4((int32_t)regs->ebx, (int*)regs->ecx,
                            (int)regs->edx, (void*)regs->esi);
            break;
        case SYS_FORK:
        case SYS_CLONE:
            ret = sys_fork(regs);
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
        case SYS_SET_TID_ADDRESS:
            ret = sys_set_tid_address((void*)regs->ebx);
            break;
        case SYS_GETRANDOM:
            ret = sys_getrandom((void*)regs->ebx, (size_t)regs->ecx, regs->edx);
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
        /* ---- Device event channel --------------------------------------- */
        case SYS_DEVEV_OPEN:
            ret = sys_devev_open();
            break;
        /* ---- Reboot / poweroff ----------------------------------------- */
        case SYS_REBOOT:
            ret = sys_reboot(regs->ebx, regs->ecx, regs->edx);
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
