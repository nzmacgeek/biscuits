#pragma once
#include "../include/types.h"

typedef void (*kprintf_output_hook_t)(char c, void *ctx);

typedef struct {
	kprintf_output_hook_t hook;
	void                 *ctx;
} kprintf_output_state_t;

void kprintf_direct(const char *fmt, ...);
void kprintf_set_output_hook(kprintf_output_hook_t hook, void *ctx);
kprintf_output_state_t kprintf_get_output_state(void);
void kprintf_restore_output_state(kprintf_output_state_t state);
void kprintf(const char *fmt, ...);
