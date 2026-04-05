#pragma once

#include "../include/types.h"
#include "idt.h"

typedef struct process process_t;

#define SIG_DFL ((uint32_t)0)
#define SIG_IGN ((uint32_t)1)

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

typedef struct {
	uint32_t sa_handler;
	uint32_t sa_flags;
	uint32_t sa_mask;
} bluey_sigaction_t;

#define SIGHUP    1
#define SIGINT    2
#define SIGQUIT   3
#define SIGILL    4
#define SIGABRT   6
#define SIGSEGV   11
#define SIGKILL   9
#define SIGALRM   14
#define SIGTERM   15
#define SIGCHLD   17
#define SIGCONT   18
#define SIGSTOP   19

const char *signal_name(int sig);
void        signal_init(void);
bool        signal_is_valid(int sig);
int         signal_send_pid(uint32_t pid, int sig);
int         signal_send_pgrp(uint32_t pgid, int sig);
int         signal_dispatch_pending(process_t *process, registers_t *regs);
int         signal_sigaction(process_t *process, int sig,
							 const bluey_sigaction_t *act,
							 bluey_sigaction_t *oldact);
int         signal_sigprocmask(process_t *process, int how,
							   const uint32_t *set,
							   uint32_t *oldset);
int         signal_sigreturn(process_t *process, registers_t *regs, void *frame_ptr);
void        signal_reset_on_exec(process_t *process);