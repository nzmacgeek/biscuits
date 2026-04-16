#pragma once
// BlueyOS Syscall Interface - "Bluey's Daddy Daughter Syscalls"
// int 0x80 gateway from user space (ring3) to kernel (ring0)
// Episode ref: "Daddy Daughter Day" - Bandit drops everything to help Bluey
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "idt.h"

// Syscall numbers - Linux-compatible where sensible
#define SYS_READ          0
#define SYS_WRITE         1
#define SYS_OPEN          2
#define SYS_CLOSE         3
#define SYS_STAT          4
#define SYS_FSTAT         5
#define SYS_FSTAT64       197
#define SYS_GETUID32      199
#define SYS_WRITEV        146
#define SYS_LINK          9
#define SYS_UNLINK        10
#define SYS_EXECVE        11
#define SYS_CHMOD         15
#define SYS_LCHOWN        16  /* lchown (16-bit uid/gid variant) */
#define SYS_LSEEK         19
#define SYS_SELECT        142
#define SYS_FLOCK         143
#define SYS_GETPID        20
#define SYS_MOUNT         21
#define SYS_GETUID        24
#define SYS_FORK          57
#define SYS_CLONE         120
#define SYS_SOCKETCALL    102
#define SYS_GETGID        47
#define SYS_MMAP          90
#define SYS_MMAP2         192
#define SYS_MUNMAP        91
#define SYS_MPROTECT      92
#define SYS_KILL          62
#define SYS_EXIT          60
#define SYS_UNAME         63
#define SYS_BRK           45
#define SYS_IOCTL         54
#define SYS_WAITPID       61
#define SYS_CHDIR         80
#define SYS_GETCWD        183
#define SYS_PIPE          42
#define SYS_DUP2          33
#define SYS_GETTIMEOFDAY  78
#define SYS_CLOCK_GETTIME 265
#define SYS_CLOCK_GETTIME64 403
/* clock_settime64 syscall number (Linux i386 time64 ABI) */
#define SYS_CLOCK_SETTIME64 404
#define SYS_SETTIMEOFDAY  79
#define SYS_RT_SIGACTION  174
#define SYS_RT_SIGPROCMASK 175
#define SYS_SIGRETURN     119
#define SYS_GETHOSTNAME   125
#define SYS_SETHOSTNAME   74
#define SYS_SETRLIMIT     75
#define SYS_GETRLIMIT     76
#define SYS_GETDENTS      141
#define SYS_GETDENTS64    220
#define SYS_MKDIR         39
#define SYS_RMDIR         40
#define SYS_DUP           41
#define SYS_SYMLINK       83
#define SYS_READLINK      84
#define SYS_ACCESS        85
#define SYS_RENAME        38
#define SYS_FCHMOD        94
#define SYS_FCHOWN        95
#define SYS_FCHDIR        133
#define SYS_LSTAT         107
#define SYS_FCNTL         55
#define SYS_FCNTL64       221
#define SYS_GETPPID       64
#define SYS_WAIT4         114
#define SYS_SETSID        66
#define SYS_SCHED_YIELD   158
#define SYS_NANOSLEEP     162
#define SYS_CHOWN         182
#define SYS_EXIT_GROUP    252
#define SYS_SET_TID_ADDRESS 258
#define SYS_SET_ROBUST_LIST 311
#define SYS_GETRANDOM     355
#define SYS_STATX         383
#define SYS_RSEQ          386
#define SYS_SOCKET        359
#define SYS_BIND          361
#define SYS_CONNECT       362
#define SYS_LISTEN        363
#define SYS_ACCEPT4       364
#define SYS_SENDMSG       366
#define SYS_RECVMSG       367
// chown/fchown/lchown 32-bit uid variants (used by musl on i386)
#define SYS_LCHOWN32      198
#define SYS_GETGID32      200
#define SYS_GETEUID32     201
#define SYS_GETEGID32     202
#define SYS_SETREUID32    203
#define SYS_SETREGID32    204
#define SYS_GETGROUPS32   205
#define SYS_SETGROUPS32   206
#define SYS_FCHOWN32      207
#define SYS_SETRESUID32   208
#define SYS_GETRESUID32   209
#define SYS_SETRESGID32   210
#define SYS_GETRESGID32   211
#define SYS_CHOWN32       212
#define SYS_SETUID32      213
#define SYS_SETGID32      214
#define SYS_SETFSUID32    215
#define SYS_SETFSGID32    216
// Process groups
#define SYS_SETPGID       1000
#define SYS_GETPGID       1001
#define SYS_GETPGRP       1002
// Mount / unmount
#define SYS_UMOUNT2       52
// Poll (event multiplexing for supervision and sockets)
#define SYS_POLL          168
#define SYS_PSELECT6      308
// Device event channel
#define SYS_DEVEV_OPEN    1003
// Reboot / poweroff
#define SYS_REBOOT        88
#define SYS_VFORK         190
#define SYS_UGETRLIMIT    191
#define SYS_PRLIMIT64     340
// Thread-local storage (set_thread_area - sets GS segment base for musl TLS)
#define SYS_SET_THREAD_AREA 243
#define SYS_MODIFY_LDT      123

/* Legacy 32-bit llseek syscall used by some libc variants */
#define SYS__llseek        140

// Module loading syscalls (Linux compatible)
#define SYS_INIT_MODULE    128
#define SYS_DELETE_MODULE  129

// pipe2 — like pipe() but accepts O_CLOEXEC / O_NONBLOCK flags (bash uses this)
#define SYS_PIPE2          331

// faccessat2 — extended access check (bash uses this for path resolution)
#define SYS_FACCESSAT2     439

void syscall_init(void);
// Called from syscall.asm - dispatches based on eax syscall number
int32_t syscall_dispatch(registers_t *regs);
