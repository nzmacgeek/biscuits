#pragma once
#include "../include/types.h"
void timer_init(uint32_t freq);
uint32_t timer_get_ticks(void);
void timer_sleep(uint32_t ms);
