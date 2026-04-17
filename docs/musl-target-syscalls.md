The following is a snippet from the `kernel/syscall/syscall.h` file, which defines the system call numbers for the BlueyOS operating system on the i386 architecture. These are the current system call numbers, but more will be coming.

```c
#define SYS_READ          0
#define SYS_WRITE         1
#define SYS_OPEN          2
#define SYS_CLOSE         3
#define SYS_SYNC          36
#define SYS_STAT          4
#define SYS_FSTAT         5
#define SYS_LINK          9
#define SYS_UNLINK        10
#define SYS_EXECVE        11
#define SYS_CHMOD         15
#define SYS_LCHOWN        16  /* lchown (16-bit uid/gid variant) */
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
#define SYS_SIGRETURN     119
#define SYS_GETHOSTNAME   125
#define SYS_SETHOSTNAME   74
#define SYS_GETDENTS      141
#define SYS_MKDIR         39
#define SYS_RMDIR         40
#define SYS_DUP           41
#define SYS_SYMLINK       83
#define SYS_READLINK      84
#define SYS_ACCESS        85
#define SYS_FCHMOD        94
#define SYS_FCHOWN        95
#define SYS_FCHDIR        133
#define SYS_LSTAT         107
#define SYS_FCNTL         55
#define SYS_GETPPID       64
#define SYS_WAIT4         114
#define SYS_SCHED_YIELD   158
#define SYS_NANOSLEEP     162
#define SYS_CHOWN         182
#define SYS_EXIT_GROUP    252
#define SYS_SET_TID_ADDRESS 258
#define SYS_GETRANDOM     355
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
// Device event channel
#define SYS_DEVEV_OPEN    1003
// Reboot / poweroff
#define SYS_REBOOT        88
```

Device event ABI includes `DEV_EV_CTRL_ALT_DEL` in `kernel/devev.h`, emitted by
the keyboard driver when Ctrl+Alt+Del is pressed so PID 1 can choose policy.
