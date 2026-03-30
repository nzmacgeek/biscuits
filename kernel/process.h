#pragma once
// BlueyOS Process Management
// "Everyone gets a turn!" - Bandit (Bluey S1E1: "Waterhose")
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "idt.h"

#define MAX_PROCESSES  64
#define PROC_STACK_SIZE 8192   // 8 KiB per process stack

typedef enum {
    PROC_READY,     // ready to run - like Bluey ready for a new game
    PROC_RUNNING,   // currently running - it's their turn!
    PROC_SLEEPING,  // waiting for a timer - nap time
    PROC_WAITING,   // waiting on a child or event
    PROC_STOPPED,   // stopped by signal delivery
    PROC_ZOMBIE,    // finished but not reaped - "Dad, I'm done!"
    PROC_DEAD       // fully cleaned up
} proc_state_t;

typedef struct {
    uint32_t handler;
    uint32_t mask;
    uint32_t flags;
} process_sigaction_t;

typedef struct process {
    uint32_t      pid;
    uint32_t      parent_pid;
    char          name[32];
    proc_state_t  state;
    uint32_t      flags;
    uint32_t      esp;           // saved stack pointer (context switch)
    uint32_t      eip;           // saved instruction pointer
    uint32_t      stack_base;    // bottom of this process's kernel stack
    uint32_t      user_stack_base;
    uint32_t      user_stack_top;
    uint32_t      page_dir;      // physical address of page directory
    uint32_t      uid;           // user id
    uint32_t      gid;           // group id
    int           exit_code;
    uint32_t      sleep_until;   // timer tick to wake at (0 = not sleeping)
    uint32_t      priority;      // 1 (low) .. 10 (high) - Bluey always gets priority :)
    uint32_t      pending_signals;
    uint32_t      blocked_signals;
    uint32_t      brk_base;
    uint32_t      brk_current;
    uint32_t      mmap_base;
    int32_t       wait_pid;
    uint32_t      wait_status_ptr;
    uint32_t      wait_options;
    registers_t   saved_regs;
    process_sigaction_t signal_actions[32];
    struct process *next;
    struct process *sched_next;
} process_t;

#define PROC_FLAG_USER_MODE 0x00000001u
#define PROC_FLAG_SIGNAL_ACTIVE 0x00000002u

void       process_init(void);
process_t *process_create(const char *name, void (*entry)(void), uint32_t uid, uint32_t gid);
process_t *process_create_image(const char *name, uint32_t entry, uint32_t user_esp,
                                uint32_t user_stack_base, uint32_t user_stack_top,
                                uint32_t page_dir,
                                uint32_t uid, uint32_t gid);
process_t *process_fork_current(const registers_t *regs);
void       process_exec_replace(process_t *process, const char *name,
                                uint32_t entry, uint32_t user_esp,
                                uint32_t user_stack_base, uint32_t user_stack_top,
                                uint32_t page_dir);
void       process_set_memory_layout(process_t *process, uint32_t image_end);
void       process_mark_exited(process_t *process, int code);
void       process_exit(int code);
process_t *process_current(void);
void       process_set_current(process_t *p);
process_t *process_get_by_pid(uint32_t pid);
process_t *process_first(void);
process_t *process_next(process_t *p);
int32_t    process_waitpid(int32_t pid, int *status, int options);
uint32_t   process_getpid(void);
void       process_sleep(uint32_t ms);
void       process_wake(uint32_t pid);
void       process_enter_first_user(process_t *process);
