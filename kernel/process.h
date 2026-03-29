#pragma once
// BlueyOS Process Management
// "Everyone gets a turn!" - Bandit (Bluey S1E1: "Waterhose")
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"

#define MAX_PROCESSES  64
#define PROC_STACK_SIZE 8192   // 8 KiB per process stack

typedef enum {
    PROC_READY,     // ready to run - like Bluey ready for a new game
    PROC_RUNNING,   // currently running - it's their turn!
    PROC_SLEEPING,  // waiting for a timer - nap time
    PROC_ZOMBIE,    // finished but not reaped - "Dad, I'm done!"
    PROC_DEAD       // fully cleaned up
} proc_state_t;

typedef struct process {
    uint32_t      pid;
    char          name[32];
    proc_state_t  state;
    uint32_t      esp;           // saved stack pointer (context switch)
    uint32_t      eip;           // saved instruction pointer
    uint32_t      stack_base;    // bottom of this process's stack
    uint32_t      page_dir;      // physical address of page directory
    uint32_t      uid;           // user id
    uint32_t      gid;           // group id
    int           exit_code;
    uint32_t      sleep_until;   // timer tick to wake at (0 = not sleeping)
    uint32_t      priority;      // 1 (low) .. 10 (high) - Bluey always gets priority :)
    struct process *next;
} process_t;

void       process_init(void);
process_t *process_create(const char *name, void (*entry)(void), uint32_t uid, uint32_t gid);
void       process_exit(int code);
process_t *process_current(void);
process_t *process_get_by_pid(uint32_t pid);
uint32_t   process_getpid(void);
void       process_sleep(uint32_t ms);
void       process_wake(uint32_t pid);
