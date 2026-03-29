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
#define SYS_GETPID        20
#define SYS_GETUID        24
#define SYS_GETGID        47
#define SYS_EXECVE        11
#define SYS_EXIT          60
#define SYS_UNAME         63
#define SYS_GETTIMEOFDAY  78
#define SYS_SETTIMEOFDAY  79
#define SYS_GETHOSTNAME   125
#define SYS_SETHOSTNAME   74
#define SYS_GETDENTS      141

void syscall_init(void);
// Called from syscall.asm - dispatches based on eax syscall number
int32_t syscall_dispatch(registers_t regs);
