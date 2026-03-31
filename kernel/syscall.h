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
#define SYS_UNLINK        10
#define SYS_EXECVE        11
#define SYS_LSEEK         19
#define SYS_GETPID        20
#define SYS_MOUNT         21
#define SYS_GETUID        24
#define SYS_FORK          57
#define SYS_CLONE         120
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
#define SYS_SETTIMEOFDAY  79
#define SYS_RT_SIGACTION  174
#define SYS_RT_SIGPROCMASK 175
#define SYS_SIGRETURN     15
#define SYS_GETHOSTNAME   125
#define SYS_SETHOSTNAME   74
#define SYS_GETDENTS      141
#define SYS_MKDIR         39
#define SYS_RMDIR         40
#define SYS_DUP           41
#define SYS_ACCESS        85
#define SYS_LSTAT         107
#define SYS_FCNTL         55
#define SYS_GETPPID       64
#define SYS_WAIT4         114
#define SYS_SCHED_YIELD   158
#define SYS_NANOSLEEP     162
#define SYS_EXIT_GROUP    252
#define SYS_SET_TID_ADDRESS 258
#define SYS_GETRANDOM     355
// Process groups
#define SYS_SETPGID       200
#define SYS_GETPGID       201
#define SYS_GETPGRP       202
// Mount / unmount
#define SYS_UMOUNT2       52
// Poll (event multiplexing for supervision and sockets)
#define SYS_POLL          168
// Device event channel
#define SYS_DEVEV_OPEN    203
// Reboot / poweroff
#define SYS_REBOOT        88

void syscall_init(void);
// Called from syscall.asm - dispatches based on eax syscall number
int32_t syscall_dispatch(registers_t *regs);
