#ifndef _BITS_SYSCALL_H
#define _BITS_SYSCALL_H

/* BlueyOS syscall numbers for musl i386 build
 * This maps musl's __NR_* names to BlueyOS syscall numbers
 * (sourced from kernel/syscall.h in-tree).
 */

#define __NR_read          0
#define __NR_write         1
#define __NR_open          2
#define __NR_close         3
#define __NR_sync          36
#define __NR_stat          4
#define __NR_fstat         5
#define __NR_fstat64       197
#define __NR_writev        146
#define __NR_unlink        10
#define __NR_execve        11
#define __NR_lseek         19
#define __NR_getpid        20
#define __NR_mount         21
#define __NR_getuid        24
#define __NR_fork          57
#define __NR_clone         120
#define __NR_getgid        47
#define __NR_mmap          90
#define __NR_mmap2         192
#define __NR_munmap        91
#define __NR_mprotect      92
#define __NR_kill          62
#define __NR_exit          60
#define __NR_uname         63
#define __NR_brk           45
#define __NR_ioctl         54
#define __NR_waitpid       61
#define __NR_chdir         80
#define __NR_getcwd        183
#define __NR_pipe          42
#define __NR_dup2          33
#define __NR_gettimeofday  78
#define __NR_clock_gettime 265
#define __NR_clock_gettime64 403
#define __NR_rt_sigaction  174
#define __NR_rt_sigprocmask 175
#define __NR_sigreturn     15
#define __NR_getrandom     355
#define __NR_getppid       64
#define __NR_wait4         114
#define __NR_sched_yield   158
#define __NR_nanosleep     162
#define __NR_exit_group    252
#define __NR_set_tid_address 258
#define __NR_set_robust_list 311
#define __NR_rseq          386

#endif
