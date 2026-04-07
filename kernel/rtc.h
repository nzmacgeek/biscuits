#pragma once

#include "../include/types.h"

void        rtc_init(void);
void        rtc_notify_tick(void);
void        rtc_poll(void);
void        rtc_poll_if_pending(void);
bool        rtc_get_unix_time(uint32_t *unix_secs);
uint32_t    rtc_get_uptime_seconds(void);
const char *rtc_source_name(void);
/* Set the kernel's RTC-backed wall clock (unix seconds). Caller should
 * ensure appropriate privileges. */
void        rtc_set_unix_time(uint32_t unix_secs);